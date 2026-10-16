#include "edgelink/plugin_manager.h"

#include <iostream>

/**
 * @brief 验证PluginManager可动态加载和卸载SocketCAN插件。
 * @param argc 命令行参数数量。
 * @param argv 第二个参数为SocketCAN插件动态库路径。
 * @return 加载、查询和卸载都成功返回0。
 */
int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cerr << "[FAIL] Missing SocketCAN plugin path" << std::endl;
        return 1;
    }

    edgelink::PluginManager manager;

    if (!manager.loadPlugin(argv[1]))
    {
        std::cerr << "[FAIL] Failed to load SocketCAN plugin" << std::endl;
        return 1;
    }

    if (!manager.hasPlugin("SocketCanPlugin"))
    {
        std::cerr << "[FAIL] SocketCAN plugin was not registered" << std::endl;
        return 1;
    }

    if (!manager.unloadPlugin("SocketCanPlugin"))
    {
        std::cerr << "[FAIL] Failed to unload SocketCAN plugin" << std::endl;
        return 1;
    }

    std::cout << "[PASS] SocketCAN plugin load test finished" << std::endl;
    return 0;
}
