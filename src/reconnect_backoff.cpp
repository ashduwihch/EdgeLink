#include "edgelink/reconnect_backoff.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace edgelink
{

/**
 * @brief 校验退避范围并准备单调时钟。
 */
ReconnectBackoff::ReconnectBackoff(
    std::chrono::milliseconds initialDelay,
    std::chrono::milliseconds maximumDelay,
    NowProvider nowProvider)
    : initialDelay_(initialDelay),
      maximumDelay_(maximumDelay),
      nextDelay_(initialDelay),
      nowProvider_(std::move(nowProvider))
{
    if (initialDelay_.count() <= 0 || maximumDelay_ < initialDelay_)
    {
        throw std::invalid_argument("Invalid reconnect backoff range");
    }

    if (!nowProvider_)
    {
        nowProvider_ = []()
        {
            return Clock::now();
        };
    }
}

/**
 * @brief 以当前延迟安排重试，再将下次延迟加倍并限制最大值。
 */
void ReconnectBackoff::recordFailure()
{
    retryAt_ = nowProvider_() + nextDelay_;
    ++failureCount_;

    if (nextDelay_ >= maximumDelay_)
    {
        nextDelay_ = maximumDelay_;
        return;
    }

    const auto doubledDelay = nextDelay_ * 2;
    nextDelay_ = std::min(doubledDelay, maximumDelay_);
}

/**
 * @brief 清除退避状态，使下次失败重新从初始延迟开始。
 */
void ReconnectBackoff::reset()
{
    failureCount_ = 0;
    nextDelay_ = initialDelay_;
    retryAt_ = Clock::time_point{};
}

/**
 * @brief 比较单调时钟和已安排的重试时间。
 */
bool ReconnectBackoff::canRetry() const
{
    return failureCount_ == 0 || nowProvider_() >= retryAt_;
}

/**
 * @brief 返回连续失败计数。
 */
std::size_t ReconnectBackoff::failureCount() const
{
    return failureCount_;
}

/**
 * @brief 计算并截断为非负的剩余退避时间。
 */
std::chrono::milliseconds ReconnectBackoff::remainingDelay() const
{
    if (failureCount_ == 0)
    {
        return std::chrono::milliseconds(0);
    }

    const Clock::time_point now = nowProvider_();

    if (now >= retryAt_)
    {
        return std::chrono::milliseconds(0);
    }

    return std::chrono::duration_cast<std::chrono::milliseconds>(
        retryAt_ - now);
}

} // namespace edgelink
