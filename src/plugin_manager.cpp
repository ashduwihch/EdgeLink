#include "edgelink/plugin_manager.h"

#include "edgelink/logger.h"

#include <dlfcn.h> //Linux API

#include <string>
#include <vector>

namespace edgelink
{

    // 销毁管理器时卸载所有插件
    PluginManager::~PluginManager()
    {
        unloadAll();
    }


    // 加载并初始化插件
    bool PluginManager::loadPlugin(const std::string& path)
    {
        if (path.empty())
        {
            Logger::error("Plugin path is empty");
            return false;
        }

        // 打开动态库
        void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);

        if (handle == nullptr)
        {
            Logger::error("Failed to load plugin: " + std::string(dlerror()));
            return false;
        }

        // 清除之前可能存在的dlerror状态
        dlerror();

        // 查找插件创建入口
        auto createPlugin = reinterpret_cast<CreatePluginFunc>(dlsym(handle, "createPlugin"));

        const char* error = dlerror();

        if (error != nullptr)
        {
            Logger::error("Failed to find createPlugin: " + std::string(error));
            dlclose(handle);
            return false;
        }

        // 查找插件销毁入口
        dlerror();

        auto destroyPlugin = reinterpret_cast<DestroyPluginFunc>(dlsym(handle, "destroyPlugin"));

        error = dlerror();

        if (error != nullptr)
        {
            Logger::error("Failed to find destroyPlugin: " + std::string(error));
            dlclose(handle);
            return false;
        }

        // 创建插件对象
        Plugin* instance = createPlugin();

        if (instance == nullptr)
        {
            Logger::error("Failed to create plugin");
            dlclose(handle);
            return false;
        }

        const char* pluginName = instance->name();

        if (pluginName == nullptr || pluginName[0] == '\0')
        {
            Logger::error("Plugin name is empty");
            destroyPlugin(instance);
            dlclose(handle);
            return false;
        }

        std::string name = pluginName;

        // 不允许重复加载同名插件
        if (plugins_.find(name) != plugins_.end())
        {
            Logger::error("Plugin already loaded: " + name);
            destroyPlugin(instance);
            dlclose(handle);
            return false;
        }

        // 初始化插件
        if (!instance->initialize())
        {
            Logger::error("Plugin initialize failed: " + name);
            destroyPlugin(instance);
            dlclose(handle);
            return false;
        }

        LoadedPlugin plugin;
        plugin.handle = handle;
        plugin.instance = instance;
        plugin.destroyPlugin = destroyPlugin;
        plugin.path = path;

        plugins_.emplace(name, std::move(plugin));

        Logger::info("Plugin loaded: " + name);

        return true;
    }


    // 卸载指定插件
    bool PluginManager::unloadPlugin(const std::string& name)
    {
        auto plugin = plugins_.find(name);

        if (plugin == plugins_.end())
        {
            Logger::error("Plugin not found: " + name);
            return false;
        }

        LoadedPlugin& loadedPlugin = plugin->second;

        // 如果插件还在运行，先停止
        if (loadedPlugin.started)
        {
            loadedPlugin.instance->stop();
            loadedPlugin.started = false;
        }

        // 先销毁插件对象
        if (loadedPlugin.instance != nullptr && loadedPlugin.destroyPlugin != nullptr)
        {
            loadedPlugin.destroyPlugin(loadedPlugin.instance);
            loadedPlugin.instance = nullptr;
        }

        bool closeSuccess = true;

        // 再卸载动态库
        if (loadedPlugin.handle != nullptr)
        {
            if (dlclose(loadedPlugin.handle) != 0)
            {
                const char* error = dlerror();

                Logger::error("Failed to close plugin: " + std::string(error != nullptr ? error : "unknown error"));
                closeSuccess = false;
            }

            loadedPlugin.handle = nullptr;
        }

        plugins_.erase(plugin);

        Logger::info("Plugin unloaded: " + name);

        return closeSuccess;
    }


    // 获取指定插件
    Plugin* PluginManager::getPlugin(const std::string& name) const
    {
        auto plugin = plugins_.find(name);

        if (plugin == plugins_.end())
        {
            return nullptr;
        }

        return plugin->second.instance;
    }


    // 判断插件是否已经加载
    bool PluginManager::hasPlugin(const std::string& name) const
    {
        return plugins_.find(name) != plugins_.end();
    }


    // 启动指定插件
    bool PluginManager::startPlugin(const std::string& name)
    {
        auto plugin = plugins_.find(name);

        if (plugin == plugins_.end())
        {
            Logger::error("Plugin not found: " + name);
            return false;
        }

        LoadedPlugin& loadedPlugin = plugin->second;

        // 已经启动则直接返回成功
        if (loadedPlugin.started)
        {
            return true;
        }

        if (!loadedPlugin.instance->start())
        {
            Logger::error("Plugin start failed: " + name);
            return false;
        }

        loadedPlugin.started = true;

        Logger::info("Plugin started: " + name);

        return true;
    }


    // 停止指定插件
    void PluginManager::stopPlugin(const std::string& name)
    {
        auto plugin = plugins_.find(name);

        if (plugin == plugins_.end())
        {
            Logger::error("Plugin not found: " + name);
            return;
        }

        LoadedPlugin& loadedPlugin = plugin->second;

        if (!loadedPlugin.started)
        {
            return;
        }

        loadedPlugin.instance->stop();
        loadedPlugin.started = false;

        Logger::info("Plugin stopped: " + name);
    }


    // 启动所有插件
    bool PluginManager::startAll()
    {
        std::vector<std::string> startedPlugins;

        for (auto& plugin : plugins_)
        {
            if (plugin.second.started)
            {
                continue;
            }

            if (!plugin.second.instance->start())
            {
                Logger::error("Plugin start failed: " + plugin.first);

                // 如果中途失败，停止本次已经启动的插件
                for (const std::string& name : startedPlugins)
                {
                    stopPlugin(name);
                }

                return false;
            }

            plugin.second.started = true;
            startedPlugins.push_back(plugin.first);

            Logger::info("Plugin started: " + plugin.first);
        }

        return true;
    }


    // 停止所有插件
    void PluginManager::stopAll()
    {
        for (auto& plugin : plugins_)
        {
            if (!plugin.second.started)
            {
                continue;
            }

            plugin.second.instance->stop();
            plugin.second.started = false;

            Logger::info("Plugin stopped: " + plugin.first);
        }
    }


    // 卸载所有插件
    void PluginManager::unloadAll()
    {
        while (!plugins_.empty())
        {
            std::string name = plugins_.begin()->first;
            unloadPlugin(name);
        }
    }

} // namespace edgelink