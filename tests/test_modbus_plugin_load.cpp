#include "edgelink/plugin_manager.h"

#include <iostream>

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cerr << "[FAIL] Missing Modbus plugin path" << std::endl;
        return 1;
    }

    edgelink::PluginManager manager;

    if (!manager.loadPlugin(argv[1]))
    {
        std::cerr << "[FAIL] Failed to load Modbus plugin" << std::endl;
        return 1;
    }

    if (!manager.hasPlugin("ModbusPlugin"))
    {
        std::cerr << "[FAIL] Modbus plugin was not registered" << std::endl;
        return 1;
    }

    if (!manager.unloadPlugin("ModbusPlugin"))
    {
        std::cerr << "[FAIL] Failed to unload Modbus plugin" << std::endl;
        return 1;
    }

    std::cout << "[PASS] Modbus plugin load test finished" << std::endl;
    return 0;
}
