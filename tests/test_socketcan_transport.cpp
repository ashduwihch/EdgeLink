#include "socketcan_transport.h"

#include <net/if.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

/**
 * @brief 在限定时间内轮询接收一帧非阻塞SocketCAN数据。
 * @param transport 用来接收的SocketCAN传输对象。
 * @param frame 成功时写入收到的CAN帧。
 * @return 500ms内收到完整帧返回true。
 */
bool receiveWithTimeout(
    edgelink::SocketCanTransport& transport,
    edgelink::SocketCanFrame& frame)
{
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        const edgelink::SocketCanReceiveResult result =
            transport.receiveFrame(frame);

        if (result == edgelink::SocketCanReceiveResult::Received)
        {
            return true;
        }

        if (result == edgelink::SocketCanReceiveResult::Error)
        {
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    return false;
}

/**
 * @brief 验证CAN帧参数校验，并在vcan接口上完成标准帧收发。
 * @return 测试通过返回0，vcan不存在返回77，失败返回1。
 */
int main()
{
    edgelink::SocketCanFrame validFrame;
    validFrame.id = 0x123;
    validFrame.dataLength = 4;
    validFrame.data[0] = 0x11;
    validFrame.data[1] = 0x22;
    validFrame.data[2] = 0x33;
    validFrame.data[3] = 0x44;

    edgelink::SocketCanFrame invalidStandardFrame = validFrame;
    invalidStandardFrame.id = 0x800;

    edgelink::SocketCanFrame validExtendedFrame = validFrame;
    validExtendedFrame.id = 0x1ABCDE;
    validExtendedFrame.extended = true;

    if (!edgelink::SocketCanTransport::isValidFrame(validFrame) ||
        edgelink::SocketCanTransport::isValidFrame(invalidStandardFrame) ||
        !edgelink::SocketCanTransport::isValidFrame(validExtendedFrame))
    {
        std::cerr << "[FAIL] SocketCAN frame validation failed" << std::endl;
        return 1;
    }

    const char* configuredInterface = std::getenv("EDGELINK_VCAN_INTERFACE");
    const std::string interfaceName =
        configuredInterface != nullptr ? configuredInterface : "vcan0";

    if (if_nametoindex(interfaceName.c_str()) == 0)
    {
        std::cout
            << "[SKIP] SocketCAN interface not found: "
            << interfaceName << std::endl;
        return 77;
    }

    edgelink::SocketCanConfig config;
    config.interfaceName = interfaceName;
    edgelink::SocketCanTransport sender(config);
    edgelink::SocketCanTransport receiver(config);

    if (!sender.connect() || !receiver.connect())
    {
        std::cerr << "[FAIL] Failed to connect vcan transports" << std::endl;
        return 1;
    }

    edgelink::SocketCanFrame receivedFrame;
    const bool passed =
        sender.sendFrame(validFrame) &&
        receiveWithTimeout(receiver, receivedFrame) &&
        receivedFrame.id == validFrame.id &&
        receivedFrame.dataLength == validFrame.dataLength &&
        receivedFrame.data[0] == validFrame.data[0] &&
        receivedFrame.data[1] == validFrame.data[1] &&
        receivedFrame.data[2] == validFrame.data[2] &&
        receivedFrame.data[3] == validFrame.data[3];

    sender.disconnect();
    receiver.disconnect();

    if (!passed)
    {
        std::cerr << "[FAIL] SocketCAN frame transmission failed" << std::endl;
        return 1;
    }

    std::cout << "[PASS] SocketCAN frame transmitted on " << interfaceName << std::endl;
    return 0;
}
