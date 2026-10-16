#include "edgelink/device_connection_state_machine.h"

#include "edgelink/device_manager.h"

#include <chrono>
#include <iostream>
#include <string>
#include <utility>

namespace
{

/**
 * @brief 输出断言失败信息。
 * @param condition 待检查的条件。
 * @param message 失败时输出的说明。
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
 * @brief 验证设备合法状态转换、非法转换拒绝和无通信超时。
 * @return 所有状态机断言通过返回0。
 */
int main()
{
    edgelink::DeviceManager deviceManager;
    edgelink::Device device;
    device.id = "state-machine-device";
    device.name = "State Machine Device";
    device.protocol = "test";

    if (!deviceManager.addDevice(std::move(device)))
    {
        std::cerr << "[FAIL] Failed to add state machine device" << std::endl;
        return 1;
    }

    using StateMachine = edgelink::DeviceConnectionStateMachine;
    StateMachine::Clock::time_point now{};
    StateMachine stateMachine(
        &deviceManager,
        "state-machine-device",
        std::chrono::milliseconds(100),
        [&now]()
        {
            return now;
        });

    if (!check(
            stateMachine.state() == edgelink::ConnectionState::Disconnected,
            "Initial state should be Disconnected") ||
        !check(
            !stateMachine.transitionTo(edgelink::ConnectionState::Connected),
            "Disconnected should not jump directly to Connected") ||
        !check(
            stateMachine.transitionTo(edgelink::ConnectionState::Connecting),
            "Disconnected to Connecting failed") ||
        !check(
            stateMachine.transitionTo(edgelink::ConnectionState::Connected),
            "Connecting to Connected failed"))
    {
        return 1;
    }

    now += std::chrono::milliseconds(99);

    if (!check(!stateMachine.checkTimeout(), "Device timed out too early") ||
        !check(stateMachine.markActivity(), "Connected activity was rejected"))
    {
        return 1;
    }

    now += std::chrono::milliseconds(100);

    if (!check(stateMachine.checkTimeout(), "Device timeout was not detected") ||
        !check(
            stateMachine.state() == edgelink::ConnectionState::Timeout,
            "State machine did not enter Timeout") ||
        !check(
            !stateMachine.markActivity(),
            "Timeout device accepted activity") ||
        !check(
            !stateMachine.transitionTo(edgelink::ConnectionState::Connected),
            "Timeout should reconnect through Connecting"))
    {
        return 1;
    }

    if (!check(
            stateMachine.transitionTo(edgelink::ConnectionState::Connecting),
            "Timeout to Connecting failed") ||
        !check(
            stateMachine.transitionTo(edgelink::ConnectionState::Connected),
            "Reconnect to Connected failed") ||
        !check(
            stateMachine.transitionTo(edgelink::ConnectionState::Disconnected),
            "Connected to Disconnected failed") ||
        !check(
            std::string(edgelink::connectionStateToString(
                edgelink::ConnectionState::Timeout)) == "TIMEOUT",
            "Timeout state string is incorrect"))
    {
        return 1;
    }

    const edgelink::Device* storedDevice =
        deviceManager.findDevice("state-machine-device");

    if (!check(
            storedDevice != nullptr &&
                storedDevice->state == edgelink::ConnectionState::Disconnected,
            "DeviceManager state is not synchronized"))
    {
        return 1;
    }

    std::cout << "[PASS] Device connection state machine test finished" << std::endl;
    return 0;
}
