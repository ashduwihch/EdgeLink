#include "edgelink/device_manager.h"

#include "edgelink/logger.h"

#include <utility>

namespace edgelink
{

    // 添加设备
    bool DeviceManager::addDevice(Device device)
    {
        if (device.id.empty())
        {
            Logger::error("Device ID is empty");
            return false;
        }

        if (devices_.find(device.id) != devices_.end())
        {
            Logger::error("Device already exists: " + device.id);
            return false;
        }

        std::string deviceId = device.id;

        devices_.emplace(deviceId, std::move(device));

        Logger::info("Device added: " + deviceId);

        return true;
    }


    // 删除设备
    bool DeviceManager::removeDevice(const std::string& deviceId)
    {
        auto it = devices_.find(deviceId);

        if (it == devices_.end())
        {
            Logger::error("Device not found: " + deviceId);
            return false;
        }

        devices_.erase(it);

        Logger::info("Device removed: " + deviceId);

        return true;
    }


    // 查找设备
    Device* DeviceManager::findDevice(const std::string& deviceId)
    {
        auto it = devices_.find(deviceId);

        if (it == devices_.end())
        {
            return nullptr;
        }

        return &it->second;
    }


    // const版本的设备查找
    const Device* DeviceManager::findDevice(const std::string& deviceId) const
    {
        auto it = devices_.find(deviceId);

        if (it == devices_.end())
        {
            return nullptr;
        }

        return &it->second;
    }


    // 判断设备是否存在
    bool DeviceManager::hasDevice(const std::string& deviceId) const
    {
        return devices_.find(deviceId) != devices_.end();
    }


    // 获取设备数量
    std::size_t DeviceManager::deviceCount() const
    {
        return devices_.size();
    }


    // 获取所有设备ID
    std::vector<std::string> DeviceManager::deviceIds() const
    {
        std::vector<std::string> ids;

        ids.reserve(devices_.size());

        for (const auto& device : devices_)
        {
            ids.push_back(device.first);
        }

        return ids;
    }


    // 更新设备连接状态
    bool DeviceManager::updateConnectionState(const std::string& deviceId, ConnectionState state)
    {
        Device* device = findDevice(deviceId);

        if (device == nullptr)
        {
            Logger::error("Device not found: " + deviceId);
            return false;
        }

        device->state = state;

        Logger::info(
            "Device state updated: " +
            deviceId +
            " -> " +
            connectionStateToString(state));

        return true;
    }


    // 添加或更新Tag
    bool DeviceManager::updateTag(const std::string& deviceId, Tag tag)
    {
        Device* device = findDevice(deviceId);

        if (device == nullptr)
        {
            Logger::error("Device not found: " + deviceId);
            return false;
        }

        if (tag.name.empty())
        {
            Logger::error("Tag name is empty");
            return false;
        }

        std::string tagName = tag.name;

        device->updateTag(std::move(tag));

        Logger::info(
            "Tag updated: " +
            deviceId +
            "/" +
            tagName);

        return true;
    }


    // 删除所有设备
    void DeviceManager::clear()
    {
        devices_.clear();

        Logger::info("All devices cleared");
    }

} // namespace edgelink