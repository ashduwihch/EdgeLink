#include "edgelink/event_loop.h"
#include "edgelink/timer.h"

#include <iostream>

int main()
{
    edgelink::EventLoop eventLoop;

    edgelink::Timer timer(&eventLoop);

    int count = 0;

    timer.startPeriodic(500, [&]()
    {
        count++;

        std::cout << "[PASS] Timer tick " << count << std::endl;

        if (count >= 3)
        {
            eventLoop.stop();
        }
    });

    eventLoop.run();

    return 0;
}