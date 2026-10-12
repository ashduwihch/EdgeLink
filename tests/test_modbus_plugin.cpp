#include "modbus_plugin.h"

#include <modbus.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>
#include <variant>

/**
 * @brief 检查Tag是否包含期望的double值和Good质量。
 * @param device 待查询的设备。
 * @param tagName Tag名称。
 * @param expectedValue 期望工程值。
 * @return Tag存在且数值、质量均正确返回true。
 */
bool hasGoodDoubleTag(
    const edgelink::Device* device,
    const std::string& tagName,
    double expectedValue)
{
    const edgelink::Tag* tag =
        device != nullptr ? device->findTag(tagName) : nullptr;

    return tag != nullptr &&
           tag->quality == edgelink::Quality::Good &&
           std::holds_alternative<double>(tag->value) &&
           std::fabs(std::get<double>(tag->value) - expectedValue) < 0.0001;
}

/**
 * @brief 启动本地Modbus TCP模拟服务器，验证多Tag采集、换算和失败隔离。
 * @return 全部断言通过返回0，否则返回1。
 */
int main()
{
    // 先让操作系统分配一个空闲端口，再交给libmodbus监听。
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
    modbus_mapping_t* mapping = modbus_mapping_new(0, 0, 6, 0);

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

    // 寄存器0：253，按0.1换算为温度25.3。
    mapping->tab_registers[0] = 253;
    // 寄存器1：有符号-20，按-20 * 0.5 + 2换算为-8。
    mapping->tab_registers[1] = static_cast<std::uint16_t>(-20);
    // 寄存器2和3：32位无符号数100000，换算为1000。
    mapping->tab_registers[2] = 0x0001;
    mapping->tab_registers[3] = 0x86A0;
    // 寄存器4和5：IEEE 754浮点数12.5，换算为26。
    float rawFlow = 12.5F;
    std::uint32_t rawFlowBits = 0;
    std::memcpy(&rawFlowBits, &rawFlow, sizeof(rawFlow));
    mapping->tab_registers[4] = static_cast<std::uint16_t>(rawFlowBits >> 16U);
    mapping->tab_registers[5] = static_cast<std::uint16_t>(rawFlowBits);

    int serverSocket = modbus_tcp_listen(serverContext, 1);

    if (serverSocket < 0)
    {
        std::cerr << "[FAIL] Failed to listen for Modbus test client" << std::endl;
        modbus_mapping_free(mapping);
        modbus_free(serverContext);
        return 1;
    }

    constexpr int expectedRequests = 5;
    std::atomic<int> repliedRequests{0};

    // 模拟设备在独立线程中接收并回复每个Tag的读取请求。
    std::thread server([&]()
    {
        if (modbus_tcp_accept(serverContext, &serverSocket) < 0)
        {
            return;
        }

        for (int index = 0; index < expectedRequests; ++index)
        {
            std::uint8_t query[MODBUS_TCP_MAX_ADU_LENGTH]{};
            int queryLength = modbus_receive(serverContext, query);

            if (queryLength <= 0 ||
                modbus_reply(serverContext, query, queryLength, mapping) < 0)
            {
                break;
            }

            ++repliedRequests;
        }

        modbus_close(serverContext);
    });

    edgelink::DeviceManager deviceManager;
    edgelink::ModbusTcpConfig config;
    config.port = port;
    config.deviceId = "process-device-01";
    config.deviceName = "Process Device";
    config.tags = {
        {"temperature", 0, edgelink::ModbusDataType::UInt16, 0.1, 0.0},
        {"signed-adjusted", 1, edgelink::ModbusDataType::Int16, 0.5, 2.0},
        {"energy", 2, edgelink::ModbusDataType::UInt32, 0.01, 0.0},
        {"flow", 4, edgelink::ModbusDataType::Float32, 2.0, 1.0},
        // 越界地址用来验证：一个Tag失败后，其他Tag仍可正常更新。
        {"missing", 20, edgelink::ModbusDataType::UInt16, 1.0, 0.0}};

    edgelink::ModbusPlugin plugin(&deviceManager, config);
    bool started = plugin.initialize() && plugin.start();
    plugin.stop();

    server.join();
    close(serverSocket);

    const edgelink::Device* device =
        deviceManager.findDevice("process-device-01");
    const edgelink::Tag* missingTag =
        device != nullptr ? device->findTag("missing") : nullptr;

    // 重复Tag名会导致后一个值覆盖前一个，所以必须在初始化阶段拒绝。
    edgelink::ModbusTcpConfig invalidConfig;
    invalidConfig.tags = {
        {"duplicate", 0, edgelink::ModbusDataType::UInt16, 1.0, 0.0},
        {"duplicate", 1, edgelink::ModbusDataType::UInt16, 1.0, 0.0}};
    edgelink::ModbusPlugin invalidPlugin(nullptr, invalidConfig);

    bool passed = started &&
                  repliedRequests == expectedRequests &&
                  device != nullptr &&
                  hasGoodDoubleTag(device, "temperature", 25.3) &&
                  hasGoodDoubleTag(device, "signed-adjusted", -8.0) &&
                  hasGoodDoubleTag(device, "energy", 1000.0) &&
                  hasGoodDoubleTag(device, "flow", 26.0) &&
                  missingTag != nullptr &&
                  missingTag->quality == edgelink::Quality::Timeout &&
                  !invalidPlugin.initialize();

    modbus_mapping_free(mapping);
    modbus_free(serverContext);

    if (!passed)
    {
        std::cerr << "[FAIL] Configurable Modbus multi-Tag polling failed" << std::endl;
        return 1;
    }

    std::cout << "[PASS] Configurable Modbus multi-Tag polling finished" << std::endl;
    return 0;
}
