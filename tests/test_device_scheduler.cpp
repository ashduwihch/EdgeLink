#include "edgelink/device_scheduler.h"

#include "edgelink/event_loop.h"
#include "edgelink/timer.h"

#include <cstdint>
#include <iostream>

/**
 * @brief 验证100/500/1000ms三种周期任务可在同一EventLoop中同时运行。
 * @return 调度次数和任务管理接口均正确返回0，否则返回1。
 */
int main()
{
    edgelink::EventLoop eventLoop;
    edgelink::DeviceScheduler scheduler(&eventLoop);

    std::uint32_t fastCount = 0;
    std::uint32_t mediumCount = 0;
    std::uint32_t slowCount = 0;
    std::uint32_t removedCount = 0;

    const edgelink::DeviceScheduler::TaskId fastTask =
        scheduler.addTask(100, [&fastCount]()
        {
            ++fastCount;
        });
    const edgelink::DeviceScheduler::TaskId mediumTask =
        scheduler.addTask(500, [&mediumCount]()
        {
            ++mediumCount;
        });
    const edgelink::DeviceScheduler::TaskId slowTask =
        scheduler.addTask(1000, [&slowCount]()
        {
            ++slowCount;
        });
    const edgelink::DeviceScheduler::TaskId removedTask =
        scheduler.addTask(100, [&removedCount]()
        {
            ++removedCount;
        });

    const bool taskRegistrationPassed =
        fastTask != 0 &&
        mediumTask != 0 &&
        slowTask != 0 &&
        removedTask != 0 &&
        scheduler.taskCount() == 4 &&
        scheduler.removeTask(removedTask) &&
        !scheduler.removeTask(removedTask) &&
        scheduler.taskCount() == 3 &&
        scheduler.addTask(0, []() {}) == 0;

    edgelink::Timer stopTimer(&eventLoop);
    bool stopTimerStarted = stopTimer.startOnce(1250, [&eventLoop]()
    {
        eventLoop.stop();
    });

    bool schedulerStarted = scheduler.start();
    eventLoop.run();
    scheduler.stop();

    const bool passed =
        taskRegistrationPassed &&
        stopTimerStarted &&
        schedulerStarted &&
        !scheduler.isRunning() &&
        fastCount >= 10 &&
        mediumCount >= 2 &&
        slowCount >= 1 &&
        removedCount == 0;

    if (!passed)
    {
        std::cerr
            << "[FAIL] DeviceScheduler counts: "
            << fastCount << "/"
            << mediumCount << "/"
            << slowCount << std::endl;
        return 1;
    }

    std::cout
        << "[PASS] DeviceScheduler counts: "
        << fastCount << "/"
        << mediumCount << "/"
        << slowCount << std::endl;
    return 0;
}
