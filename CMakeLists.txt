# CMake配置文件：这些代码如何编译，用CMake管理构建过程

# 最低CMake版本要求
cmake_minimum_required(VERSION 3.16)
# 定义工程：项目名 版本号 CXX即使用C++
project(EdgeLink VERSION 0.2.0 LANGUAGES CXX)
# 项目统一使用C++17
set(CMAKE_CXX_STANDARD 17)
# 必须使用指定的C++17
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# 尽量使用标准C++17，不依赖GCC私有语法扩展
set(CMAKE_CXX_EXTENSIONS OFF)
# 查找第三方库,REQUIRED表示找不到就直接让 CMake 配置失败
find_package(yaml-cpp REQUIRED)
find_package(Threads REQUIRED)

# 创建EdgeLink核心库：放项目中的公共功能模块

add_library(edgelink  #这里edgelink是CMake 里的一个 target（构建目标）
    src/logger.cpp
    src/config.cpp
    src/thread_pool.cpp
    src/signal_handler.cpp
    src/event_loop.cpp
    src/channel.cpp
    src/timer.cpp
    src/wakeup.cpp
    src/tcp_server.cpp
)

# 告诉CMake去哪里找项目自己的头文件
target_include_directories(edgelink
    PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

# 让EdgeLink核心库使用第三方库
target_link_libraries(edgelink
    PUBLIC
    yaml-cpp
    Threads::Threads
)

# 创建一个叫 edgelinkd 的可执行程序
add_executable(edgelinkd
    src/main.cpp
)

# 让主程序使用EdgeLink核心库
target_link_libraries(edgelinkd
    PRIVATE
    edgelink
)

# 开启CMake测试功能
enable_testing()

# 创建EventLoop测试程序
add_executable(test_event_loop
    tests/test_event_loop.cpp
)

# 让EventLoop测试程序使用EdgeLink核心库
target_link_libraries(test_event_loop
    PRIVATE
    edgelink
)

# 把 test_timer.cpp 编译成一个叫 test_timer 的普通可执行程序
add_executable(test_timer
    tests/test_timer.cpp
)

# 让Timer测试程序使用EdgeLink核心库
target_link_libraries(test_timer
    PRIVATE
    edgelink
)

# 创建Wakeup测试程序
add_executable(test_wakeup
    tests/test_wakeup.cpp
)

# 让Wakeup测试程序使用EdgeLink核心库
target_link_libraries(test_wakeup
    PRIVATE
    edgelink
)

# 创建TCP Server测试程序
add_executable(test_tcp_server
    tests/test_tcp_server.cpp
)

# 让TCP Server测试程序使用EdgeLink核心库
target_link_libraries(test_tcp_server
    PRIVATE
    edgelink
)

# 把自动测试程序注册给CTest
add_test(NAME EventLoopTest COMMAND test_event_loop)
add_test(NAME TimerTest COMMAND test_timer)
add_test(NAME WakeupTest COMMAND test_wakeup)