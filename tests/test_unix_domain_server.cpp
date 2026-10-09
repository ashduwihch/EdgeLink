#include "edgelink/control_command_processor.h"
#include "edgelink/event_loop.h"
#include "edgelink/timer.h"
#include "edgelink/unix_domain_server.h"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

namespace
{

/**
 * @brief 模拟程序异常退出后遗留在文件系统中的 Socket 文件。
 * @param socketPath 用来创建失效 Socket 文件的路径。
 * @return 创建成功返回 true，否则返回 false。
 */
bool createStaleSocket(const std::string& socketPath)
{
    int staleFd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);

    if (staleFd < 0)
    {
        return false;
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, socketPath.c_str(), socketPath.size() + 1);

    bool created = bind(
        staleFd,
        reinterpret_cast<sockaddr*>(&address),
        sizeof(address)) == 0;
    close(staleFd);
    return created;
}

/**
 * @brief 作为独立客户端进程连接服务器，并完成一次请求与响应交换。
 * @param socketPath Unix Domain Socket 的文件路径。
 * @return 收到预期响应返回 true，否则返回 false。
 */
bool exchangeControlMessage(const std::string& socketPath)
{
    int clientFd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);

    if (clientFd < 0)
    {
        return false;
    }

    sockaddr_un serverAddress{};
    serverAddress.sun_family = AF_UNIX;
    std::memcpy(serverAddress.sun_path, socketPath.c_str(), socketPath.size() + 1);

    if (connect(clientFd, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) < 0)
    {
        close(clientFd);
        return false;
    }

    const std::string request = "status";

    if (send(clientFd, request.data(), request.size(), MSG_NOSIGNAL) != static_cast<ssize_t>(request.size()))
    {
        close(clientFd);
        return false;
    }

    char responseBuffer[128]{};
    ssize_t bytesRead = recv(clientFd, responseBuffer, sizeof(responseBuffer), 0);
    close(clientFd);

    if (bytesRead <= 0)
    {
        return false;
    }

    return std::string(responseBuffer, static_cast<std::size_t>(bytesRead)) == "OK RUNNING";
}

}  // namespace

/**
 * @brief 验证两个独立进程能通过 Unix Domain Socket 交换一条完整控制消息。
 * @return 全部检查通过返回 0，否则返回 1。
 */
int main()
{
    const std::string socketPath = "/tmp/edgelink-control-" + std::to_string(getpid()) + ".sock";

    if (!createStaleSocket(socketPath))
    {
        std::cerr << "[FAIL] Could not create stale socket for cleanup test" << std::endl;
        return 1;
    }

    edgelink::EventLoop eventLoop;

    // 验证第二个服务端不会删除第一个仍在监听的Socket文件。
    const std::string activePath = socketPath + ".active";
    edgelink::UnixDomainServer firstServer(&eventLoop, activePath);
    edgelink::UnixDomainServer secondServer(&eventLoop, activePath);

    if (!firstServer.start() || secondServer.start())
    {
        std::cerr << "[FAIL] Active socket ownership protection failed" << std::endl;
        firstServer.stop();
        return 1;
    }

    firstServer.stop();

    edgelink::UnixDomainServer server(&eventLoop, socketPath);
    edgelink::ControlCommandProcessor processor;

    std::string receivedRequest;
    server.setMessageHandler([&receivedRequest, &processor](const std::string& request)
    {
        receivedRequest = request;
        return processor.process(request);
    });

    if (!server.start())
    {
        std::cerr << "[FAIL] Unix Domain Server start failed" << std::endl;
        return 1;
    }

    pid_t childPid = fork();

    if (childPid < 0)
    {
        std::cerr << "[FAIL] fork failed: " << std::strerror(errno) << std::endl;
        server.stop();
        return 1;
    }

    if (childPid == 0)
    {
        _exit(exchangeControlMessage(socketPath) ? 0 : 1);
    }

    bool childFinished = false;
    bool timedOut = false;
    int childStatus = 0;
    int checks = 0;

    edgelink::Timer checkTimer(&eventLoop);
    checkTimer.startPeriodic(10, [&]()
    {
        pid_t result = waitpid(childPid, &childStatus, WNOHANG);

        if (result == childPid)
        {
            childFinished = true;
            eventLoop.stop();
            return;
        }

        ++checks;

        if (checks >= 300)
        {
            timedOut = true;
            eventLoop.stop();
        }
    });

    eventLoop.run();
    checkTimer.stop();

    if (!childFinished)
    {
        // 只终止由本测试创建且已经超时的子进程，避免测试永久卡住。
        kill(childPid, SIGKILL);
        waitpid(childPid, &childStatus, 0);
    }

    server.stop();

    bool childSucceeded = childFinished && WIFEXITED(childStatus) && WEXITSTATUS(childStatus) == 0;
    bool socketRemoved = access(socketPath.c_str(), F_OK) != 0;

    if (timedOut || !childSucceeded || receivedRequest != "status" || !socketRemoved)
    {
        std::cerr << "[FAIL] Unix Domain Socket process communication failed" << std::endl;
        return 1;
    }

    std::cout << "[PASS] independent processes exchanged: status -> OK RUNNING" << std::endl;
    std::cout << "[PASS] active socket file was not replaced" << std::endl;
    std::cout << "[PASS] stale socket file was safely replaced" << std::endl;
    std::cout << "[PASS] socket file was removed after server stopped" << std::endl;
    return 0;
}
