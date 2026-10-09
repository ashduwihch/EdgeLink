#include "edgelink/unix_domain_server.h"

#include "edgelink/event_loop.h"
#include "edgelink/logger.h"

#include <cerrno>
#include <cstring>
#include <exception>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <utility>

namespace edgelink
{

namespace
{

constexpr std::size_t MaxControlMessageSize = 4096;

/**
 * @brief 探测已有 Socket 文件是否已经没有服务端监听。
 * @param socketPath 需要探测的 Unix Domain Socket 路径。
 * @return 连接被拒绝时说明文件已失效并返回 true；其余情况返回 false。
 */
bool isStaleSocketPath(const std::string& socketPath)
{
    int probeFd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);

    if (probeFd < 0)
    {
        return false;
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, socketPath.c_str(), socketPath.size() + 1);

    int connectResult = connect(
        probeFd,
        reinterpret_cast<sockaddr*>(&address),
        sizeof(address));
    int connectError = errno;
    close(probeFd);

    return connectResult < 0 && connectError == ECONNREFUSED;
}

}  // namespace

/**
 * @brief 保存事件循环和 Unix Domain Socket 路径。
 * @param loop 负责监听 Socket 事件的事件循环。
 * @param socketPath Unix Domain Socket 在文件系统中的路径。
 */
UnixDomainServer::UnixDomainServer(EventLoop* loop, std::string socketPath)
    : loop_(loop), socketPath_(std::move(socketPath))
{
}

/**
 * @brief 停止服务器并释放其拥有的系统资源。
 */
UnixDomainServer::~UnixDomainServer()
{
    stop();
}

/**
 * @brief 保存处理控制请求的业务回调。
 * @param handler 接收请求字符串并返回响应字符串的回调函数。
 */
void UnixDomainServer::setMessageHandler(MessageHandler handler)
{
    messageHandler_ = std::move(handler);
}

/**
 * @brief 创建非阻塞 SOCK_SEQPACKET Socket，并将监听 Channel 注册到 EventLoop。
 * @return 启动成功返回 true，否则返回 false。
 */
bool UnixDomainServer::start()
{
    if (listenFd_ >= 0)
    {
        return true;
    }

    if (loop_ == nullptr || socketPath_.empty())
    {
        Logger::error("EventLoop or Unix Domain Socket path is invalid");
        return false;
    }

    sockaddr_un serverAddress{};

    if (socketPath_.size() >= sizeof(serverAddress.sun_path))
    {
        Logger::error("Unix Domain Socket path is too long: " + socketPath_);
        return false;
    }

    // 只清理上次遗留的 Socket 文件，绝不能覆盖同名普通文件。
    struct stat pathStatus{};

    if (lstat(socketPath_.c_str(), &pathStatus) == 0)
    {
        if (!S_ISSOCK(pathStatus.st_mode))
        {
            Logger::error("Refusing to overwrite a non-socket file: " + socketPath_);
            return false;
        }

        // 能连接或无法确认状态时都拒绝删除，避免破坏另一个正在运行的实例。
        if (!isStaleSocketPath(socketPath_))
        {
            Logger::error("Unix Domain Socket path is already in use: " + socketPath_);
            return false;
        }

        if (unlink(socketPath_.c_str()) < 0)
        {
            Logger::error("Failed to remove stale Unix Domain Socket: " + std::string(std::strerror(errno)));
            return false;
        }
    }
    else if (errno != ENOENT)
    {
        Logger::error("Failed to inspect Unix Domain Socket path: " + std::string(std::strerror(errno)));
        return false;
    }

    listenFd_ = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);

    if (listenFd_ < 0)
    {
        Logger::error("Failed to create Unix Domain Socket: " + std::string(std::strerror(errno)));
        return false;
    }

    serverAddress.sun_family = AF_UNIX;
    std::memcpy(serverAddress.sun_path, socketPath_.c_str(), socketPath_.size() + 1);

    if (bind(listenFd_, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) < 0)
    {
        Logger::error("Failed to bind Unix Domain Socket: " + std::string(std::strerror(errno)));
        close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    ownsSocketPath_ = true;

    if (listen(listenFd_, SOMAXCONN) < 0)
    {
        Logger::error("Failed to listen on Unix Domain Socket: " + std::string(std::strerror(errno)));
        stop();
        return false;
    }

    listenChannel_ = std::make_unique<Channel>(listenFd_);
    listenChannel_->setEvents(EPOLLIN);
    listenChannel_->setReadCallback([this]()
    {
        handleAccept();
    });
    listenChannel_->setErrorCallback([]()
    {
        Logger::error("Unix Domain Socket listen error");
    });

    if (!loop_->addChannel(listenChannel_.get()))
    {
        stop();
        return false;
    }

    Logger::info("Unix Domain Server started at " + socketPath_);
    return true;
}

/**
 * @brief 接收所有已经排队的本地客户端，并为每个客户端创建 Channel。
 */
