#include "edgelink/message_bus.h"
#include "edgelink/mqtt_service.h"

#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace
{

struct MqttPacket
{
    std::uint8_t header = 0;
    std::vector<std::uint8_t> body;
};

struct BrokerResult
{
    int connectionCount = 0;
    int publishCount = 0;
    bool receivedDisconnect = false;
    std::array<std::string, 2> publishTopics;
    std::array<std::string, 2> publishPayloads;
    std::string error;
};

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
 * @brief 从socket完整读取指定数量的字节
 * @param socketFd 已连接的socket描述符
 * @param data 接收缓冲区
 * @param size 期望读取的字节数
 * @return 完整读取返回true，否则返回false
 */
bool receiveAll(int socketFd, std::uint8_t* data, std::size_t size)
{
    std::size_t receivedSize = 0;

    while (receivedSize < size)
    {
        ssize_t result = ::recv(
            socketFd,
            data + receivedSize,
            size - receivedSize,
            0);

        if (result > 0)
        {
            receivedSize += static_cast<std::size_t>(result);
            continue;
        }

        if (result < 0 && errno == EINTR)
        {
            continue;
        }

        return false;
    }

    return true;
}

/**
 * @brief 读取一整个MQTT报文，包括固定头、变长长度和报文体
 * @param socketFd 已连接的socket描述符
 * @param packet 用于保存解析后的报文
 * @return 成功读取合法报文返回true，否则返回false
 */
bool receiveMqttPacket(int socketFd, MqttPacket& packet)
{
    if (!receiveAll(socketFd, &packet.header, 1))
    {
        return false;
    }

    std::size_t remainingLength = 0;
    std::size_t multiplier = 1;

    for (int byteIndex = 0; byteIndex < 4; ++byteIndex)
    {
        std::uint8_t encodedByte = 0;

        if (!receiveAll(socketFd, &encodedByte, 1))
        {
            return false;
        }

        remainingLength += (encodedByte & 0x7F) * multiplier;

        if ((encodedByte & 0x80) == 0)
        {
            packet.body.resize(remainingLength);
            return remainingLength == 0 ||
                   receiveAll(socketFd, packet.body.data(), remainingLength);
        }

        multiplier *= 128;
    }

    return false;
}

/**
 * @brief 从QoS 0 PUBLISH报文体中提取Topic和Payload
 * @param packet 已读取的PUBLISH报文
 * @param topic 输出MQTT Topic
 * @param payload 输出消息正文
 * @return 报文结构正确返回true，否则返回false
 */
bool parsePublishPacket(
    const MqttPacket& packet,
    std::string& topic,
    std::string& payload)
{
    if ((packet.header & 0xF0) != 0x30 || packet.body.size() < 2)
    {
        return false;
    }

    std::size_t topicLength =
        (static_cast<std::size_t>(packet.body[0]) << 8) |
        packet.body[1];

    if (packet.body.size() < 2 + topicLength)
    {
        return false;
    }

    topic.assign(
        packet.body.begin() + 2,
        packet.body.begin() + 2 + topicLength);
    payload.assign(
        packet.body.begin() + 2 + topicLength,
        packet.body.end());
    return true;
}

/**
 * @brief 创建只监听本机随机端口的测试Broker socket
 * @param port 输出内核分配的监听端口
 * @return 成功时返回监听socket，失败时返回-1
 */
int createListener(std::uint16_t& port)
{
    int listenerFd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);

    if (listenerFd < 0)
    {
        return -1;
    }

    int reuseAddress = 1;
    ::setsockopt(
        listenerFd,
        SOL_SOCKET,
        SO_REUSEADDR,
        &reuseAddress,
        sizeof(reuseAddress));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;

    if (::bind(
            listenerFd,
            reinterpret_cast<sockaddr*>(&address),
            sizeof(address)) != 0 ||
        ::listen(listenerFd, 1) != 0)
    {
        ::close(listenerFd);
        return -1;
    }

    socklen_t addressLength = sizeof(address);

    if (::getsockname(
            listenerFd,
            reinterpret_cast<sockaddr*>(&address),
            &addressLength) != 0)
    {
        ::close(listenerFd);
        return -1;
    }

    port = ntohs(address.sin_port);
    return listenerFd;
}

