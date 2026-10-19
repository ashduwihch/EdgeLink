#include "edgelink/systemd_notifier.h"

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace
{

/**
 * @brief 检查测试条件，失败时输出对应错误
 * @param condition 需要验证的条件
 * @param message 条件失败时的说明
 * @return 条件成立返回true，否则返回false
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

/**
 * @brief 创建用于接收systemd通知的Unix数据报socket
 * @param socketPath 输出唯一socket文件路径
 * @return 成功返回socket描述符，否则返回-1
 */
int createNotificationReceiver(std::string& socketPath)
{
    char pathTemplate[] = "/tmp/edgelink-notify-XXXXXX";
    int temporaryFd = ::mkstemp(pathTemplate);

    if (temporaryFd < 0)
    {
        return -1;
    }

    ::close(temporaryFd);
    ::unlink(pathTemplate);
    socketPath = pathTemplate;

    int socketFd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);

    if (socketFd < 0)
    {
        return -1;
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;

    if (socketPath.size() >= sizeof(address.sun_path))
    {
        ::close(socketFd);
        return -1;
    }

    std::memcpy(
        address.sun_path,
        socketPath.c_str(),
        socketPath.size() + 1);
    socklen_t addressLength = static_cast<socklen_t>(
        offsetof(sockaddr_un, sun_path) + socketPath.size() + 1);

    if (::bind(
            socketFd,
            reinterpret_cast<sockaddr*>(&address),
            addressLength) != 0)
    {
        ::close(socketFd);
        return -1;
    }

    timeval timeout{};
    timeout.tv_sec = 1;
    ::setsockopt(
        socketFd,
        SOL_SOCKET,
        SO_RCVTIMEO,
        &timeout,
        sizeof(timeout));
    return socketFd;
}

/**
 * @brief 从测试socket读取一条systemd通知数据报
 * @param socketFd 已绑定的Unix数据报socket
 * @return 收到的通知文本，失败返回空字符串
 */
std::string receiveNotification(int socketFd)
{
    char buffer[512]{};
    ssize_t receivedSize = ::recv(
        socketFd,
        buffer,
        sizeof(buffer),
        0);

    return receivedSize > 0
               ? std::string(buffer, static_cast<std::size_t>(receivedSize))
               : std::string{};
}

/**
 * @brief 清理测试使用的systemd环境变量
 */
void clearSystemdEnvironment()
{
    ::unsetenv("NOTIFY_SOCKET");
    ::unsetenv("WATCHDOG_USEC");
    ::unsetenv("WATCHDOG_PID");
}

} // namespace

/**
 * @brief 验证READY、WATCHDOG、STOPPING通知和watchdog环境解析
 * @return 全部检查通过返回0，否则返回1
 */
int main()
{
    std::string socketPath;
    int receiverFd = createNotificationReceiver(socketPath);

    if (!check(receiverFd >= 0, "Failed to create notification receiver"))
    {
        return 1;
    }

    ::setenv("NOTIFY_SOCKET", socketPath.c_str(), 1);
    ::setenv("WATCHDOG_USEC", "4000000", 1);
    std::string processId = std::to_string(::getpid());
    ::setenv("WATCHDOG_PID", processId.c_str(), 1);

    edgelink::SystemdNotifier notifier;
    bool success = true;
    success = check(
                  notifier.isAvailable(),
                  "NOTIFY_SOCKET should be available") &&
              success;
    success = check(
                  notifier.watchdogInterval() ==
                      std::chrono::milliseconds(4000),
                  "WATCHDOG_USEC was parsed incorrectly") &&
              success;

    success = check(
                  notifier.notifyReady("ready\nline two"),
                  "Failed to send READY notification") &&
              success;
    success = check(
                  receiveNotification(receiverFd) ==
                      "READY=1\nSTATUS=ready line two",
                  "READY notification payload is incorrect") &&
              success;

    success = check(
                  notifier.notifyWatchdog(),
                  "Failed to send WATCHDOG notification") &&
              success;
    success = check(
                  receiveNotification(receiverFd) == "WATCHDOG=1",
                  "WATCHDOG notification payload is incorrect") &&
              success;

    success = check(
                  notifier.notifyStopping("shutdown"),
                  "Failed to send STOPPING notification") &&
              success;
    success = check(
                  receiveNotification(receiverFd) ==
                      "STOPPING=1\nSTATUS=shutdown",
                  "STOPPING notification payload is incorrect") &&
              success;

    std::string differentProcessId = std::to_string(::getpid() + 1);
    ::setenv("WATCHDOG_PID", differentProcessId.c_str(), 1);
    edgelink::SystemdNotifier wrongProcessNotifier;
    success = check(
                  wrongProcessNotifier.watchdogInterval().count() == 0,
                  "Watchdog for another process should be ignored") &&
              success;

    clearSystemdEnvironment();
    ::close(receiverFd);
    ::unlink(socketPath.c_str());

    if (!success)
    {
        return 1;
    }

    std::cout << "[PASS] systemd notify and watchdog messages are valid"
              << std::endl;
    return 0;
}
