#include "edgelink/device_scheduler.h"

#include "edgelink/event_loop.h"
#include "edgelink/logger.h"
#include "edgelink/timer.h"

#include <stdexcept>
#include <utility>

namespace edgelink
{

/**
 * @brief 保存EventLoop，后续每个调度任务都将注册到该事件循环。
 */
DeviceScheduler::DeviceScheduler(EventLoop* loop)
    : loop_(loop)
{
    if (loop_ == nullptr)
    {
        throw std::invalid_argument("DeviceScheduler requires an EventLoop");
    }
}

/**
 * @brief 停止调度，tasks_销毁时会继续释放所有Timer。
 */
DeviceScheduler::~DeviceScheduler()
{
    stop();
}

/**
 * @brief 创建周期任务，调度器已运行时会立即启动它。
 */
DeviceScheduler::TaskId DeviceScheduler::addTask(
    std::uint64_t intervalMs,
    PollCallback callback)
{
    if (intervalMs == 0 || !callback)
    {
        Logger::error("Invalid device scheduler task");
        return 0;
    }

    Task task;
    task.intervalMs = intervalMs;
    task.callback = std::move(callback);

    try
    {
        task.timer = std::make_unique<Timer>(loop_);
    }
    catch (const std::exception& exception)
    {
        Logger::error(
            "Failed to create scheduler timer: " +
            std::string(exception.what()));
        return 0;
    }

    const TaskId taskId = nextTaskId_++;
    auto result = tasks_.emplace(taskId, std::move(task));

    if (running_ && !startTask(result.first->second))
    {
        tasks_.erase(result.first);
        return 0;
    }

    return taskId;
}

/**
 * @brief 停止并删除指定任务。
 */
bool DeviceScheduler::removeTask(TaskId taskId)
{
    auto task = tasks_.find(taskId);

    if (task == tasks_.end())
    {
        return false;
    }

    task->second.timer->stop();
    tasks_.erase(task);
    return true;
}

/**
 * @brief 遍历并启动所有已注册的轮询任务。
 */
bool DeviceScheduler::start()
{
    if (running_)
    {
        return true;
    }

    for (auto& task : tasks_)
    {
        if (!startTask(task.second))
        {
            for (auto& startedTask : tasks_)
            {
                startedTask.second.timer->stop();
            }

            return false;
        }
    }

    running_ = true;
    Logger::info("DeviceScheduler started");
    return true;
}

/**
 * @brief 取消所有timerfd的周期计时。
 */
void DeviceScheduler::stop()
{
    for (auto& task : tasks_)
    {
        task.second.timer->stop();
    }

    if (running_)
    {
        running_ = false;
        Logger::info("DeviceScheduler stopped");
    }
}

/**
 * @brief 返回调度器当前运行状态。
 */
bool DeviceScheduler::isRunning() const
{
    return running_;
}

/**
 * @brief 返回tasks_容器中的任务数量。
 */
std::size_t DeviceScheduler::taskCount() const
{
    return tasks_.size();
}

/**
 * @brief 将任务回调交给Timer，并按配置的毫秒周期启动。
 */
bool DeviceScheduler::startTask(Task& task)
{
    PollCallback callback = task.callback;

    return task.timer->startPeriodic(
        task.intervalMs,
        [callback = std::move(callback)]()
        {
            callback();
        });
}

} // namespace edgelink
