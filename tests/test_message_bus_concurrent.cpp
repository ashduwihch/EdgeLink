#include "edgelink/message_bus.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

/**
 * @brief 使用多个发布线程验证MessageBus不会丢失消息或发生数据竞争
 * @return 并发消息数量正确返回0，否则返回1
 */
int main()
{
    // 4个发布线程共发送20000条消息，每条消息分发给3个订阅者
    constexpr int publisherCount = 4;
    constexpr int messagesPerPublisher = 5000;
    constexpr int subscriberCount = 3;
    constexpr int expectedMessages = publisherCount * messagesPerPublisher;

    edgelink::MessageBus bus;

    // 回调会被多个发布线程并发执行，因此计数器必须使用atomic
    std::atomic<int> callbackCount{0};

    for (int i = 0; i < subscriberCount; ++i)
    {
        bus.subscribe(
            "device.tag.updated",
            [&](const edgelink::Message&)
            {
                ++callbackCount;
            });
    }

    std::atomic<int> publishedCount{0};
    std::vector<std::thread> publishers;
    publishers.reserve(publisherCount);

    auto startTime = std::chrono::steady_clock::now();

    for (int publisher = 0; publisher < publisherCount; ++publisher)
    {
        // 每个线程模拟一个独立的数据生产者
        publishers.emplace_back([&, publisher]()
        {
            for (int sequence = 0; sequence < messagesPerPublisher; ++sequence)
            {
                edgelink::Message message;
                message.topic = "device.tag.updated";
                message.deviceId = "device-" + std::to_string(publisher);
                message.tagName = "temperature";
                message.value = static_cast<double>(sequence);
                message.quality = edgelink::Quality::Good;
                message.timestamp = std::chrono::system_clock::now();

                if (bus.publish(message) == subscriberCount)
                {
                    ++publishedCount;
                }
            }
        });
    }

    for (std::thread& publisher : publishers)
    {
        // 等待全部发布线程结束后再核对最终数量
        publisher.join();
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - startTime);

    int expectedCallbacks = expectedMessages * subscriberCount;

    if (publishedCount != expectedMessages ||
        callbackCount != expectedCallbacks)
    {
        std::cerr << "[FAIL] Concurrent MessageBus lost messages" << std::endl;
        std::cerr << "Published: " << publishedCount
                  << "/" << expectedMessages << std::endl;
        std::cerr << "Callbacks: " << callbackCount
                  << "/" << expectedCallbacks << std::endl;
        return 1;
    }

    double seconds = elapsed.count() / 1000000.0;

    // 此吞吐仅用于基础回归观察，不作为正式性能Benchmark
    double throughput = seconds > 0.0
        ? expectedMessages / seconds
        : static_cast<double>(expectedMessages);

    std::cout << "[PASS] Concurrent MessageBus delivered "
              << expectedMessages << " messages to "
              << subscriberCount << " subscribers" << std::endl;
    std::cout << "Elapsed: " << elapsed.count() << " us, throughput: "
              << static_cast<std::size_t>(throughput)
              << " messages/s" << std::endl;

    return 0;
}
