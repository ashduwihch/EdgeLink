#include "edgelink/channel.h"
#include "edgelink/event_loop.h"

#include <sys/epoll.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <thread>

int main()
{
    edgelink::EventLoop eventLoop;

    int pipeFds[2];

    if (pipe(pipeFds) < 0)
    {
        std::cerr << "Failed to create pipe" << std::endl;
        return 1;
    }

    edgelink::Channel channel(pipeFds[0]);
    channel.setEvents(EPOLLIN);

    channel.setReadCallback([&]()
    {
        char buffer[64]{};

        ssize_t bytesRead = read(pipeFds[0], buffer, sizeof(buffer));

        if (bytesRead > 0)
        {
            std::cout << "[PASS] EventLoop received: " << buffer << std::endl;
        }

        eventLoop.stop();
    });

    if (!eventLoop.addChannel(&channel))
    {
        std::cerr << "Failed to add channel" << std::endl;
        close(pipeFds[0]);
        close(pipeFds[1]);
        return 1;
    }

    std::thread writer([&]()
    {
        sleep(1);

        const char* message = "hello epoll";
        write(pipeFds[1], message, std::strlen(message));
    });

    eventLoop.run();

    writer.join();

    eventLoop.removeChannel(&channel);

    close(pipeFds[0]);
    close(pipeFds[1]);

    return 0;
}