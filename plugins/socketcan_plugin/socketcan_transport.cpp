#include "socketcan_transport.h"

#include "edgelink/logger.h"

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <utility>

namespace edgelink
{

/**
 * @brief 保存SocketCAN接口配置，延迟到connect时创建套接字。
 */
SocketCanTransport::SocketCanTransport(SocketCanConfig config)
    : config_(std::move(config))
{
}

/**
 * @brief 自动关闭SocketCAN套接字。
 */
SocketCanTransport::~SocketCanTransport()
{
    disconnect();
}

/**
 * @brief 创建PF_CAN原始套接字，查询接口索引并完成bind。
 */
bool SocketCanTransport::connect()
{
    if (socketFd_ >= 0)
    {
        return true;
    }

    if (config_.interfaceName.empty() || config_.interfaceName.size() >= IFNAMSIZ)
    {
        Logger::error("Invalid SocketCAN interface name");
        return false;
    }

    int socketFd = socket(
        PF_CAN,
        SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC,
        CAN_RAW);

    if (socketFd < 0)
    {
        Logger::error(
            "Failed to create SocketCAN socket: " +
            std::string(std::strerror(errno)));
        return false;
    }

    ifreq interfaceRequest{};
    std::strncpy(
        interfaceRequest.ifr_name,
        config_.interfaceName.c_str(),
        IFNAMSIZ - 1);

    if (ioctl(socketFd, SIOCGIFINDEX, &interfaceRequest) < 0)
    {
        Logger::error(
            "Failed to find SocketCAN interface " +
            config_.interfaceName + ": " +
            std::string(std::strerror(errno)));
        close(socketFd);
        return false;
    }

    sockaddr_can address{};
    address.can_family = AF_CAN;
    address.can_ifindex = interfaceRequest.ifr_ifindex;

    if (bind(
            socketFd,
            reinterpret_cast<sockaddr*>(&address),
            sizeof(address)) < 0)
    {
        Logger::error(
            "Failed to bind SocketCAN interface: " +
            std::string(std::strerror(errno)));
        close(socketFd);
        return false;
    }

    socketFd_ = socketFd;
    Logger::info("SocketCAN connected: " + config_.interfaceName);
    return true;
}

/**
 * @brief 关闭当前SocketCAN文件描述符。
 */
void SocketCanTransport::disconnect()
{
    if (socketFd_ < 0)
    {
        return;
    }

    close(socketFd_);
    socketFd_ = -1;
    Logger::info("SocketCAN disconnected");
}

/**
 * @brief 通过套接字fd判断是否已连接。
 */
bool SocketCanTransport::isConnected() const
{
    return socketFd_ >= 0;
}

/**
 * @brief 将EdgeLink CAN帧编码为Linux can_frame并发送。
 */
bool SocketCanTransport::sendFrame(const SocketCanFrame& frame)
{
    if (socketFd_ < 0)
    {
        Logger::error("SocketCAN transport is not connected");
        return false;
    }

    if (!isValidFrame(frame))
    {
        Logger::error("Invalid SocketCAN frame");
        return false;
    }

    can_frame kernelFrame{};
    kernelFrame.can_id = frame.id;
    kernelFrame.can_dlc = frame.dataLength;

    if (frame.extended)
    {
        kernelFrame.can_id |= CAN_EFF_FLAG;
    }

    if (frame.remoteTransmissionRequest)
    {
        kernelFrame.can_id |= CAN_RTR_FLAG;
    }

    std::memcpy(
        kernelFrame.data,
        frame.data.data(),
        frame.dataLength);

    const ssize_t writtenBytes = write(
        socketFd_,
        &kernelFrame,
        sizeof(kernelFrame));

    if (writtenBytes != static_cast<ssize_t>(sizeof(kernelFrame)))
    {
        Logger::error(
            "Failed to send SocketCAN frame: " +
            std::string(std::strerror(errno)));
        return false;
    }

    return true;
}

/**
 * @brief 读取Linux can_frame并解析为EdgeLink CAN帧。
 */
SocketCanReceiveResult SocketCanTransport::receiveFrame(SocketCanFrame& frame)
{
    if (socketFd_ < 0)
    {
        Logger::error("SocketCAN transport is not connected");
        return SocketCanReceiveResult::Error;
    }

    can_frame kernelFrame{};
    const ssize_t readBytes = read(
        socketFd_,
        &kernelFrame,
        sizeof(kernelFrame));

    if (readBytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
    {
        return SocketCanReceiveResult::NoData;
    }

    if (readBytes != static_cast<ssize_t>(sizeof(kernelFrame)) ||
        (kernelFrame.can_id & CAN_ERR_FLAG) != 0)
    {
        Logger::error("Failed to receive SocketCAN frame");
        return SocketCanReceiveResult::Error;
    }

    SocketCanFrame receivedFrame;
    receivedFrame.extended = (kernelFrame.can_id & CAN_EFF_FLAG) != 0;
    receivedFrame.remoteTransmissionRequest =
        (kernelFrame.can_id & CAN_RTR_FLAG) != 0;
    receivedFrame.id = kernelFrame.can_id &
        (receivedFrame.extended ? CAN_EFF_MASK : CAN_SFF_MASK);
    receivedFrame.dataLength = kernelFrame.can_dlc;
    std::memcpy(
        receivedFrame.data.data(),
        kernelFrame.data,
        receivedFrame.dataLength);

    frame = receivedFrame;
    return SocketCanReceiveResult::Received;
}

/**
 * @brief 限制经典CAN最大8字节，标准ID为11位，扩展ID为29位。
 */
bool SocketCanTransport::isValidFrame(const SocketCanFrame& frame)
{
    if (frame.dataLength > socketCanClassicMaxDataLength)
    {
        return false;
    }

    const std::uint32_t maximumId =
        frame.extended ? CAN_EFF_MASK : CAN_SFF_MASK;
    return frame.id <= maximumId;
}

} // namespace edgelink
