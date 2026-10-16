#include "edgelink/reconnect_backoff.h"

#include <chrono>
#include <iostream>

namespace
{

/**
 * @brief 检查退避测试条件并输出失败信息。
 * @param condition 待检查条件。
 * @param message 失败原因。
 * @return 条件成立返回true。
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
 * @brief 验证100/200/400/800ms指数退避、最大值限制和成功重置。
 * @return 所有退避断言通过返回0。
 */
int main()
{
    using Backoff = edgelink::ReconnectBackoff;
    Backoff::Clock::time_point now{};
    Backoff backoff(
        std::chrono::milliseconds(100),
        std::chrono::milliseconds(800),
        [&now]()
        {
            return now;
        });

    if (!check(backoff.canRetry(), "Initial retry should be allowed"))
    {
        return 1;
    }

    const int expectedDelays[] = {100, 200, 400, 800, 800};

    for (int expectedDelay : expectedDelays)
    {
        backoff.recordFailure();

        if (!check(!backoff.canRetry(), "Retry was allowed before delay") ||
            !check(
                backoff.remainingDelay() ==
                    std::chrono::milliseconds(expectedDelay),
                "Unexpected reconnect delay"))
        {
            return 1;
        }

        now += std::chrono::milliseconds(expectedDelay - 1);

        if (!check(!backoff.canRetry(), "Retry was allowed one millisecond early"))
        {
            return 1;
        }

        now += std::chrono::milliseconds(1);

        if (!check(backoff.canRetry(), "Retry was not allowed after delay"))
        {
            return 1;
        }
    }

    if (!check(backoff.failureCount() == 5, "Failure count is incorrect"))
    {
        return 1;
    }

    backoff.reset();

    if (!check(backoff.failureCount() == 0, "Reset did not clear failures") ||
        !check(backoff.canRetry(), "Reset did not allow immediate retry"))
    {
        return 1;
    }

    std::cout << "[PASS] Reconnect backoff test finished" << std::endl;
    return 0;
}
