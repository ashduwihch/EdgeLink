#include "modbus_plugin.h"

#include <modbus.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <thread>
#include <variant>

int main()
{
    // 先让操作系统分配一个空闲端口，再交给libmodbus监听
    int portSocket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in portAddress{};
    portAddress.sin_family = AF_INET;
    portAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    portAddress.sin_port = 0;

    if (portSocket < 0 ||
        bind(
            portSocket,
            reinterpret_cast<sockaddr*>(&portAddress),
            sizeof(portAddress)) < 0)
    {
        std::cerr << "[FAIL] Failed to reserve Modbus test port" << std::endl;

        if (portSocket >= 0)
        {
            close(portSocket);
        }

        return 1;
    }

    socklen_t addressLength = sizeof(portAddress);

    if (getsockname(
            portSocket,
            reinterpret_cast<sockaddr*>(&portAddress),
            &addressLength) < 0)
    {
        std::cerr << "[FAIL] Failed to query Modbus test port" << std::endl;
        close(portSocket);
        return 1;
    }

    int port = ntohs(portAddress.sin_port);
    close(portSocket);

    modbus_t* serverContext = modbus_new_tcp("127.0.0.1", port);
    modbus_mapping_t* mapping = modbus_mapping_new(0, 0, 1, 0);

    if (serverContext == nullptr || mapping == nullptr)
    {
        std::cerr << "[FAIL] Failed to create Modbus test server" << std::endl;

        if (mapping != nullptr)
        {
            modbus_mapping_free(mapping);
        }

        if (serverContext != nullptr)
        {
            modbus_free(serverContext);
        }

        return 1;
    }
    mapping->tab_registers[0] = 253;
    int serverSocket = modbus_tcp_listen(serverContext, 1);
    if (serverSocket < 0)
    {
        std::cerr << "[FAIL] Failed to listen for Modbus test client" << std::endl;
        modbus_mapping_free(mapping);
        modbus_free(serverContext);
        return 1;
    }

    std::atomic<bool> serverSucceeded{false};

    std::thread server([&]()
    {
        if (modbus_tcp_accept(serverContext, &serverSocket) < 0)
        {
            return;
        }

        std::uint8_t query[MODBUS_TCP_MAX_ADU_LENGTH]{};
        int queryLength = modbus_receive(serverContext, query);

        if (queryLength > 0 &&
            modbus_reply(serverContext, query, queryLength, mapping) >= 0)
        {
            serverSucceeded = true;
        }

        modbus_close(serverContext);
    });

    edgelink::DeviceManager deviceManager;
    edgelink::ModbusTcpConfig config;
    config.port = port;
    config.registerAddress = 0;
    config.scale = 0.1;
    config.deviceId = "temperature-sensor-01";
    config.deviceName = "Temperature Sensor";
    config.tagName = "temperature";

    edgelink::ModbusPlugin plugin(&deviceManager, config);
    bool started = plugin.initialize() && plugin.start();
    plugin.stop();

    server.join();
    close(serverSocket);

    const edgelink::Device* device =
        deviceManager.findDevice("temperature-sensor-01");
    const edgelink::Tag* tag =
        device != nullptr ? device->findTag("temperature") : nullptr;

    bool passed = started &&
                  serverSucceeded &&
                  device != nullptr &&
                  tag != nullptr &&
                  tag->quality == edgelink::Quality::Good &&
                  std::holds_alternative<double>(tag->value) &&
                  std::fabs(std::get<double>(tag->value) - 25.3) < 0.0001;

    modbus_mapping_free(mapping);
    modbus_free(serverContext);

    if (!passed)
    {
        std::cerr << "[FAIL] Modbus register was not converted to TagValue" << std::endl;
        return 1;
    }

    std::cout << "[PASS] Modbus register 253 converted to 25.3" << std::endl;
    return 0;
}
