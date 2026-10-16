#include "edgelink/device_connection_state_machine.h"

#include "edgelink/device_manager.h"
#include "edgelink/logger.h"

#include <stdexcept>
#include <utility>

namespace edgelink
{

/**
 * @brief 校验设备和超时参数，并从DeviceManager读取初始状态。
 */
DeviceConnectionStateMachine::DeviceConnectionStateMachine(
    DeviceManager* deviceManager,
    std::string deviceId,
    std::chrono::milliseconds timeout,
    NowProvider nowProvider)
    : deviceManager_(deviceManager),
      deviceId_(std::move(deviceId)),
      timeout_(timeout),
      nowProvider_(std::move(nowProvider))
{
    if (deviceManager_ == nullptr ||
        deviceId_.empty() ||
        timeout_.count() <= 0)
    {
        throw std::invalid_argument(
            "Invalid device connection state machine configuration");
    }

    const Device* device = deviceManager_->findDevice(deviceId_);

    if (device == nullptr)
    {
        throw std::invalid_argument("Connection state machine device not found");
    }

    if (!nowProvider_)
    {
        nowProvider_ = []()
        {
            return Clock::now();
        };
    }

    state_ = device->state;
    lastActivity_ = nowProvider_();
}

/**
 * @brief 校验转换后同步更新DeviceManager中的设备状态。
 */
bool DeviceConnectionStateMachine::transitionTo(ConnectionState nextState)
{
    if (nextState == state_)
    {
        return true;
    }

    if (!isTransitionAllowed(state_, nextState))
    {
        Logger::error(
            "Invalid device state transition: " +
            std::string(connectionStateToString(state_)) +
            " -> " +
            connectionStateToString(nextState));
        return false;
    }

    if (!deviceManager_->updateConnectionState(deviceId_, nextState))
    {
        return false;
    }

    state_ = nextState;

    if (state_ == ConnectionState::Connected)
    {
        lastActivity_ = nowProvider_();
    }

    return true;
}

/**
 * @brief 在设备已连接时刷新最后成功通信时间。
 */
bool DeviceConnectionStateMachine::markActivity()
{
    if (state_ != ConnectionState::Connected)
    {
        return false;
    }

    lastActivity_ = nowProvider_();
    return true;
}

/**
 * @brief 使用单调时钟检查无通信时长，超时时进入Timeout。
 */
bool DeviceConnectionStateMachine::checkTimeout()
{
    if (state_ != ConnectionState::Connected)
    {
        return false;
    }

    if (nowProvider_() - lastActivity_ < timeout_)
    {
        return false;
    }

    return transitionTo(ConnectionState::Timeout);
}

/**
 * @brief 返回状态机内部保存的当前状态。
 */
ConnectionState DeviceConnectionStateMachine::state() const
{
    return state_;
}

/**
 * @brief 定义断开、连接中、已连接、超时和错误之间的合法路径。
 */
bool DeviceConnectionStateMachine::isTransitionAllowed(
    ConnectionState currentState,
    ConnectionState nextState)
{
    switch (currentState)
    {
        case ConnectionState::Disconnected:
            return nextState == ConnectionState::Connecting;

        case ConnectionState::Connecting:
            return nextState == ConnectionState::Connected ||
                   nextState == ConnectionState::Timeout ||
                   nextState == ConnectionState::Error ||
                   nextState == ConnectionState::Disconnected;

        case ConnectionState::Connected:
            return nextState == ConnectionState::Timeout ||
                   nextState == ConnectionState::Error ||
                   nextState == ConnectionState::Disconnected;

        case ConnectionState::Timeout:
        case ConnectionState::Error:
            return nextState == ConnectionState::Connecting ||
                   nextState == ConnectionState::Disconnected;
    }

    return false;
}

} // namespace edgelink
