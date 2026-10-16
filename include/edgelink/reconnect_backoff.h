#pragma once

#include <chrono>
#include <cstddef>
#include <functional>

namespace edgelink
{

/**
 * @brief 用指数增长的等待时间限制设备重连频率。
 */
class ReconnectBackoff
{
public:
    using Clock = std::chrono::steady_clock;
    using NowProvider = std::function<Clock::time_point()>;

    /**
     * @brief 创建指数退避计时器。
     * @param initialDelay 第一次失败后的等待时间。
     * @param maximumDelay 连续失败时允许增长到的最大等待时间。
     * @param nowProvider 提供单调时间的回调，留空时使用steady_clock。
     */
    ReconnectBackoff(
        std::chrono::milliseconds initialDelay,
        std::chrono::milliseconds maximumDelay,
        NowProvider nowProvider = {});

    /**
     * @brief 记录一次连接失败，安排下次可重试时间并将后续延迟加倍。
     */
    void recordFailure();

    /**
     * @brief 在连接成功后清空失败次数并恢复初始延迟。
     */
    void reset();

    /**
     * @brief 检查当前时间是否已允许再次尝试连接。
     * @return 未发生失败或等待时间已到返回true。
     */
    bool canRetry() const;

    /**
     * @brief 获取当前连续失败次数。
     * @return 自最近一次成功后累计的失败次数。
     */
    std::size_t failureCount() const;

    /**
     * @brief 获取距离下次可重试的剩余时间。
     * @return 可立即重试时返回0毫秒。
     */
    std::chrono::milliseconds remainingDelay() const;

private:
    std::chrono::milliseconds initialDelay_;
    std::chrono::milliseconds maximumDelay_;
    std::chrono::milliseconds nextDelay_;
    NowProvider nowProvider_;
    Clock::time_point retryAt_{};
    std::size_t failureCount_ = 0;
};

} // namespace edgelink
