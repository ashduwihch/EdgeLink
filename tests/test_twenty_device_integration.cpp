#include "edgelink/device_connection_state_machine.h"
#include "edgelink/device_manager.h"
#include "edgelink/device_scheduler.h"
#include "edgelink/event_loop.h"
#include "edgelink/logger.h"
#include "edgelink/timer.h"

#include <chrono>
#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{

constexpr std::size_t deviceCount = 20;
constexpr std::size_t faultyDeviceIndex = 7;

/**
 * @brief 保存集成测试中一台模拟设备的运行状态。
 */
struct SimulatedDeviceRuntime
{
    std::string deviceId;
    std::unique_ptr<edgelink::DeviceConnectionStateMachine> stateMachine;
    std::size_t pollCount = 0;
    bool faulty = false;
};

} // namespace

/**
 * @brief 验证20台设备同时轮询时，单台超时不影响其他设备。
 * @return 19台设备正常且故障设备进入Timeout返回0。
 */
int main()
{
    edgelink::Logger::setLevel(edgelink::LogLevel::Warn);

    edgelink::EventLoop eventLoop;
    edgelink::DeviceManager deviceManager;
    edgelink::DeviceScheduler scheduler(&eventLoop);
    std::vector<SimulatedDeviceRuntime> runtimes;
    runtimes.reserve(deviceCount);

    for (std::size_t index = 0; index < deviceCount; ++index)
    {
        SimulatedDeviceRuntime runtime;
        runtime.deviceId = "integration-device-" + std::to_string(index);
        runtime.faulty = index == faultyDeviceIndex;

        edgelink::Device device;
        device.id = runtime.deviceId;
        device.name = "Integration Device " + std::to_string(index);
        device.protocol = "simulated";

        edgelink::Tag tag;
        tag.name = "value";
        tag.value = 0.0;
        tag.quality = edgelink::Quality::Good;
        device.updateTag(std::move(tag));

        if (!deviceManager.addDevice(std::move(device)))
        {
            std::cerr << "[FAIL] Failed to add integration device" << std::endl;
            return 1;
        }

        runtime.stateMachine =
            std::make_unique<edgelink::DeviceConnectionStateMachine>(
                &deviceManager,
                runtime.deviceId,
                std::chrono::milliseconds(60));

        if (!runtime.stateMachine->transitionTo(
                edgelink::ConnectionState::Connecting) ||
            !runtime.stateMachine->transitionTo(
                edgelink::ConnectionState::Connected))
        {
            std::cerr << "[FAIL] Failed to connect integration device" << std::endl;
            return 1;
        }

        runtimes.push_back(std::move(runtime));
    }

    for (SimulatedDeviceRuntime& runtime : runtimes)
    {
        SimulatedDeviceRuntime* currentRuntime = &runtime;

        if (scheduler.addTask(20, [&deviceManager, currentRuntime]()
            {
                ++currentRuntime->pollCount;

                if (currentRuntime->faulty)
                {
                    if (currentRuntime->stateMachine->checkTimeout())
                    {
                        deviceManager.updateAllTagQualities(
                            currentRuntime->deviceId,
                            edgelink::Quality::Timeout);
                    }

                    return;
                }

                currentRuntime->stateMachine->markActivity();

                edgelink::Tag tag;
                tag.name = "value";
                tag.value = static_cast<double>(currentRuntime->pollCount);
                tag.quality = edgelink::Quality::Good;
                deviceManager.updateTag(
                    currentRuntime->deviceId,
                    std::move(tag));
            }) == 0)
        {
            std::cerr << "[FAIL] Failed to schedule integration device" << std::endl;
            return 1;
        }
    }

    edgelink::Timer stopTimer(&eventLoop);
    const bool stopTimerStarted = stopTimer.startOnce(180, [&eventLoop]()
    {
        eventLoop.stop();
    });

    const bool schedulerStarted = scheduler.start();
    eventLoop.run();
    scheduler.stop();

    bool devicesPassed =
        stopTimerStarted &&
        schedulerStarted &&
        deviceManager.deviceCount() == deviceCount;

    for (std::size_t index = 0; index < runtimes.size(); ++index)
    {
        const SimulatedDeviceRuntime& runtime = runtimes[index];
        const edgelink::Device* device =
            deviceManager.findDevice(runtime.deviceId);
        const edgelink::Tag* tag =
            device != nullptr ? device->findTag("value") : nullptr;

        if (index == faultyDeviceIndex)
        {
            devicesPassed = devicesPassed &&
                device != nullptr &&
                device->state == edgelink::ConnectionState::Timeout &&
                tag != nullptr &&
                tag->quality == edgelink::Quality::Timeout;
        }
        else
        {
            devicesPassed = devicesPassed &&
                device != nullptr &&
                device->state == edgelink::ConnectionState::Connected &&
                tag != nullptr &&
                tag->quality == edgelink::Quality::Good &&
                runtime.pollCount >= 5;
        }
    }

    if (!devicesPassed)
    {
        std::cerr << "[FAIL] Twenty-device isolation test failed" << std::endl;
        return 1;
    }

    std::cout
        << "[PASS] 19 healthy devices continued while 1 device timed out"
        << std::endl;
    return 0;
}
