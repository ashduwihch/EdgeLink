#include "edgelink/event_loop.h"
#include "edgelink/tcp_server.h"
#include "edgelink/timer.h"

#include <iostream>

int main()
{
    edgelink::EventLoop eventLoop;
    edgelink::TcpServer server(&eventLoop, 9000);

    if (!server.start())
    {
        std::cerr << "[FAIL] TCP server start failed" << std::endl;
        return 1;
    }

    std::cout << "[PASS] TCP server started on port 9000" << std::endl;
    std::cout << "Use: nc 127.0.0.1 9000" << std::endl;
    std::cout << "Server will stop after 10 seconds" << std::endl;

    edgelink::Timer timer(&eventLoop);

    timer.startOnce(10000, [&]()
    {
        eventLoop.stop();
    });

    eventLoop.run();

    server.stop();

    return 0;
}