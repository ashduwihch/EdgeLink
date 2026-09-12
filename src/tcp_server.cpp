#include "edgelink/tcp_server.h"
#include "edgelink/event_loop.h"
#include "edgelink/logger.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string>
#include <utility>

namespace edgelink
{

// 保存 EventLoop 和监听端口
TcpServer::TcpServer(EventLoop* loop, std::uint16_t port) : loop_(loop), port_(port)
{
}

// 停止服务器并释放资源
TcpServer::~TcpServer()
{
    stop();
}

// 启动 TCP Server
bool TcpServer::start()
{
    if (listenFd_ >= 0)
    {
        return true;
    }

    // 创建 IPv4 TCP 非阻塞监听 Socket
    listenFd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);

    if (listenFd_ < 0)
    {
        Logger::error("Failed to create server socket: " + std::string(std::strerror(errno)));
        return false;
    }

    // 允许服务器重启后快速重新绑定地址
    int reuseAddress = 1;

    if (setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &reuseAddress, sizeof(reuseAddress)) < 0)
    {
        Logger::error("Failed to set SO_REUSEADDR: " + std::string(std::strerror(errno)));
        close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    // 配置服务器监听地址
    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddress.sin_port = htons(port_);

    // 绑定 IP 和端口
    if (bind(listenFd_, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) < 0)
    {
        Logger::error("Failed to bind server socket: " + std::string(std::strerror(errno)));
        close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    // 开始监听客户端连接
    if (listen(listenFd_, SOMAXCONN) < 0)
    {
        Logger::error("Failed to listen on server socket: " + std::string(std::strerror(errno)));
        close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    // 为监听 Socket 创建 Channel
    listenChannel_ = std::make_unique<Channel>(listenFd_);
    listenChannel_->setEvents(EPOLLIN);

    listenChannel_->setReadCallback([this]()
    {
        handleAccept();
    });

    listenChannel_->setErrorCallback([]()
    {
        Logger::error("Listen socket error");
    });

    // 注册监听 Channel
    if (!loop_->addChannel(listenChannel_.get()))
    {
        listenChannel_.reset();
        close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    Logger::info("TCP server started on port " + std::to_string(port_));
    return true;
}

// 接收新的客户端连接
void TcpServer::handleAccept()
{
    while (true)
    {
        sockaddr_in clientAddress{};
        socklen_t addressLength = sizeof(clientAddress);

        int clientFd = accept4(listenFd_, reinterpret_cast<sockaddr*>(&clientAddress), &addressLength, SOCK_NONBLOCK | SOCK_CLOEXEC);

        if (clientFd < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return;
            }

            if (errno == EINTR)
            {
                continue;
            }

            Logger::error("Failed to accept client: " + std::string(std::strerror(errno)));
            return;
        }

        auto clientChannel = std::make_unique<Channel>(clientFd);
        clientChannel->setEvents(EPOLLIN | EPOLLRDHUP);

        clientChannel->setReadCallback([this, clientFd]()
        {
            handleClientRead(clientFd);
        });

        clientChannel->setWriteCallback([this, clientFd]()
        {
            handleClientWrite(clientFd);
        });

        clientChannel->setErrorCallback([this, clientFd]()
        {
            Logger::error("Client socket error, fd=" + std::to_string(clientFd));
            removeClient(clientFd);
        });

        if (!loop_->addChannel(clientChannel.get()))
        {
            close(clientFd);
            continue;
        }

        ClientState state;
        state.channel = std::move(clientChannel);

        clients_.emplace(clientFd, std::move(state));

        char clientIp[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &clientAddress.sin_addr, clientIp, sizeof(clientIp));

        Logger::info("Client connected, fd=" + std::to_string(clientFd) + ", address=" + std::string(clientIp) + ":" + std::to_string(ntohs(clientAddress.sin_port)));
    }
}

// 读取客户端发送的数据
void TcpServer::handleClientRead(int clientFd)
{
    auto client = clients_.find(clientFd);

    if (client == clients_.end())
    {
        return;
    }

    char buffer[4096];

    while (true)
    {
        ssize_t bytesRead = recv(clientFd, buffer, sizeof(buffer), 0);

        if (bytesRead > 0)
        {
            Logger::info("Received from client fd=" + std::to_string(clientFd) + ", bytes=" + std::to_string(bytesRead));

            // 将需要返回的数据加入发送缓冲区
            client->second.outputBuffer.append(buffer, static_cast<std::size_t>(bytesRead));
            continue;
        }

        // TCP 对端正常关闭连接
        if (bytesRead == 0)
        {
            Logger::info("Client disconnected, fd=" + std::to_string(clientFd));
            removeClient(clientFd);
            return;
        }

        if (errno == EINTR)
        {
            continue;
        }

        // 非阻塞 Socket 当前已经没有更多数据
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            break;
        }

        Logger::error("Failed to receive data from client fd=" + std::to_string(clientFd) + ": " + std::string(std::strerror(errno)));
        removeClient(clientFd);
        return;
    }

    // 数据读取完成后尝试发送
    handleClientWrite(clientFd);
}

// 向客户端发送缓冲区中的数据
void TcpServer::handleClientWrite(int clientFd)
{
    auto client = clients_.find(clientFd);

    if (client == clients_.end())
    {
        return;
    }

    ClientState& state = client->second;

    while (state.writeOffset < state.outputBuffer.size())
    {
        const char* data = state.outputBuffer.data() + state.writeOffset;
        std::size_t remaining = state.outputBuffer.size() - state.writeOffset;

        ssize_t bytesWritten = send(clientFd, data, remaining, MSG_NOSIGNAL);

        if (bytesWritten > 0)
        {
            state.writeOffset += static_cast<std::size_t>(bytesWritten);
            continue;
        }

        if (bytesWritten < 0 && errno == EINTR)
        {
            continue;
        }

        // Socket 暂时不可写，等待下一次 EPOLLOUT
        if (bytesWritten < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            updateClientEvents(clientFd, EPOLLIN | EPOLLOUT | EPOLLRDHUP);
            return;
        }

        Logger::error("Failed to send data to client fd=" + std::to_string(clientFd) + ": " + std::string(std::strerror(errno)));
        removeClient(clientFd);
        return;
    }

    // 当前缓冲区已经全部发送完成
    state.outputBuffer.clear();
    state.writeOffset = 0;

    updateClientEvents(clientFd, EPOLLIN | EPOLLRDHUP);
}

// 修改客户端 Channel 当前监听的事件
bool TcpServer::updateClientEvents(int clientFd, std::uint32_t events)
{
    auto client = clients_.find(clientFd);

    if (client == clients_.end())
    {
        return false;
    }

    Channel* channel = client->second.channel.get();

    // 当前 EventLoop 尚未提供 EPOLL_CTL_MOD，因此通过删除再重新注册更新事件
    if (!loop_->removeChannel(channel))
    {
        return false;
    }

    channel->setEvents(events);

    if (!loop_->addChannel(channel))
    {
        Logger::error("Failed to update client events, fd=" + std::to_string(clientFd));
        removeClient(clientFd);
        return false;
    }

    return true;
}

// 移除已经断开的客户端
void TcpServer::removeClient(int clientFd)
{
    auto client = clients_.find(clientFd);

    if (client == clients_.end())
    {
        return;
    }

    loop_->removeChannel(client->second.channel.get());
    close(clientFd);

    clients_.erase(client);
}

// 停止 TCP Server
void TcpServer::stop()
{
    for (auto& client : clients_)
    {
        loop_->removeChannel(client.second.channel.get());
        close(client.first);
    }

    clients_.clear();

    if (listenChannel_)
    {
        loop_->removeChannel(listenChannel_.get());
        listenChannel_.reset();
    }

    if (listenFd_ >= 0)
    {
        close(listenFd_);
        listenFd_ = -1;
    }
}

}  // namespace edgelink