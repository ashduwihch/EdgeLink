#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>

namespace edgelink
{

class EventLoop;
class Timer;

/**
 * @brief 在同一个EventLoop上管理多个不同周期的设备采集任务。
 *
 * DeviceScheduler只负责“什么时候执行”，具体的Modbus、CAN等
 * 采集逻辑由注册进来的回调函数完成。
 */
class DeviceScheduler
{
public:
    using TaskId = std::uint64_t;
    using PollCallback = std::function<void()>;

    /**
     * @brief 创建设备轮询调度器。
     * @param loop 用来接收timerfd事件的EventLoop，不能为nullptr。
     */
    explicit DeviceScheduler(EventLoop* loop);

    /**
     * @brief 停止所有任务并释放它们的Timer。
     */
    ~DeviceScheduler();

    DeviceScheduler(const DeviceScheduler&) = delete;
    DeviceScheduler& operator=(const DeviceScheduler&) = delete;

    /**
     * @brief 添加一个周期采集任务。
     * @param intervalMs 调度周期，单位毫秒，必须大于0。
     * @param callback 每次到期时执行的采集回调。
     * @return 成功返回非0任务ID，参数非法或Timer创建失败返回0。
     */
    TaskId addTask(std::uint64_t intervalMs, PollCallback callback);

    /**
     * @brief 删除指定轮询任务。
     * @param taskId addTask返回的任务ID。
     * @return 找到并删除任务返回true，任务不存在返回false。
     */
    bool removeTask(TaskId taskId);

    /**
     * @brief 启动当前已注册的所有周期任务。
     * @return 全部任务启动成功返回true。
     */
    bool start();

    /**
     * @brief 停止所有周期任务，但保留任务配置以便再次启动。
     */
    void stop();

    /**
     * @brief 查询调度器是否已启动。
     * @return 已启动返回true。
     */
    bool isRunning() const;

    /**
     * @brief 获取已注册任务数量。
     * @return 当前任务数量。
     */
    std::size_t taskCount() const;

private:
    struct Task
    {
        std::uint64_t intervalMs = 0;
        PollCallback callback;
        std::unique_ptr<Timer> timer;
    };

    /**
     * @brief 启动单个任务内部的周期Timer。
     * @param task 待启动的任务。
     * @return Timer启动成功返回true。
     */
    bool startTask(Task& task);

    EventLoop* loop_;
    std::unordered_map<TaskId, Task> tasks_;
    TaskId nextTaskId_ = 1;
    bool running_ = false;
};

} // namespace edgelink
