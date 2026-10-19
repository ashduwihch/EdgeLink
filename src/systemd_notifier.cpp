#include "edgelink/systemd_notifier.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace edgelink
{
namespace
{

/**
 * @brief 读取无符号整数环境变量
 * @param name 环境变量名称
 * @param value 输出解析后的整数
 * @return 环境变量存在且完整解析成功返回true
 */
bool readUnsignedEnvironment(const char* name, unsigned long long& value)
{
    const char* text = std::getenv(name);

    if (text == nullptr || *text == '\0')
    {
        return false;
    }

    try
    {
        std::size_t parsedLength = 0;
        value = std::stoull(text, &parsedLength);
        return parsedLength == std::strlen(text);
    }
    catch (const std::exception&)
    {
        return false;
    }
}

/**
 * @brief 清除状态文本中的换行符，避免伪造额外systemd字段
 * @param status 原始状态说明
 * @return 可安全拼接到STATUS字段中的单行文本
 */
std::string sanitizeStatus(const std::string& status)
{
    std::string sanitized = status;

    for (char& character : sanitized)
    {
        if (character == '\n' || character == '\r')
        {
            character = ' ';
        }
    }

    return sanitized;
}

} // namespace

/**
 * @brief 缓存NOTIFY_SOCKET，并解析WATCHDOG_USEC和WATCHDOG_PID
 */
SystemdNotifier::SystemdNotifier()
{
    const char* notifySocket = std::getenv("NOTIFY_SOCKET");

    if (notifySocket != nullptr)
    {
        notifySocket_ = notifySocket;
    }

    unsigned long long watchdogPid = 0;

    if (readUnsignedEnvironment("WATCHDOG_PID", watchdogPid) &&
        watchdogPid != static_cast<unsigned long long>(::getpid()))
    {
        return;
    }

    unsigned long long watchdogMicroseconds = 0;

    if (!readUnsignedEnvironment(
            "WATCHDOG_USEC",
            watchdogMicroseconds) ||
        watchdogMicroseconds == 0)
    {
        return;
    }

    unsigned long long watchdogMilliseconds =
        watchdogMicroseconds / 1000;

    if (watchdogMilliseconds == 0 ||
        watchdogMilliseconds > static_cast<unsigned long long>(
                                   std::numeric_limits<long long>::max()))
    {
        return;
    }

    watchdogInterval_ = std::chrono::milliseconds(
        static_cast<long long>(watchdogMilliseconds));
}

/**
 * @brief 查询NOTIFY_SOCKET配置是否存在
 * @return socket路径非空返回true
 */
bool SystemdNotifier::isAvailable() const
{
    return !notifySocket_.empty();
}

/**
 * @brief 返回systemd配置的watchdog完整周期
 * @return watchdog周期，未启用时为0毫秒
 */
std::chrono::milliseconds SystemdNotifier::watchdogInterval() const
{
    return watchdogInterval_;
}

/**
 * @brief 发送READY=1和可选STATUS字段
 * @param status 服务启动完成后的状态说明
 * @return systemd数据报发送成功返回true
 */
bool SystemdNotifier::notifyReady(const std::string& status) const
{
    std::string state = "READY=1";

    if (!status.empty())
    {
        state += "\nSTATUS=" + sanitizeStatus(status);
    }

    return sendNotification(state);
}

/**
 * @brief 发送WATCHDOG=1存活通知
 * @return systemd数据报发送成功返回true
 */
bool SystemdNotifier::notifyWatchdog() const
{
    return sendNotification("WATCHDOG=1");
}

/**
 * @brief 发送STOPPING=1和可选STATUS字段
 * @param status 服务停止过程的状态说明
 * @return systemd数据报发送成功返回true
 */
bool SystemdNotifier::notifyStopping(const std::string& status) const
{
    std::string state = "STOPPING=1";

    if (!status.empty())
    {
        state += "\nSTATUS=" + sanitizeStatus(status);
    }

    return sendNotification(state);
}

/**
 * @brief 创建Unix数据报socket并发送systemd通知文本
 * @param state systemd通知协议文本
 * @return 数据报完整发送返回true，否则返回false
 */
bool SystemdNotifier::sendNotification(const std::string& state) const
{
    if (notifySocket_.empty() || state.empty())
    {
        return false;
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    socklen_t addressLength = 0;

    if (notifySocket_.front() == '@')
    {
        // systemd用@表示Linux抽象Unix Socket，实际sun_path首字节必须为\0
        if (notifySocket_.size() > sizeof(address.sun_path))
        {
            return false;
        }

        address.sun_path[0] = '\0';
        std::memcpy(
            address.sun_path + 1,
            notifySocket_.data() + 1,
            notifySocket_.size() - 1);
        addressLength = static_cast<socklen_t>(
            offsetof(sockaddr_un, sun_path) + notifySocket_.size());
    }
    else
    {
        if (notifySocket_.size() >= sizeof(address.sun_path))
        {
            return false;
        }

        std::memcpy(
            address.sun_path,
            notifySocket_.c_str(),
            notifySocket_.size() + 1);
        addressLength = static_cast<socklen_t>(
            offsetof(sockaddr_un, sun_path) + notifySocket_.size() + 1);
    }

    int socketFd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);

    if (socketFd < 0)
    {
        return false;
    }

    ssize_t sentSize = ::sendto(
        socketFd,
        state.data(),
        state.size(),
        MSG_NOSIGNAL,
        reinterpret_cast<const sockaddr*>(&address),
        addressLength);
    ::close(socketFd);
    return sentSize == static_cast<ssize_t>(state.size());
}

} // namespace edgelink
