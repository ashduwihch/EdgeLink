#include "edgelink/event_loop.h"
#include "edgelink/eventfd_notifier.h"
#include "edgelink/message_bus.h"
#include "edgelink/shared_memory_buffer.h"
#include "edgelink/shared_memory_message_bridge.h"
#include "edgelink/timer.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <string>
#include <thread>
#include <unistd.h>
#include <variant>
#include <vector>

namespace
{

using Clock = std::chrono::steady_clock;

constexpr std::size_t MessageBusIterations = 100000;
constexpr std::size_t IpcBridgeIterations = 5000;

struct BenchmarkResult
{
    std::string name;
    std::size_t iterations = 0;
    double throughput = 0.0;
    double averageMicroseconds = 0.0;
    double p50Microseconds = 0.0;
    double p95Microseconds = 0.0;
    double p99Microseconds = 0.0;
    bool succeeded = false;
};

/**
 * @brief 从已经排序的延迟样本中取指定百分位数。
 * @param sortedSamples 按从小到大排列的微秒延迟。
 * @param percentile 0到1之间的百分位比例。
 * @return 对应位置的延迟值，数组为空时返回0。
 */
double percentileValue(
    const std::vector<double>& sortedSamples,
    double percentile)
{
    if (sortedSamples.empty())
    {
        return 0.0;
    }

    const double position = percentile * static_cast<double>(sortedSamples.size() - 1);
    return sortedSamples[static_cast<std::size_t>(position)];
}

/**
 * @brief 根据耗时和延迟样本计算吞吐、平均值及百分位数。
 * @param name Benchmark项目名称。
 * @param samples 每次操作的微秒延迟。
 * @param totalSeconds 全部操作总耗时。
 * @param succeeded 数据正确性检查是否通过。
 * @return 填充完成的Benchmark结果。
 */
BenchmarkResult calculateResult(
    std::string name,
    std::vector<double> samples,
    double totalSeconds,
    bool succeeded)
{
    BenchmarkResult result;
    result.name = std::move(name);
    result.iterations = samples.size();
    result.succeeded = succeeded;

    if (samples.empty() || totalSeconds <= 0.0)
    {
        return result;
    }

    result.throughput = static_cast<double>(samples.size()) / totalSeconds;
    result.averageMicroseconds =
        std::accumulate(samples.begin(), samples.end(), 0.0) /
        static_cast<double>(samples.size());

    std::sort(samples.begin(), samples.end());
    result.p50Microseconds = percentileValue(samples, 0.50);
    result.p95Microseconds = percentileValue(samples, 0.95);
    result.p99Microseconds = percentileValue(samples, 0.99);
    return result;
}

/**
 * @brief 测量单线程MessageBus同步发布到一个订阅者的性能。
 * @return 包含吞吐与延迟分布的Benchmark结果。
 */
BenchmarkResult benchmarkMessageBus()
{
    edgelink::MessageBus messageBus;
    std::size_t received = 0;

    messageBus.subscribe("benchmark.message", [&](const edgelink::Message&)
    {
        ++received;
    });

    edgelink::Message message;
    message.topic = "benchmark.message";
    message.deviceId = "benchmark-device";
    message.tagName = "value";
    message.value = std::uint64_t{0};
    message.quality = edgelink::Quality::Good;

    std::vector<double> samples;
    samples.reserve(MessageBusIterations);
    const auto totalStart = Clock::now();

    for (std::size_t index = 0; index < MessageBusIterations; ++index)
    {
        const auto operationStart = Clock::now();
        messageBus.publish(message);
        const auto operationEnd = Clock::now();
        samples.push_back(
            std::chrono::duration<double, std::micro>(
                operationEnd - operationStart).count());
    }

    const double totalSeconds =
        std::chrono::duration<double>(Clock::now() - totalStart).count();

    return calculateResult(
        "MessageBus publish",
        std::move(samples),
        totalSeconds,
        received == MessageBusIterations);
}

/**
 * @brief 测量共享内存、eventfd、编解码和MessageBus组成的完整链路性能。
 * @return 包含端到端吞吐与延迟分布的Benchmark结果。
 */
BenchmarkResult benchmarkIpcBridge()
{
    const std::string sharedMemoryName =
        "/edgelink-benchmark-" + std::to_string(getpid());

    edgelink::EventLoop eventLoop;
    edgelink::SharedMemoryBuffer sharedMemory(sharedMemoryName);

    if (!sharedMemory.create(4096))
    {
        return BenchmarkResult{"Shared memory IPC bridge"};
    }

    edgelink::EventFdNotifier notifier(&eventLoop);
    edgelink::MessageBus messageBus;
    edgelink::SharedMemoryMessageBridge bridge(
        sharedMemory,
        notifier,
        messageBus);

    std::mutex mutex;
    std::condition_variable condition;
    std::size_t received = 0;
    bool failed = false;
    Clock::time_point sendStart;
    std::vector<double> samples;
    samples.reserve(IpcBridgeIterations);

    messageBus.subscribe("benchmark.ipc", [&](const edgelink::Message& message)
    {
        std::lock_guard<std::mutex> lock(mutex);

        if (!std::holds_alternative<std::uint64_t>(message.value) ||
            std::get<std::uint64_t>(message.value) != received)
        {
            failed = true;
            condition.notify_all();
            eventLoop.stop();
            return;
        }

        samples.push_back(
            std::chrono::duration<double, std::micro>(
                Clock::now() - sendStart).count());
        ++received;
        condition.notify_all();

        if (received == IpcBridgeIterations)
        {
            eventLoop.stop();
        }
    });

    if (!bridge.start())
    {
        sharedMemory.close();
        sharedMemory.remove();
        return BenchmarkResult{"Shared memory IPC bridge"};
    }

    edgelink::Timer timeoutTimer(&eventLoop);
    timeoutTimer.startOnce(30000, [&]()
    {
        std::lock_guard<std::mutex> lock(mutex);
        failed = true;
        condition.notify_all();
        eventLoop.stop();
    });

    edgelink::Message message;
    message.topic = "benchmark.ipc";
    message.deviceId = "benchmark-device";
    message.tagName = "value";
    message.quality = edgelink::Quality::Good;

    const auto totalStart = Clock::now();
    std::thread sender([&]()
    {
        for (std::size_t index = 0; index < IpcBridgeIterations; ++index)
        {
            {
                std::lock_guard<std::mutex> lock(mutex);
                sendStart = Clock::now();
            }

            message.value = static_cast<std::uint64_t>(index);

            if (!edgelink::SharedMemoryMessageBridge::send(
                    message,
                    sharedMemory,
                    notifier))
            {
                std::lock_guard<std::mutex> lock(mutex);
                failed = true;
                condition.notify_all();
                eventLoop.stop();
                return;
            }

            std::unique_lock<std::mutex> lock(mutex);
            condition.wait(lock, [&]()
            {
                return received > index || failed;
            });

            if (failed)
            {
                return;
            }
        }
    });

    eventLoop.run();
    sender.join();
    timeoutTimer.stop();
    const double totalSeconds =
        std::chrono::duration<double>(Clock::now() - totalStart).count();

    bridge.stop();
    sharedMemory.close();
    bool removed = sharedMemory.remove();
    bool succeeded = !failed &&
                     received == IpcBridgeIterations &&
                     samples.size() == IpcBridgeIterations &&
                     removed;

    return calculateResult(
        "Shared memory IPC bridge",
        std::move(samples),
        totalSeconds,
        succeeded);
}

/**
 * @brief 以便于复制到README的表格行格式输出Benchmark结果。
 * @param result 需要输出的性能结果。
 */
void printResult(const BenchmarkResult& result)
{
    std::cout << std::fixed << std::setprecision(3)
              << result.name << " | "
              << result.iterations << " | "
              << result.throughput << " msg/s | "
              << result.averageMicroseconds << " us | "
              << result.p50Microseconds << " us | "
              << result.p95Microseconds << " us | "
              << result.p99Microseconds << " us | "
              << (result.succeeded ? "PASS" : "FAIL")
              << std::endl;
}

}  // namespace

/**
 * @brief 运行Messaging与IPC初步Benchmark并输出吞吐和延迟分布。
 * @return 两项Benchmark均通过数据正确性检查时返回0，否则返回1。
 */
int main()
{
    const BenchmarkResult messageBusResult = benchmarkMessageBus();
    const BenchmarkResult ipcBridgeResult = benchmarkIpcBridge();

    std::cout << "Benchmark | Iterations | Throughput | Average | P50 | P95 | P99 | Result"
              << std::endl;
    printResult(messageBusResult);
    printResult(ipcBridgeResult);

    return messageBusResult.succeeded && ipcBridgeResult.succeeded ? 0 : 1;
}
