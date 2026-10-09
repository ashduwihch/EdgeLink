#include "edgelink/message_bus.h"

#include "edgelink/logger.h"

#include <algorithm>
#include <exception>
#include <mutex>
#include <utility>

namespace edgelink
{

/**
 * @brief 订阅指定消息主题，并为本次订阅分配唯一ID
 * @param topic 需要接收的消息主题
 * @param callback 收到消息时执行的回调函数
 * @return 成功时返回非0订阅ID，参数无效时返回0
 */
MessageBus::SubscriptionId MessageBus::subscribe(
    const std::string& topic,
    MessageCallback callback)
{
    if (topic.empty() || !callback)
    {
        Logger::error("MessageBus topic or callback is empty");
        return InvalidSubscriptionId;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // 0被保留为失败标志，因此有效订阅ID从1开始递增
    SubscriptionId subscriptionId = nextSubscriptionId_++;

    // 同时维护topic索引和ID反向索引：前者用于发布，后者用于取消订阅
    subscribers_[topic].push_back(
        Subscription{subscriptionId, std::move(callback)});
    subscriptionTopics_[subscriptionId] = topic;

    return subscriptionId;
}

/**
 * @brief 根据订阅ID删除对应的回调和反向索引
 * @param subscriptionId 需要取消的订阅ID
 * @return 成功删除返回true，订阅不存在返回false
 */
bool MessageBus::unsubscribe(SubscriptionId subscriptionId)
{
    std::lock_guard<std::mutex> lock(mutex_);

    // 先通过反向索引找到该订阅所属的topic
    auto topicIt = subscriptionTopics_.find(subscriptionId);

    if (topicIt == subscriptionTopics_.end())
    {
        return false;
    }

    auto subscribersIt = subscribers_.find(topicIt->second);

    if (subscribersIt == subscribers_.end())
    {
        subscriptionTopics_.erase(topicIt);
        return false;
    }

    std::vector<Subscription>& subscriptions = subscribersIt->second;

    // remove_if把目标订阅移动到vector尾部，再由erase真正删除
    auto newEnd = std::remove_if(
        subscriptions.begin(),
        subscriptions.end(),
        [subscriptionId](const Subscription& subscription)
        {
            return subscription.id == subscriptionId;
        });

    bool removed = newEnd != subscriptions.end();
    subscriptions.erase(newEnd, subscriptions.end());

    if (subscriptions.empty())
    {
        // topic已经没有订阅者时一并删除空列表
        subscribers_.erase(subscribersIt);
    }

    subscriptionTopics_.erase(topicIt);
    return removed;
}

/**
 * @brief 同步分发消息，并隔离单个订阅回调抛出的异常
 * @param message 包含topic和设备Tag数据的消息
 * @return 成功执行的回调数量
 */
std::size_t MessageBus::publish(const Message& message)
{
    if (message.topic.empty())
    {
        Logger::error("MessageBus message topic is empty");
        return 0;
    }

    std::vector<Subscription> subscriptions;

    {
        // mutex只保护内部订阅表，不在执行用户回调期间持锁
        std::lock_guard<std::mutex> lock(mutex_);
        auto subscribersIt = subscribers_.find(message.topic);

        if (subscribersIt == subscribers_.end())
        {
            return 0;
        }

        // 锁内复制快照，使回调可以安全地订阅或取消订阅
        subscriptions = subscribersIt->second;
    }

    std::size_t deliveredCount = 0;

    for (const Subscription& subscription : subscriptions)
    {
        try
        {
            subscription.callback(message);
            ++deliveredCount;
        }
        catch (const std::exception& e)
        {
            // 单个订阅者失败不能阻止后续订阅者接收消息
            Logger::error(
                "MessageBus subscriber threw exception: " +
                std::string(e.what()));
        }
        catch (...)
        {
            Logger::error("MessageBus subscriber threw unknown exception");
        }
    }

    return deliveredCount;
}

/**
 * @brief 获取指定topic的订阅者数量
 * @param topic 需要查询的消息主题
 * @return 订阅者数量，topic不存在时返回0
 */
std::size_t MessageBus::subscriberCount(const std::string& topic) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto subscribersIt = subscribers_.find(topic);

    if (subscribersIt == subscribers_.end())
    {
        return 0;
    }

    return subscribersIt->second.size();
}

/**
 * @brief 清空topic索引和订阅ID反向索引
 */
void MessageBus::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);

    // 两张索引表必须同时清空，保持内部状态一致
    subscribers_.clear();
    subscriptionTopics_.clear();
}

} // namespace edgelink
