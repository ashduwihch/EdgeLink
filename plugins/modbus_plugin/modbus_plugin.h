#pragma once

#include "edgelink/device_manager.h"
#include "edgelink/plugin.h"

#include <modbus.h>

#include <string>

namespace edgelink
{

// 第一版Modbus TCP连接和单寄存器采集配置
struct ModbusTcpConfig
{
    std::string host = "127.0.0.1";
    int port = 1502;
    int slaveId = 1;
    int registerAddress = 0;
    double scale = 1.0;

    std::string deviceId = "modbus-device-01";
    std::string deviceName = "Modbus TCP Device";
    std::string tagName = "holding-register-0";
};

class ModbusPlugin : public Plugin
{
public:
    explicit ModbusPlugin(
        DeviceManager* deviceManager = nullptr,
        ModbusTcpConfig config = {});

    ~ModbusPlugin() override;

    const char* name() const override;
    bool initialize() override;
    bool start() override;
    void stop() override;

    // 读取一个保持寄存器并更新到DeviceManager
    bool pollOnce();

    const DeviceManager& deviceManager() const;

private:
    ModbusTcpConfig config_;
    DeviceManager ownedDeviceManager_;
    DeviceManager* deviceManager_;

    modbus_t* context_ = nullptr;
    bool connected_ = false;
};

} // namespace edgelink