void UnixDomainServer::handleAccept()
{
    while (true)
    {
        int clientFd = accept4(listenFd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);

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

            Logger::error("Failed to accept Unix Domain client: " + std::string(std::strerror(errno)));
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
            Logger::error("Unix Domain client error, fd=" + std::to_string(clientFd));
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
        Logger::info("Unix Domain client connected, fd=" + std::to_string(clientFd));
    }
}

/**
 * @brief 按消息边界读取请求，调用业务回调，再将响应放入发送队列。
 * @param clientFd 客户端 Socket 的文件描述符。
 */
void UnixDomainServer::handleClientRead(int clientFd)
{
    auto client = clients_.find(clientFd);

    if (client == clients_.end())
    {
        return;
    }

    char buffer[MaxControlMessageSize];

    while (true)
    {
        ssize_t bytesRead = recv(clientFd, buffer, sizeof(buffer), MSG_TRUNC);

        if (bytesRead > 0)
        {
            std::string response;

            if (static_cast<std::size_t>(bytesRead) > sizeof(buffer))
            {
                response = "ERROR: control message is too large";
            }
            else if (!messageHandler_)
            {
                response = "ERROR: no message handler";
            }
            else
            {
                try
                {
                    response = messageHandler_(std::string(buffer, static_cast<std::size_t>(bytesRead)));
                }
                catch (const std::exception& exception)
                {
                    Logger::error("Control message handler failed: " + std::string(exception.what()));
                    response = "ERROR: handler failed";
                }
                catch (...)
                {
                    Logger::error("Control message handler failed with an unknown exception");
                    response = "ERROR: handler failed";
                }
            }

            if (!response.empty())
            {
                client->second.outputMessages.push_back(std::move(response));
            }

            continue;
        }

        if (bytesRead == 0)
        {
            removeClient(clientFd);
            return;
        }

        if (errno == EINTR)
        {
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            break;
        }

        Logger::error("Failed to receive Unix Domain message: " + std::string(std::strerror(errno)));
        removeClient(clientFd);
        return;
    }

    handleClientWrite(clientFd);
}

/**
 * @brief 逐条发送响应；Socket 暂不可写时改为监听 EPOLLOUT。
 * @param clientFd 客户端 Socket 的文件描述符。
 */
void UnixDomainServer::handleClientWrite(int clientFd)
{
    auto client = clients_.find(clientFd);

    if (client == clients_.end())
    {
        return;
    }

    auto& outputMessages = client->second.outputMessages;

    while (!outputMessages.empty())
    {
        const std::string& message = outputMessages.front();
        ssize_t bytesWritten = send(clientFd, message.data(), message.size(), MSG_NOSIGNAL);

        if (bytesWritten == static_cast<ssize_t>(message.size()))
        {
            outputMessages.pop_front();
            continue;
        }

        if (bytesWritten < 0 && errno == EINTR)
        {
            continue;
        }

        if (bytesWritten < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            updateClientEvents(clientFd, EPOLLIN | EPOLLOUT | EPOLLRDHUP);
            return;
        }

        Logger::error("Failed to send Unix Domain message: " + std::string(std::strerror(errno)));
        removeClient(clientFd);
        return;
    }

    updateClientEvents(clientFd, EPOLLIN | EPOLLRDHUP);
}

/**
 * @brief 通过先移除再注册 Channel 的方式更新客户端事件。
 * @param clientFd 客户端 Socket 的文件描述符。
 * @param events 新的 epoll 事件掩码。
 * @return 更新成功返回 true，否则返回 false。
 */
bool UnixDomainServer::updateClientEvents(int clientFd, std::uint32_t events)
{
    auto client = clients_.find(clientFd);

    if (client == clients_.end())
    {
        return false;
    }

    Channel* channel = client->second.channel.get();

    if (channel->events() == events)
    {
        return true;
    }

    if (!loop_->removeChannel(channel))
    {
        return false;
    }

    channel->setEvents(events);

    if (!loop_->addChannel(channel))
    {
        Logger::error("Failed to update Unix Domain client events, fd=" + std::to_string(clientFd));
        close(clientFd);
        clients_.erase(client);
        return false;
    }

    return true;
}

/**
 * @brief 注销客户端 Channel、关闭文件描述符并删除客户端状态。
 * @param clientFd 客户端 Socket 的文件描述符。
 */
void UnixDomainServer::removeClient(int clientFd)
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

/**
 * @brief 关闭全部客户端和监听 Socket，并清理服务器创建的 Socket 文件。
 */
void UnixDomainServer::stop()
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

    if (ownsSocketPath_)
    {
        if (unlink(socketPath_.c_str()) < 0 && errno != ENOENT)
        {
            Logger::error("Failed to remove Unix Domain Socket path: " + std::string(std::strerror(errno)));
        }

        ownsSocketPath_ = false;
    }
}

}  // namespace edgelink
