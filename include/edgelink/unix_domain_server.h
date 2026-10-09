#pragma once

#include "edgelink/channel.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace edgelink
{

class EventLoop;

/**
 * @brief 基于 Unix Domain Socket 的本地进程控制服务器。
 *
 * 它使用 SOCK_SEQPACKET 保留每条控制消息的边界，并将所有 Socket
 * 事件交给 EventLoop 统一处理。
 */
class UnixDomainServer
{
public:
    using MessageHandler = std::function<std::string(const std::string&)>;

    /**
     * @brief 创建本地控制服务器，但暂不开始监听。
     * @param loop 负责监听 Socket 事件的事件循环。
     * @param socketPath Unix Domain Socket 在文件系统中的路径。
     */
    UnixDomainServer(EventLoop* loop, std::string socketPath);

    /**
     * @brief 停止服务器并释放 Socket、Channel 和套接字路径。
     */
    ~UnixDomainServer();

    /**
     * @brief 设置收到控制消息后要执行的业务处理函数。
     * @param handler 接收请求字符串并返回响应字符串的回调函数。
     */
    void setMessageHandler(MessageHandler handler);

    /**
     * @brief 创建、绑定并监听 Unix Domain Socket。
     * @return 启动成功返回 true，否则返回 false。
     */
    bool start();

    /**
     * @brief 停止监听，断开所有客户端并删除本服务器创建的套接字文件。
     */
    void stop();

private:
    struct ClientState
    {
        std::unique_ptr<Channel> channel;
        std::deque<std::string> outputMessages;
    };

    /**
     * @brief 接收当前等待建立连接的所有本地客户端。
     */
    void handleAccept();

    /**
     * @brief 读取并处理指定客户端发来的完整控制消息。
     * @param clientFd 客户端 Socket 的文件描述符。
     */
    void handleClientRead(int clientFd);

    /**
     * @brief 向指定客户端发送队列中等待的响应消息。
     * @param clientFd 客户端 Socket 的文件描述符。
     */
    void handleClientWrite(int clientFd);

    /**
     * @brief 更新指定客户端需要监听的 epoll 事件。
     * @param clientFd 客户端 Socket 的文件描述符。
     * @param events 新的 epoll 事件掩码。
     * @return 更新成功返回 true，否则返回 false。
     */
    bool updateClientEvents(int clientFd, std::uint32_t events);

    /**
     * @brief 从 EventLoop 中移除并关闭指定客户端。
     * @param clientFd 客户端 Socket 的文件描述符。
     */
    void removeClient(int clientFd);

    EventLoop* loop_;
    std::string socketPath_;
    MessageHandler messageHandler_;

    int listenFd_ = -1;
    bool ownsSocketPath_ = false;
    std::unique_ptr<Channel> listenChannel_;
    std::unordered_map<int, ClientState> clients_;
};

}  // namespace edgelink
