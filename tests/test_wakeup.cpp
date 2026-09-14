#include "edgelink/event_loop.h"

#include <chrono>
#include <iostream>
#include <thread>

int main()
{
    edgelink::EventLoop eventLoop;

    std::thread stopper([&]()
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        std::cout << "[PASS] Wakeup triggered" << std::endl;

        eventLoop.stop();
    });

    eventLoop.run();

    stopper.join();

    return 0;
}