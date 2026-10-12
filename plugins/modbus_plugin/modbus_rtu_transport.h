#pragma once

#include <modbus.h>

#include <cstdint>
#include <string>
#include <vector>

namespace edgelink
{

/**
 * @brief Modbus RTU串口通信参数。
 */
struct ModbusRtuConfig
{
    std::string serialDevice;
    int baudRate = 9600;
    char parity = 'N';
    int dataBits = 8;
    int stopBits = 1;
    int slaveId = 1;
};

/**
 * @brief 封装libmodbus的RTU串口连接和保持寄存器读取。
 *
 * 该类只负责串口传输，不解释寄存器的业务含义。
 */
class ModbusRtuTransport
{
public:
    /**
     * @brief 创建Modbus RTU传输对象。
     * @param config 串口参数和Modbus从站地址。
     */
    explicit ModbusRtuTransport(ModbusRtuConfig config);

    /**
     * @brief 关闭串口并释放libmodbus上下文。
     */
    ~ModbusRtuTransport();

    ModbusRtuTransport(const ModbusRtuTransport&) = delete;
    ModbusRtuTransport& operator=(const ModbusRtuTransport&) = delete;

    /**
     * @brief 校验参数、创建RTU上下文并打开串口。
     * @return 连接成功返回true。
     */
    bool connect();

    /**
     * @brief 关闭当前串口连接。
     */
    void disconnect();

    /**
     * @brief 查询串口是否已连接。
     * @return 已连接返回true。
     */
    bool isConnected() const;

    /**
     * @brief 读取一段Modbus保持寄存器。
     * @param address 起始寄存器地址。
     * @param count 寄存器数量，范围1到125。
     * @param output 成功时接收读取结果。
     * @return 读取成功返回true。
     */
    bool readHoldingRegisters(
        int address,
        int count,
        std::vector<std::uint16_t>& output);

private:
    /**
     * @brief 检查RTU串口参数是否可以交给libmodbus。
     * @return 参数合法返回true。
     */
    bool validateConfig() const;

    ModbusRtuConfig config_;
    modbus_t* context_ = nullptr;
    bool connected_ = false;
};

} // namespace edgelink
