#include "modbus_plugin.h"

#include "edgelink/logger.h"

#include <array>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <unordered_set>
#include <utility>

namespace
{

/**
 * @brief 根据数据类型计算需要读取的16位寄存器数量。
 * @param dataType Modbus Tag的数据类型。
 * @return 16位类型返回1，32位类型返回2，非法类型返回0。
 */
int registerCount(edgelink::ModbusDataType dataType)
{
    switch (dataType)
    {
        case edgelink::ModbusDataType::UInt16:
        case edgelink::ModbusDataType::Int16:
            return 1;

        case edgelink::ModbusDataType::UInt32:
        case edgelink::ModbusDataType::Int32:
        case edgelink::ModbusDataType::Float32:
            return 2;
    }

    return 0;
}

/**
 * @brief 校验Modbus TCP连接参数和每个Tag的配置。
 * @param config 待校验的配置。
 * @return 配置完整且合法返回true。
 */
bool validateConfig(const edgelink::ModbusTcpConfig& config)
{
    if (config.host.empty() ||
        config.port <= 0 || config.port > 65535 ||
        config.slaveId <= 0 || config.slaveId > 247 ||
        config.deviceId.empty() || config.tags.empty())
    {
        return false;
    }

    std::unordered_set<std::string> tagNames;

    for (const edgelink::ModbusTagConfig& tag : config.tags)
    {
        const int count = registerCount(tag.dataType);

        if (tag.name.empty() ||
            !tagNames.insert(tag.name).second ||
            count == 0 ||
            tag.registerAddress < 0 ||
            tag.registerAddress > 65535 - (count - 1) ||
            !std::isfinite(tag.scale) ||
            !std::isfinite(tag.offset))
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief 将Modbus寄存器按配置类型解析为工程值。
 * @param registers 读取到的寄存器，32位数据按高字在前组合。
 * @param config 当前Tag的数据类型和换算参数。
 * @return 解析成功返回value * scale + offset，数值非法返回空。
 */
std::optional<double> decodeValue(
    const std::array<std::uint16_t, 2>& registers,
    const edgelink::ModbusTagConfig& config)
{
    double rawValue = 0.0;

    switch (config.dataType)
    {
        case edgelink::ModbusDataType::UInt16:
            rawValue = registers[0];
            break;

        case edgelink::ModbusDataType::Int16:
        {
            std::int16_t value = 0;
            std::memcpy(&value, &registers[0], sizeof(value));
            rawValue = value;
            break;
        }

        case edgelink::ModbusDataType::UInt32:
        {
            const std::uint32_t value =
                (static_cast<std::uint32_t>(registers[0]) << 16U) |
                registers[1];
            rawValue = value;
            break;
        }

        case edgelink::ModbusDataType::Int32:
        {
            const std::uint32_t bits =
                (static_cast<std::uint32_t>(registers[0]) << 16U) |
                registers[1];
            std::int32_t value = 0;
            std::memcpy(&value, &bits, sizeof(value));
            rawValue = value;
            break;
        }

        case edgelink::ModbusDataType::Float32:
        {
            const std::uint32_t bits =
                (static_cast<std::uint32_t>(registers[0]) << 16U) |
                registers[1];
            float value = 0.0F;
            std::memcpy(&value, &bits, sizeof(value));
            rawValue = value;
            break;
        }
    }

    const double engineeringValue = rawValue * config.scale + config.offset;

    if (!std::isfinite(rawValue) || !std::isfinite(engineeringValue))
    {
        return std::nullopt;
    }

    return engineeringValue;
}

} // namespace

namespace edgelink
{

/**
 * @brief 创建Modbus TCP插件并保存采集配置。
 */
ModbusPlugin::ModbusPlugin(DeviceManager* deviceManager, ModbusTcpConfig config)
    : config_(std::move(config)),
      deviceManager_(deviceManager != nullptr ? deviceManager : &ownedDeviceManager_)
{
}

/**
 * @brief 停止Modbus连接并释放libmodbus上下文。
 */
ModbusPlugin::~ModbusPlugin()
{
    stop();

    if (context_ != nullptr)
    {
        modbus_free(context_);
        context_ = nullptr;
    }
}

/**
 * @brief 获取用于PluginManager注册的插件名称。
 */
const char* ModbusPlugin::name() const
{
    return "ModbusPlugin";
}

/**
 * @brief 校验配置、创建Modbus TCP上下文并注册设备。
 */
bool ModbusPlugin::initialize()
{
    if (context_ != nullptr)
    {
        return true;
    }

    if (!validateConfig(config_))
    {
        Logger::error("Invalid Modbus TCP or Tag configuration");
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

/**
 * @brief 连接Modbus TCP设备，连接成功后尝试首次多Tag采集。
 */
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

    // 单个Tag采集失败不代表TCP连接失败，保留连接便于下次重试。
    if (!pollOnce())
    {
        Logger::warn("Initial Modbus polling finished with Tag errors");
    }

    Logger::info("ModbusPlugin started");
    return true;
}

/**
 * @brief 关闭Modbus TCP连接并把设备标记为已断开。
 */
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

/**
 * @brief 按Tag配置逐个读取保持寄存器并换算成统一TagValue。
 */
bool ModbusPlugin::pollOnce()
{
    if (!connected_)
    {
        Logger::error("Modbus TCP device is not connected");
        return false;
    }

    bool allSucceeded = true;

    for (const ModbusTagConfig& tagConfig : config_.tags)
    {
        std::array<std::uint16_t, 2> registers{};
        const int expectedCount = registerCount(tagConfig.dataType);
        const int actualCount = modbus_read_registers(
            context_,
            tagConfig.registerAddress,
            expectedCount,
            registers.data());

        Tag tag;
        tag.name = tagConfig.name;

        if (actualCount != expectedCount)
        {
            tag.quality = Quality::Timeout;
            deviceManager_->updateTag(config_.deviceId, std::move(tag));

            Logger::error(
                "Failed to read Modbus Tag " + tagConfig.name + ": " +
                std::string(modbus_strerror(errno)));
            allSucceeded = false;
            continue;
        }

        const std::optional<double> value = decodeValue(registers, tagConfig);

        if (!value.has_value())
        {
            tag.quality = Quality::Invalid;
            deviceManager_->updateTag(config_.deviceId, std::move(tag));
            Logger::error("Invalid Modbus value for Tag: " + tagConfig.name);
            allSucceeded = false;
            continue;
        }

        tag.value = *value;
        tag.quality = Quality::Good;

        if (!deviceManager_->updateTag(config_.deviceId, std::move(tag)))
        {
            allSucceeded = false;
        }
    }

    return allSucceeded;
}

/**
 * @brief 获取插件内部或外部传入的设备管理器。
 */
const DeviceManager& ModbusPlugin::deviceManager() const
{
    return *deviceManager_;
}

} // namespace edgelink

/**
 * @brief 动态库导出工厂函数，供PluginManager创建Modbus插件。
 * @return 新创建的插件对象，由destroyPlugin释放。
 */
extern "C" edgelink::Plugin* createPlugin()
{
    return new edgelink::ModbusPlugin();
}

/**
 * @brief 动态库导出销毁函数，释放PluginManager持有的插件。
 * @param plugin 待释放的插件指针。
 */
extern "C" void destroyPlugin(edgelink::Plugin* plugin)
{
    delete plugin;
}
