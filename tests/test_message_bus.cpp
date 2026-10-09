#include "edgelink/message_bus.h"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>

namespace
{

/**
 * @brief 检查测试条件，失败时输出对应错误信息
 * @param condition 需要验证的条件
 * @param message 条件失败时输出的说明
 * @return 条件成立返回true，否则返回false
 */
bool check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "[FAIL] " << message << std::endl;
        return false;
    }

    return true;
}

} // namespace

/**
 * @brief 验证MessageBus基础订阅、发布、取消和异常隔离行为
 * @return 全部检查通过返回0，任一检查失败返回1
 */
int main()
{
    edgelink::MessageBus bus;

    // 第一部分：验证同topic多订阅者和不同topic隔离
    int firstSubscriberCalls = 0;
    int secondSubscriberCalls = 0;
    int stateSubscriberCalls = 0;
    double receivedTemperature = 0.0;

    auto firstId = bus.subscribe(
        "device.tag.updated",
        [&](const edgelink::Message& message)
        {
            ++firstSubscriberCalls;
            receivedTemperature = std::get<double>(message.value);
        });

    auto secondId = bus.subscribe(
        "device.tag.updated",
        [&](const edgelink::Message&)
        {
            ++secondSubscriberCalls;
        });

    bus.subscribe(
        "device.state.changed",
        [&](const edgelink::Message&)
        {
            ++stateSubscriberCalls;
        });

    edgelink::Message tagMessage;
    tagMessage.topic = "device.tag.updated";
    tagMessage.deviceId = "temperature-sensor-01";
    tagMessage.tagName = "temperature";
    tagMessage.value = 25.3;
    tagMessage.quality = edgelink::Quality::Good;
    tagMessage.timestamp = std::chrono::system_clock::now();

    if (!check(firstId != edgelink::MessageBus::InvalidSubscriptionId, "First subscription failed") ||
        !check(secondId != edgelink::MessageBus::InvalidSubscriptionId, "Second subscription failed") ||
        !check(bus.subscriberCount("device.tag.updated") == 2, "Subscriber count should be two") ||
        !check(bus.publish(tagMessage) == 2, "Message should reach two subscribers") ||
        !check(firstSubscriberCalls == 1, "First subscriber did not receive message") ||
        !check(secondSubscriberCalls == 1, "Second subscriber did not receive message") ||
        !check(stateSubscriberCalls == 0, "Different topic received the message") ||
        !check(receivedTemperature == 25.3, "Message value is incorrect"))
    {
        return 1;
    }

    // 第二部分：取消一个订阅后，剩余订阅者仍应收到消息
    if (!check(bus.unsubscribe(firstId), "Failed to unsubscribe first subscriber") ||
        !check(!bus.unsubscribe(firstId), "Repeated unsubscribe should fail") ||
        !check(bus.publish(tagMessage) == 1, "Message should reach one subscriber after unsubscribe") ||
        !check(firstSubscriberCalls == 1, "Unsubscribed callback was called again") ||
        !check(secondSubscriberCalls == 2, "Remaining subscriber did not receive message"))
    {
        return 1;
    }

    edgelink::MessageBus::SubscriptionId selfId =
        edgelink::MessageBus::InvalidSubscriptionId;
    int selfSubscriberCalls = 0;

    // 第三部分：回调在执行过程中取消自己，验证发布快照不会失效
    selfId = bus.subscribe(
        "device.tag.updated",
        [&](const edgelink::Message&)
        {
            ++selfSubscriberCalls;
            bus.unsubscribe(selfId);
        });

    bus.publish(tagMessage);
    bus.publish(tagMessage);

    if (!check(selfSubscriberCalls == 1, "Self-unsubscribe callback should run once") ||
        !check(bus.subscriberCount("device.tag.updated") == 1, "Only one tag subscriber should remain"))
    {
        return 1;
    }

    int healthySubscriberCalls = 0;

    // 第四部分：一个回调抛出异常时，后续健康回调仍需执行
    bus.subscribe(
        "fault-test",
        [](const edgelink::Message&)
        {
            throw std::runtime_error("expected test exception");
        });
    bus.subscribe(
        "fault-test",
        [&](const edgelink::Message&)
        {
            ++healthySubscriberCalls;
        });

    edgelink::Message faultMessage;
    faultMessage.topic = "fault-test";

    if (!check(bus.publish(faultMessage) == 1, "Healthy subscriber should run after another throws") ||
        !check(healthySubscriberCalls == 1, "Healthy subscriber was not called") ||
        !check(bus.subscribe("", {}) == edgelink::MessageBus::InvalidSubscriptionId, "Invalid subscription was accepted"))
    {
        return 1;
    }

    bus.clear();

    // 第五部分：clear应删除全部topic和订阅者
    if (!check(bus.subscriberCount("device.tag.updated") == 0, "Clear did not remove subscribers"))
    {
        return 1;
    }

    std::cout << "[PASS] MessageBus test finished" << std::endl;
    return 0;
}