/**
 * @brief 在指定时间内等待并接受一个客户端连接
 * @param listenerFd MQTT测试监听socket
 * @param timeoutMs 最长等待毫秒数
 * @return 成功时返回客户端socket，超时或失败返回-1
 */
int acceptWithTimeout(int listenerFd, int timeoutMs)
{
    pollfd descriptor{};
    descriptor.fd = listenerFd;
    descriptor.events = POLLIN;

    int pollResult;

    do
    {
        pollResult = ::poll(&descriptor, 1, timeoutMs);
    }
    while (pollResult < 0 && errno == EINTR);

    if (pollResult <= 0 || (descriptor.revents & POLLIN) == 0)
    {
        return -1;
    }

    int clientFd = ::accept4(listenerFd, nullptr, nullptr, SOCK_CLOEXEC);

    if (clientFd >= 0)
    {
        timeval timeout{};
        timeout.tv_sec = 3;
        ::setsockopt(
            clientFd,
            SOL_SOCKET,
            SO_RCVTIMEO,
            &timeout,
            sizeof(timeout));
    }

    return clientFd;
}

/**
 * @brief 接收CONNECT报文并返回成功CONNACK
 * @param clientFd 已连接的客户端socket
 * @return 握手报文合法且回复成功时返回true
 */
bool completeBrokerHandshake(int clientFd)
{
    MqttPacket connectPacket;

    if (!receiveMqttPacket(clientFd, connectPacket) ||
        connectPacket.header != 0x10)
    {
        return false;
    }

    // Session Present=0，返回码=0表示Broker接受连接
    const std::uint8_t connack[] = {0x20, 0x02, 0x00, 0x00};
    return ::send(clientFd, connack, sizeof(connack), MSG_NOSIGNAL) ==
           static_cast<ssize_t>(sizeof(connack));
}

/**
 * @brief 在超时时间内轮询指定条件是否成立
 * @param predicate 需要等待的条件函数
 * @param timeout 最长等待时间
 * @return 条件在超时前成立返回true，否则返回false
 */
