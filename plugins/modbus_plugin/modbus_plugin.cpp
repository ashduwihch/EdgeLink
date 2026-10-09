#include "modbus_plugin.h"

#include "edgelink/logger.h"

#include <cerrno>
#include <cstdint>
#include <utility>

namespace edgelink
{

ModbusPlugin::ModbusPlugin(DeviceManager* deviceManager, ModbusTcpConfig config)
    : config_(std::move(config)),
      deviceManager_(deviceManager != nullptr ? deviceManager : &ownedDeviceManager_)
{
}

ModbusPlugin::~ModbusPlugin()
{
    stop();

    if (context_ != nullptr)
    {
        modbus_free(context_);
        context_ = nullptr;
    }
}

const char* ModbusPlugin::name() const
{
    return "ModbusPlugin";
}

bool ModbusPlugin::initialize()
{
    if (context_ != nullptr)
    {
        return true;
    }

    if (config_.host.empty() ||
        config_.port <= 0 || config_.port > 65535 ||
        config_.slaveId <= 0 || config_.slaveId > 247 ||
        config_.registerAddress < 0 ||
        config_.deviceId.empty() || config_.tagName.empty())
    {
        Logger::error("Invalid Modbus TCP configuration");
        return false;
    }

    context_ = modbus_new_tcp(config_.host.c_str(), config_.port);

    if (context_ == nullptr)
    {
        Logger::error("Failed to create Modbus TCP context");
        return false;
    }

    if (modbus_set_slave(context_, config_.slaveId) < 0)
    {
        Logger::error("Failed to set Modbus slave ID");
        modbus_free(context_);
        context_ = nullptr;
        return false;
    }

    if (!deviceManager_->hasDevice(config_.deviceId))
    {
        Device device;
        device.id = config_.deviceId;
        device.name = config_.deviceName;
        device.protocol = "modbus-tcp";

        if (!deviceManager_->addDevice(std::move(device)))
        {
            modbus_free(context_);
            context_ = nullptr;
            return false;
        }
    }

    Logger::info("ModbusPlugin initialized");
    return true;
}

bool ModbusPlugin::start()
{
    if (connected_)
    {
        return true;
    }

    if (context_ == nullptr && !initialize())
    {
        return false;
    }

    deviceManager_->updateConnectionState(
        config_.deviceId,
        ConnectionState::Connecting);

    if (modbus_connect(context_) < 0)
    {
        Logger::error(
            "Failed to connect Modbus TCP device: " +
            std::string(modbus_strerror(errno)));

        deviceManager_->updateConnectionState(
            config_.deviceId,
            ConnectionState::Error);
        return false;
    }

    connected_ = true;

    deviceManager_->updateConnectionState(
        config_.deviceId,
        ConnectionState::Connected);

    if (!pollOnce())
    {
        stop();
        return false;
    }

    Logger::info("ModbusPlugin started");
    return true;
}

void ModbusPlugin::stop()
{
    if (connected_)
    {
        modbus_close(context_);
        connected_ = false;
    }

    if (deviceManager_->hasDevice(config_.deviceId))
    {
        deviceManager_->updateConnectionState(
            config_.deviceId,
            ConnectionState::Disconnected);
    }
}

bool ModbusPlugin::pollOnce()
{
    if (!connected_)
    {
        Logger::error("Modbus TCP device is not connected");
        return false;
    }

    std::uint16_t registerValue = 0;

    int count = modbus_read_registers(
        context_,
        config_.registerAddress,
        1,
        &registerValue);

    if (count != 1)
    {
        Tag tag;
        tag.name = config_.tagName;
        tag.quality = Quality::Timeout;
        deviceManager_->updateTag(config_.deviceId, std::move(tag));

        Logger::error(
            "Failed to read Modbus register: " +
            std::string(modbus_strerror(errno)));
        return false;
    }

    Tag tag;
    tag.name = config_.tagName;
    tag.value = static_cast<double>(registerValue) * config_.scale;
    tag.quality = Quality::Good;

    return deviceManager_->updateTag(
        config_.deviceId,
        std::move(tag));
}

const DeviceManager& ModbusPlugin::deviceManager() const
{
    return *deviceManager_;
}

} // namespace edgelink

extern "C" edgelink::Plugin* createPlugin()
{
    return new edgelink::ModbusPlugin();
}

extern "C" void destroyPlugin(edgelink::Plugin* plugin)
{
    delete plugin;
}
