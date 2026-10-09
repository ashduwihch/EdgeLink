#pragma once

#include "edgelink/device.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace edgelink
{

// Message：模块之间传递的统一Tag更新消息
struct Message
{
    // topic用于消息路由，例如device.tag.updated
    std::string topic;

    // 以下字段描述一次设备Tag更新
    std::string deviceId;
    std::string tagName;
    TagValue value;
    Quality quality = Quality::Invalid;
    std::chrono::system_clock::time_point timestamp{};
};

// MessageBus：线程安全的进程内发布/订阅总线
class MessageBus
{
public:
    // 每次成功订阅都会得到唯一ID，后续通过该ID取消订阅
    using SubscriptionId = std::uint64_t;

    // 所有订阅回调统一接收只读Message引用
    using MessageCallback = std::function<void(const Message&)>;

    static constexpr SubscriptionId InvalidSubscriptionId = 0;

    /**
     * @brief 订阅指定主题
     * @param topic 需要接收的消息主题
     * @param callback 收到消息时执行的回调函数
     * @return 成功时返回非0订阅ID，失败时返回InvalidSubscriptionId
     */
    SubscriptionId subscribe(
        const std::string& topic,
        MessageCallback callback);

    /**
     * @brief 通过订阅ID取消订阅
     * @param subscriptionId subscribe返回的订阅ID
     * @return 找到并删除订阅时返回true，否则返回false
     */
    bool unsubscribe(SubscriptionId subscriptionId);

    /**
     * @brief 将消息同步发布给相同topic的全部订阅者
     * @param message 需要发布的消息
     * @return 成功执行的订阅回调数量
     */
    std::size_t publish(const Message& message);

    /**
     * @brief 查询指定主题当前拥有的订阅者数量
     * @param topic 需要查询的消息主题
     * @return 该主题的订阅者数量
     */
    std::size_t subscriberCount(const std::string& topic) const;

    /**
     * @brief 删除MessageBus中的全部订阅关系
     */
    void clear();

private:
    struct Subscription
    {
        // ID用于精确区分同一个topic下的不同订阅者
        SubscriptionId id;
        MessageCallback callback;
    };

    SubscriptionId nextSubscriptionId_ = 1;

    // 保护订阅表和订阅ID分配
    mutable std::mutex mutex_;

    // topic -> 订阅列表
    std::unordered_map<std::string, std::vector<Subscription>> subscribers_;

    // subscriptionId -> topic，用于快速取消订阅
    std::unordered_map<SubscriptionId, std::string> subscriptionTopics_;
};

} // namespace edgelink