bool waitUntil(
    const std::function<bool()>& predicate,
    std::chrono::milliseconds timeout)
{
    auto deadline = std::chrono::steady_clock::now() + timeout;

    while (std::chrono::steady_clock::now() < deadline)
    {
        if (predicate())
        {
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return predicate();
}

/**
 * @brief 运行会主动断开一次连接的最小MQTT Broker
 * @param listenerFd 已创建的监听socket
 * @param result 用于返回两次连接、两条消息和最终断开信息
 */
void runFakeBroker(int listenerFd, BrokerResult& result)
{
    for (int connectionIndex = 0; connectionIndex < 2; ++connectionIndex)
    {
        int clientFd = acceptWithTimeout(listenerFd, 5000);

        if (clientFd < 0)
        {
            result.error = "timed out waiting for MQTT connection";
            break;
        }

        if (!completeBrokerHandshake(clientFd))
        {
            result.error = "invalid CONNECT packet";
            ::close(clientFd);
            break;
        }

        ++result.connectionCount;

        MqttPacket publishPacket;

        if (!receiveMqttPacket(clientFd, publishPacket) ||
            !parsePublishPacket(
                publishPacket,
                result.publishTopics[connectionIndex],
                result.publishPayloads[connectionIndex]))
        {
            result.error = "invalid PUBLISH packet";
            ::close(clientFd);
            break;
        }

        ++result.publishCount;

        if (connectionIndex == 0)
        {
            // 模拟Broker或网络突然中断，不发送正常的DISCONNECT
            ::shutdown(clientFd, SHUT_RDWR);
            ::close(clientFd);
            continue;
        }

        MqttPacket disconnectPacket;

        if (receiveMqttPacket(clientFd, disconnectPacket) &&
            disconnectPacket.header == 0xE0 &&
            disconnectPacket.body.empty())
        {
            result.receivedDisconnect = true;
        }
        else
        {
            result.error = "invalid DISCONNECT packet";
        }

        ::close(clientFd);
    }

    ::close(listenerFd);
}

} // namespace

/**
 * @brief 验证MQTT发布、意外掉线检测、自动重连和恢复发布
 * @return 全部检查通过返回0，否则返回1
 */
int main()
{
    std::uint16_t brokerPort = 0;
    int listenerFd = createListener(brokerPort);

    if (!check(listenerFd >= 0, "Failed to create fake MQTT Broker"))
    {
        return 1;
    }

    BrokerResult brokerResult;
    std::thread brokerThread(
        runFakeBroker,
        listenerFd,
        std::ref(brokerResult));

    edgelink::MqttConfig config;
    config.host = "127.0.0.1";
    config.port = brokerPort;
    config.clientId = "edgelink-test";
    config.connectTimeoutMs = 1000;
    config.topicPrefix = "factory/devices";

    edgelink::MessageBus messageBus;
    edgelink::MqttService service(config);

    bool success = true;
    success = check(
                  !service.publish("before/connect", "data"),
                  "Publish before connect should fail") &&
              success;
    success = check(service.connect(), "MQTT CONNECT failed") && success;
    success = check(service.isConnected(), "Service should be connected") &&
              success;
    success = check(
                  service.attachMessageBus(messageBus),
                  "MessageBus subscription failed") &&
              success;

    edgelink::Message message;
    message.topic = "device.tag.updated";
    message.deviceId = "pump-01";
    message.tagName = "pressure";
    message.value = 12.5;
    message.quality = edgelink::Quality::Good;
    message.timestamp = std::chrono::system_clock::time_point(
        std::chrono::milliseconds(123456789));

    success = check(
                  messageBus.publish(message) == 1,
                  "Tag message was not delivered to MqttService") &&
              success;

    success = check(
                  waitUntil(
                      [&service]()
                      {
                          return !service.isConnected();
                      },
                      std::chrono::milliseconds(2000)),
                  "Service did not detect MQTT disconnection") &&
              success;

    success = check(
                  waitUntil(
                      [&service]()
                      {
                          return service.isConnected();
                      },
                      std::chrono::milliseconds(4000)),
                  "Service did not reconnect to MQTT Broker") &&
              success;

    message.value = 13.75;
    message.timestamp = std::chrono::system_clock::time_point(
        std::chrono::milliseconds(123456999));

    success = check(
                  messageBus.publish(message) == 1,
                  "Tag message was not published after reconnect") &&
              success;

    service.detachMessageBus();
    service.disconnect();
    brokerThread.join();

    success = check(!service.isConnected(), "Service should be disconnected") &&
              success;
    success = check(
                  brokerResult.error.empty(),
                  brokerResult.error.c_str()) &&
              success;
    success = check(
                  brokerResult.connectionCount == 2,
                  "Broker should receive two MQTT connections") &&
              success;
    success = check(
                  brokerResult.publishCount == 2,
                  "Broker should receive two MQTT messages") &&
              success;
    success = check(
                  brokerResult.receivedDisconnect,
                  "Broker did not receive DISCONNECT") &&
              success;
    success = check(
                  brokerResult.publishTopics[0] ==
                      "factory/devices/pump-01/tags/pressure",
                  "First MQTT topic is incorrect") &&
              success;
    success = check(
                  brokerResult.publishPayloads[0] ==
                      "{\"deviceId\":\"pump-01\",\"tagName\":\"pressure\","
                      "\"value\":12.5,\"quality\":\"GOOD\","
                      "\"timestampMs\":123456789}",
                  "First JSON payload is incorrect") &&
              success;
    success = check(
                  brokerResult.publishPayloads[1] ==
                      "{\"deviceId\":\"pump-01\",\"tagName\":\"pressure\","
                      "\"value\":13.75,\"quality\":\"GOOD\","
                      "\"timestampMs\":123456999}",
                  "Second JSON payload is incorrect") &&
              success;

    if (!success)
    {
        return 1;
    }

    std::cout << "[PASS] MQTT service reconnected and resumed publishing"
              << std::endl;
    return 0;
}
