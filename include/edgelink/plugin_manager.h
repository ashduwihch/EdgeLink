#pragma once

#include "edgelink/plugin.h"

#include <string>
#include <unordered_map>

namespace edgelink
{

    // PluginManager：负责插件的加载、运行、查询和卸载
    class PluginManager
    {
    public:
        PluginManager() = default;

        // 销毁管理器时自动卸载所有插件
        ~PluginManager();

        // 禁止复制，避免重复管理同一个动态库和插件对象
        PluginManager(const PluginManager&) = delete;
        PluginManager& operator=(const PluginManager&) = delete;

        // 加载并初始化插件
        bool loadPlugin(const std::string& path);

        // 卸载指定插件
        bool unloadPlugin(const std::string& name);

        // 获取指定插件
        Plugin* getPlugin(const std::string& name) const;

        // 判断插件是否已经加载
        bool hasPlugin(const std::string& name) const;

        // 启动指定插件
        bool startPlugin(const std::string& name);

        // 停止指定插件
        void stopPlugin(const std::string& name);

        // 启动所有插件
        bool startAll();

        // 停止所有插件
        void stopAll();

        // 卸载所有插件
        void unloadAll();

    private:
        // 保存一个已经加载的插件及其动态库信息
        struct LoadedPlugin
        {
            void* handle = nullptr;
            Plugin* instance = nullptr;
            DestroyPluginFunc destroyPlugin = nullptr;

            bool started = false;

            std::string path;
        };

        // 插件名称 -> 插件信息
        std::unordered_map<std::string, LoadedPlugin> plugins_;
    };

} // namespace edgelink