#pragma once

#include "edgelink/device.h"

#include <chrono>
#include <functional>
#include <string>

namespace edgelink
{

class DeviceManager;

/**
 * @brief 管理单个设备的合法连接状态转换和通信超时。
 */
class DeviceConnectionStateMachine
{
public:
    using Clock = std::chrono::steady_clock;
    using NowProvider = std::function<Clock::time_point()>;

    /**
     * @brief 创建指定设备的连接状态机。
     * @param deviceManager 保存设备状态的DeviceManager。
     * @param deviceId 已注册设备的唯一ID。
     * @param timeout 设备在Connected状态下的最大无通信时间。
     * @param nowProvider 提供单调时间的回调，留空时使用steady_clock。
     */
    DeviceConnectionStateMachine(
        DeviceManager* deviceManager,
        std::string deviceId,
        std::chrono::milliseconds timeout,
        NowProvider nowProvider = {});

    DeviceConnectionStateMachine(const DeviceConnectionStateMachine&) = delete;
    DeviceConnectionStateMachine& operator=(const DeviceConnectionStateMachine&) = delete;

    /**
     * @brief 按状态转换规则更新设备状态。
     * @param nextState 目标状态。
     * @return 转换合法且DeviceManager更新成功返回true。
     */
    bool transitionTo(ConnectionState nextState);

    /**
     * @brief 记录一次成功通信，重置超时计时起点。
     * @return 仅在Connected状态下记录成功并返回true。
     */
    bool markActivity();

    /**
     * @brief 检查距离最后一次成功通信是否已超过限制。
     * @return 本次检查触发Connected到Timeout转换返回true。
     */
    bool checkTimeout();

    /**
     * @brief 获取状态机当前状态。
     * @return 当前ConnectionState。
     */
    ConnectionState state() const;

private:
    /**
     * @brief 判断两个连接状态之间是否允许直接转换。
     * @param currentState 当前状态。
     * @param nextState 目标状态。
     * @return 符合状态机规则返回true。
     */
    static bool isTransitionAllowed(
        ConnectionState currentState,
        ConnectionState nextState);

    DeviceManager* deviceManager_;
    std::string deviceId_;
    std::chrono::milliseconds timeout_;
    NowProvider nowProvider_;
    Clock::time_point lastActivity_{};
    ConnectionState state_ = ConnectionState::Disconnected;
};

} // namespace edgelink
