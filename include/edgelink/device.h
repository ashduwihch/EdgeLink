#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>

namespace edgelink
{

// 数据质量状态
enum class Quality
{
    Good,
    Stale,
    Timeout,
    Invalid,
    Disconnected
};

// 设备连接状态
enum class ConnectionState
{
    Disconnected,
    Connecting,
    Connected,
    Error
};

// Tag统一值类型
using TagValue = std::variant<
    std::monostate,
    bool,
    std::int8_t, std::int16_t, std::int32_t, std::int64_t,
    std::uint8_t, std::uint16_t, std::uint32_t, std::uint64_t,
    float, double,
    std::string>;

// Tag：设备中的一个数据点
struct Tag
{
    std::string name;
    TagValue value;
    Quality quality = Quality::Disconnected;
    std::chrono::system_clock::time_point timestamp{};

    // 判断当前Tag数据是否有效
    bool isValid() const
    {
        return quality == Quality::Good &&
               !std::holds_alternative<std::monostate>(value);
    }
};

// Device：一个工业设备
struct Device
{
    std::string id;
    std::string name;
    std::string protocol;

    ConnectionState state = ConnectionState::Disconnected;

    // Tag名称 -> Tag
    std::unordered_map<std::string, Tag> tags;

    // 查找Tag
    Tag* findTag(const std::string& tagName)
    {
        auto it = tags.find(tagName);
        return it != tags.end() ? &it->second : nullptr;
    }

    // const对象使用的查找接口
    const Tag* findTag(const std::string& tagName) const
    {
        auto it = tags.find(tagName);
        return it != tags.end() ? &it->second : nullptr;
    }

    // 添加或更新Tag
    void updateTag(Tag tag)
    {
        tag.timestamp = std::chrono::system_clock::now();
        tags[tag.name] = std::move(tag);
    }

    // 判断设备是否在线
    bool isOnline() const
    {
        return state == ConnectionState::Connected;
    }
};

// Quality转字符串
inline const char* qualityToString(Quality quality)
{
    switch (quality)
    {
        case Quality::Good:
            return "GOOD";

        case Quality::Stale:
            return "STALE";

        case Quality::Timeout:
            return "TIMEOUT";

        case Quality::Invalid:
            return "INVALID";

        case Quality::Disconnected:
            return "DISCONNECTED";
    }

    return "UNKNOWN";
}

// ConnectionState转字符串
inline const char* connectionStateToString(ConnectionState state)
{
    switch (state)
    {
        case ConnectionState::Disconnected:
            return "DISCONNECTED";

        case ConnectionState::Connecting:
            return "CONNECTING";

        case ConnectionState::Connected:
            return "CONNECTED";

        case ConnectionState::Error:
            return "ERROR";
    }

    return "UNKNOWN";
}

} // namespace edgelink