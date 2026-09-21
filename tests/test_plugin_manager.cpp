#include "edgelink/plugin_manager.h"

#include <iostream>
#include <string>

int main(int argc, char* argv[])
{
    std::string pluginPath = "./libmock_plugin.so";

    if (argc >= 2)
    {
        pluginPath = argv[1];
    }

    edgelink::PluginManager manager;

    // 无效路径必须返回失败，不能影响管理器继续工作
    if (manager.loadPlugin("./plugin-does-not-exist.so"))
    {
        std::cerr << "[FAIL] Invalid plugin path was accepted" << std::endl;
        return 1;
    }

    // 加载并初始化插件
    if (!manager.loadPlugin(pluginPath))
    {
        std::cerr << "[FAIL] Failed to load plugin" << std::endl;
        return 1;
    }

    // 同名插件不能被重复加载
    if (manager.loadPlugin(pluginPath))
    {
        std::cerr << "[FAIL] Duplicate plugin was accepted" << std::endl;
        return 1;
    }

    // 验证插件是否存在
    if (!manager.hasPlugin("MockPlugin"))
    {
        std::cerr << "[FAIL] Plugin not found after loading" << std::endl;
        return 1;
    }

    // 获取插件
    edgelink::Plugin* plugin = manager.getPlugin("MockPlugin");

    if (plugin == nullptr)
    {
        std::cerr << "[FAIL] Failed to get plugin" << std::endl;
        return 1;
    }

    std::cout << "Plugin name: " << plugin->name() << std::endl;

    // 启动插件
    if (!manager.startPlugin("MockPlugin"))
    {
        std::cerr << "[FAIL] Failed to start plugin" << std::endl;
        return 1;
    }

    // 重复启动采用幂等语义，已经启动时仍返回成功
    if (!manager.startPlugin("MockPlugin"))
    {
        std::cerr << "[FAIL] Repeated start was not idempotent" << std::endl;
        return 1;
    }

    // 停止插件
    manager.stopPlugin("MockPlugin");

    // 重复停止不应再次调用插件，也不应导致崩溃
    manager.stopPlugin("MockPlugin");

    // 卸载插件
    if (!manager.unloadPlugin("MockPlugin"))
    {
        std::cerr << "[FAIL] Failed to unload plugin" << std::endl;
        return 1;
    }

    // 确认插件已经不存在
    if (manager.hasPlugin("MockPlugin"))
    {
        std::cerr << "[FAIL] Plugin still exists after unloading" << std::endl;
        return 1;
    }

    if (manager.getPlugin("MockPlugin") != nullptr)
    {
        std::cerr << "[FAIL] Unloaded plugin can still be queried" << std::endl;
        return 1;
    }

    std::cout << "[PASS] PluginManager test finished" << std::endl;

    return 0;
}
