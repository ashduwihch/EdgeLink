#include "modbus_rtu_transport.h"

#include "edgelink/logger.h"

#include <cerrno>
#include <utility>

namespace edgelink
{

/**
 * @brief 保存RTU串口配置，延迟到connect时再打开设备。
 */
ModbusRtuTransport::ModbusRtuTransport(ModbusRtuConfig config)
    : config_(std::move(config))
{
}

/**
 * @brief 断开连接并释放libmodbus上下文。
 */
ModbusRtuTransport::~ModbusRtuTransport()
{
    disconnect();

    if (context_ != nullptr)
    {
        modbus_free(context_);
        context_ = nullptr;
    }
}

/**
 * @brief 按配置创建RTU上下文、设置从站并打开串口。
 */
bool ModbusRtuTransport::connect()
{
    if (connected_)
    {
        return true;
    }

    if (!validateConfig())
    {
        Logger::error("Invalid Modbus RTU configuration");
        return false;
    }

    if (context_ == nullptr)
    {
        context_ = modbus_new_rtu(
            config_.serialDevice.c_str(),
            config_.baudRate,
            config_.parity,
            config_.dataBits,
            config_.stopBits);

        if (context_ == nullptr)
        {
            Logger::error("Failed to create Modbus RTU context");
            return false;
        }

        if (modbus_set_slave(context_, config_.slaveId) < 0)
        {
            Logger::error("Failed to set Modbus RTU slave ID");
            modbus_free(context_);
            context_ = nullptr;
            return false;
        }
    }

    if (modbus_connect(context_) < 0)
    {
        Logger::error(
            "Failed to open Modbus RTU serial device: " +
            std::string(modbus_strerror(errno)));
        return false;
    }

    connected_ = true;
    Logger::info("Modbus RTU transport connected");
    return true;
}

/**
 * @brief 关闭libmodbus打开的串口文件描述符。
 */
void ModbusRtuTransport::disconnect()
{
    if (!connected_)
    {
        return;
    }

    modbus_close(context_);
    connected_ = false;
    Logger::info("Modbus RTU transport disconnected");
}

/**
 * @brief 返回RTU串口连接标志。
 */
bool ModbusRtuTransport::isConnected() const
{
    return connected_;
}

/**
 * @brief 通过libmodbus功能码03读取保持寄存器。
 */
bool ModbusRtuTransport::readHoldingRegisters(
    int address,
    int count,
    std::vector<std::uint16_t>& output)
{
    if (!connected_)
    {
        Logger::error("Modbus RTU transport is not connected");
        return false;
    }

    if (address < 0 || count <= 0 || count > 125 ||
        address > 65535 - (count - 1))
    {
        Logger::error("Invalid Modbus RTU register range");
        return false;
    }

    std::vector<std::uint16_t> registers(static_cast<std::size_t>(count));
    const int actualCount = modbus_read_registers(
        context_,
        address,
        count,
        registers.data());

    if (actualCount != count)
    {
        Logger::error(
            "Failed to read Modbus RTU registers: " +
            std::string(modbus_strerror(errno)));
        return false;
    }

    output = std::move(registers);
    return true;
}

/**
 * @brief 校验设备路径、串口格式和Modbus从站范围。
 */
bool ModbusRtuTransport::validateConfig() const
{
    const bool parityValid =
        config_.parity == 'N' ||
        config_.parity == 'E' ||
        config_.parity == 'O';

    return !config_.serialDevice.empty() &&
           config_.baudRate > 0 &&
           parityValid &&
           config_.dataBits >= 5 && config_.dataBits <= 8 &&
           (config_.stopBits == 1 || config_.stopBits == 2) &&
           config_.slaveId >= 1 && config_.slaveId <= 247;
}

} // namespace edgelink
