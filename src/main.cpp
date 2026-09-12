#include "edgelink/event_loop.h"
#include "edgelink/logger.h"
#include "edgelink/signal_handler.h"
#include "edgelink/tcp_server.h"
#include "edgelink/timer.h"

int main()
{
    edgelink::registerSignalHandlers();

    edgelink::EventLoop eventLoop;
    edgelink::TcpServer server(&eventLoop, 9000);

    if (!server.start())
    {
        edgelink::Logger::error("Failed to start TCP server");
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

    edgelink::Logger::info("EdgeLink TCP server is running");

    eventLoop.run();

    server.stop();

    edgelink::Logger::info("EdgeLink TCP server stopped");

    return 0;
}