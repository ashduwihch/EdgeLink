#pragma once

#include <chrono>
#include <string>

namespace edgelink
{

// SystemdNotifier：无需额外库，直接通过NOTIFY_SOCKET发送systemd通知
class SystemdNotifier
{
public:
    /**
     * @brief 从环境变量读取systemd通知socket和watchdog配置
     */
    SystemdNotifier();

    SystemdNotifier(const SystemdNotifier&) = delete;
    SystemdNotifier& operator=(const SystemdNotifier&) = delete;

    /**
     * @brief 查询当前进程是否由支持notify的systemd服务启动
     * @return NOTIFY_SOCKET存在时返回true，否则返回false
     */
    bool isAvailable() const;

    /**
     * @brief 读取systemd要求的watchdog完整超时时间
     * @return 启用watchdog时返回毫秒间隔，否则返回0毫秒
     */
    std::chrono::milliseconds watchdogInterval() const;

    /**
     * @brief 通知systemd服务已经完成初始化
     * @param status 可选的服务状态说明
     * @return 通知成功发送返回true，否则返回false
     */
    bool notifyReady(const std::string& status = "") const;

    /**
     * @brief 向systemd发送一次watchdog存活心跳
     * @return 通知成功发送返回true，否则返回false
     */
    bool notifyWatchdog() const;

    /**
     * @brief 通知systemd服务已经开始正常停止流程
     * @param status 可选的停止状态说明
     * @return 通知成功发送返回true，否则返回false
     */
    bool notifyStopping(const std::string& status = "") const;

private:
    /**
     * @brief 向NOTIFY_SOCKET发送一段systemd状态文本
     * @param state systemd通知协议文本
     * @return 数据报完整发送返回true，否则返回false
     */
    bool sendNotification(const std::string& state) const;

    std::string notifySocket_;
    std::chrono::milliseconds watchdogInterval_{0};
};

} // namespace edgelink
