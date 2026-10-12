#include "modbus_rtu_transport.h"

#include <modbus.h>

#include <pty.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

/**
 * @brief 用Linux伪终端模拟串口线，验证Modbus RTU请求和响应。
 * @return RTU寄存器读取正确返回0，否则返回1。
 */
int main()
{
    int masterFd = -1;
    int slaveFd = -1;
    char slaveName[128]{};

    if (openpty(&masterFd, &slaveFd, slaveName, nullptr, nullptr) < 0)
    {
        std::cerr << "[FAIL] Failed to create pseudo terminal" << std::endl;
        return 1;
    }

    // Transport会通过slaveName自行打开从端，测试不需要保留这个原始fd。
    close(slaveFd);

    modbus_t* serverContext = modbus_new_rtu(slaveName, 9600, 'N', 8, 1);
    modbus_mapping_t* mapping = modbus_mapping_new(0, 0, 2, 0);

    if (serverContext == nullptr || mapping == nullptr)
    {
        std::cerr << "[FAIL] Failed to create Modbus RTU simulator" << std::endl;

        if (mapping != nullptr)
        {
            modbus_mapping_free(mapping);
        }

        if (serverContext != nullptr)
        {
            modbus_free(serverContext);
        }

        close(masterFd);
        return 1;
    }

    mapping->tab_registers[0] = 1234;
    mapping->tab_registers[1] = 5678;
    modbus_set_slave(serverContext, 1);
    modbus_set_socket(serverContext, masterFd);

    std::atomic<bool> serverSucceeded{false};

    // 主端模拟RTU从站，接收一次请求并返回寄存器数据。
    std::thread server([&]()
    {
        std::uint8_t query[MODBUS_RTU_MAX_ADU_LENGTH]{};
        const int queryLength = modbus_receive(serverContext, query);

        if (queryLength > 0 &&
            modbus_reply(serverContext, query, queryLength, mapping) >= 0)
        {
            serverSucceeded = true;
        }
    });

    edgelink::ModbusRtuConfig config;
    config.serialDevice = slaveName;
    config.baudRate = 9600;
    config.parity = 'N';
    config.dataBits = 8;
    config.stopBits = 1;
    config.slaveId = 1;

    edgelink::ModbusRtuTransport transport(config);
    std::vector<std::uint16_t> registers;
    const bool connected = transport.connect();
    const bool readSucceeded =
        connected && transport.readHoldingRegisters(0, 2, registers);
    transport.disconnect();

    server.join();

    const bool passed =
        connected &&
        readSucceeded &&
        serverSucceeded &&
        !transport.isConnected() &&
        registers.size() == 2 &&
        registers[0] == 1234 &&
        registers[1] == 5678;

    modbus_mapping_free(mapping);
    modbus_free(serverContext);
    close(masterFd);

    if (!passed)
    {
        std::cerr << "[FAIL] Modbus RTU transport test failed" << std::endl;
        return 1;
    }

    std::cout << "[PASS] Modbus RTU registers: 1234/5678" << std::endl;
    return 0;
}
