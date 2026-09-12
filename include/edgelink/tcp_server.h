#pragma once

#include "edgelink/channel.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace edgelink
{

class EventLoop;

class TcpServer
{
public:
    TcpServer(EventLoop* loop, std::uint16_t port);
    ~TcpServer();

    // 启动 TCP Server
    bool start();

    // 停止 TCP Server
    void stop();

private:
    struct ClientState
    {
        std::unique_ptr<Channel> channel;
        std::string outputBuffer;
        std::size_t writeOffset = 0;
    };

    // 处理新的客户端连接
    void handleAccept();

    // 处理客户端可读事件
    void handleClientRead(int clientFd);

    // 处理客户端可写事件
    void handleClientWrite(int clientFd);

    // 更新客户端监听事件
    bool updateClientEvents(int clientFd, std::uint32_t events);

    // 移除客户端
    void removeClient(int clientFd);

    EventLoop* loop_;
    std::uint16_t port_;

    int listenFd_ = -1;
    std::unique_ptr<Channel> listenChannel_;

    std::unordered_map<int, ClientState> clients_;
};

}  // namespace edgelink