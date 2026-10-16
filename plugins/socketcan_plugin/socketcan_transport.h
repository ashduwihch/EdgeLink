#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace edgelink
{

constexpr std::size_t socketCanClassicMaxDataLength = 8;

/**
 * @brief EdgeLink内部使用的经典CAN帧。
 */
struct SocketCanFrame
{
    std::uint32_t id = 0;
    std::array<std::uint8_t, socketCanClassicMaxDataLength> data{};
    std::uint8_t dataLength = 0;
    bool extended = false;
    bool remoteTransmissionRequest = false;
};

/**
 * @brief SocketCAN网络接口配置。
 */
struct SocketCanConfig
{
    std::string interfaceName = "vcan0";
};

/**
 * @brief 接收CAN帧时可能出现的三种结果。
 */
enum class SocketCanReceiveResult
{
    Received,
    NoData,
    Error
};

/**
 * @brief 封装Linux SocketCAN原始套接字的经典CAN帧收发。
 */
class SocketCanTransport
{
public:
    /**
     * @brief 创建SocketCAN传输对象。
     * @param config CAN网络接口配置。
     */
    explicit SocketCanTransport(SocketCanConfig config = {});

    /**
     * @brief 关闭SocketCAN套接字。
     */
    ~SocketCanTransport();

    SocketCanTransport(const SocketCanTransport&) = delete;
    SocketCanTransport& operator=(const SocketCanTransport&) = delete;

    /**
     * @brief 创建非阻塞CAN套接字并绑定到指定接口。
     * @return 连接成功返回true。
     */
    bool connect();

    /**
     * @brief 关闭CAN套接字。
     */
    void disconnect();

    /**
     * @brief 查询SocketCAN是否已绑定网络接口。
     * @return 已连接返回true。
     */
    bool isConnected() const;

    /**
     * @brief 向CAN总线发送一帧数据。
     * @param frame 待发送的标准帧或扩展帧。
     * @return 完整帧发送成功返回true。
     */
    bool sendFrame(const SocketCanFrame& frame);

    /**
     * @brief 非阻塞接收一帧CAN数据。
     * @param frame 收到数据时写入解析后的CAN帧。
     * @return Received表示收到，NoData表示暂无数据，Error表示读取失败。
     */
    SocketCanReceiveResult receiveFrame(SocketCanFrame& frame);

    /**
     * @brief 校验CAN ID和数据长度是否符合经典CAN规则。
     * @param frame 待校验的CAN帧。
     * @return 标准帧或扩展帧参数合法返回true。
     */
    static bool isValidFrame(const SocketCanFrame& frame);

private:
    SocketCanConfig config_;
    int socketFd_ = -1;
};

} // namespace edgelink
