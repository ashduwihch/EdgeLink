#pragma once

#include "edgelink/device.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace edgelink
{
    // DeviceManager：统一管理所有设备
    class DeviceManager
    {
    public:
        // 添加设备，设备ID不能重复
        bool addDevice(Device device);

        // 删除设备
        bool removeDevice(const std::string& deviceId);

        // 查找设备
        Device* findDevice(const std::string& deviceId);

        // const版本的设备查找
        const Device* findDevice(const std::string& deviceId) const;

        // 判断设备是否存在
        bool hasDevice(const std::string& deviceId) const;

        // 获取设备数量
        std::size_t deviceCount() const;

        // 获取所有设备ID
        std::vector<std::string> deviceIds() const;

        // 更新设备连接状态
        bool updateConnectionState(const std::string& deviceId, ConnectionState state);

        // 添加或更新设备中的Tag
        bool updateTag(const std::string& deviceId, Tag tag);

        // 删除所有设备
        void clear();

    private:
        // 设备ID -> Device
        std::unordered_map<std::string, Device> devices_;
    };

} // namespace edgelink