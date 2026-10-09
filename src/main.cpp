#include "edgelink/control_command_processor.h"
#include "edgelink/event_loop.h"
#include "edgelink/logger.h"
#include "edgelink/signal_handler.h"
#include "edgelink/tcp_server.h"
#include "edgelink/timer.h"
#include "edgelink/unix_domain_server.h"

/**
 * @brief 启动 EdgeLink TCP 服务、本地控制服务和事件循环。
 * @return 正常退出返回 0，任一服务启动失败返回 1。
 */
int main()
{
    edgelink::registerSignalHandlers();

    edgelink::EventLoop eventLoop;
    edgelink::TcpServer server(&eventLoop, 9000);
    edgelink::UnixDomainServer controlServer(&eventLoop, "/tmp/edgelink.sock");
    edgelink::ControlCommandProcessor commandProcessor;

    if (!server.start())
    {
        edgelink::Logger::error("Failed to start TCP server");
        return 1;
    }

    // Socket层收到请求后，通过回调交给独立的命令处理器。
    controlServer.setMessageHandler([&commandProcessor](const std::string& request)
    {
        return commandProcessor.process(request);
    });

    if (!controlServer.start())
    {
        edgelink::Logger::error("Failed to start Unix Domain control server");
        server.stop();
        return 1;
    }

    // 周期检查退出信号
    edgelink::Timer signalTimer(&eventLoop);

    signalTimer.startPeriodic(200, [&eventLoop]()
    {
        if (edgelink::shutdownRequested())
        {
            eventLoop.stop();
        }
    });

    edgelink::Logger::info("EdgeLink TCP and local control servers are running");

    eventLoop.run();

    controlServer.stop();
    server.stop();

    edgelink::Logger::info("EdgeLink servers stopped");

    return 0;
}
