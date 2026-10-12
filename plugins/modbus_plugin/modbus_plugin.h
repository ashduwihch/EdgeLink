#pragma once

#include "edgelink/device_manager.h"
#include "edgelink/plugin.h"

#include <modbus.h>

#include <string>
#include <vector>

namespace edgelink
{

// Modbus寄存器数据的解析类型
enum class ModbusDataType
{
    UInt16,
    Int16,
    UInt32,
    Int32,
    Float32
};

// 一个Tag对应一段Modbus保持寄存器
struct ModbusTagConfig
{
    std::string name = "holding-register-0";
    int registerAddress = 0;
    ModbusDataType dataType = ModbusDataType::UInt16;
    double scale = 1.0;
    double offset = 0.0;
};

// Modbus TCP连接与多Tag采集配置
struct ModbusTcpConfig
{
    std::string host = "127.0.0.1";
    int port = 1502;
    int slaveId = 1;

    std::string deviceId = "modbus-device-01";
    std::string deviceName = "Modbus TCP Device";
    std::vector<ModbusTagConfig> tags{ModbusTagConfig{}};
};

class ModbusPlugin : public Plugin
{
public:
    /**
     * @brief 创建Modbus TCP插件。
     * @param deviceManager 外部设备管理器；传nullptr时使用插件内部实例。
     * @param config Modbus TCP连接与Tag配置。
     */
    explicit ModbusPlugin(
        DeviceManager* deviceManager = nullptr,
        ModbusTcpConfig config = {});

    /**
     * @brief 停止连接并释放libmodbus上下文。
     */
    ~ModbusPlugin() override;

    /**
     * @brief 获取插件名称。
     * @return 固定返回"ModbusPlugin"。
     */
    const char* name() const override;

    /**
     * @brief 校验配置、创建Modbus上下文并注册设备。
     * @return 初始化成功返回true，否则返回false。
     */
    bool initialize() override;

    /**
     * @brief 连接Modbus TCP设备并执行首次采集。
     * @return TCP连接成功返回true。
     */
    bool start() override;

    /**
     * @brief 关闭Modbus TCP连接并更新设备状态。
     */
    void stop() override;

    /**
     * @brief 采集配置中的所有Tag并更新到DeviceManager。
     * @return 全部Tag采集成功返回true，任意Tag失败返回false。
     */
    bool pollOnce();

    /**
     * @brief 获取插件当前使用的设备管理器。
     * @return 设备管理器的只读引用。
     */
    const DeviceManager& deviceManager() const;

private:
    ModbusTcpConfig config_;
    DeviceManager ownedDeviceManager_;
    DeviceManager* deviceManager_;

    modbus_t* context_ = nullptr;
    bool connected_ = false;
};

} // namespace edgelink
