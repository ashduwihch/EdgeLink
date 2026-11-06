# CMake配置文件：这些代码如何编译，用CMake管理构建过程

# 最低CMake版本要求
cmake_minimum_required(VERSION 3.16)
# 定义工程：项目名 版本号 CXX即使用C++
project(EdgeLink VERSION 0.6.0 LANGUAGES CXX)
# 项目统一使用C++17
set(CMAKE_CXX_STANDARD 17)
# 必须使用指定的C++17
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# 尽量使用标准C++17，不依赖GCC私有语法扩展
set(CMAKE_CXX_EXTENSIONS OFF)
# 查找第三方库，REQUIRED表示找不到就直接让CMake配置失败
find_package(yaml-cpp REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBMODBUS REQUIRED IMPORTED_TARGET libmodbus)
pkg_check_modules(LIBMOSQUITTO REQUIRED IMPORTED_TARGET libmosquitto)
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)

# 创建EdgeLink核心库：放项目中的公共功能模块
add_library(edgelink
    src/logger.cpp
    src/config.cpp
    src/thread_pool.cpp
    src/signal_handler.cpp
    src/event_loop.cpp
    src/channel.cpp
    src/timer.cpp
    src/wakeup.cpp
    src/tcp_server.cpp
    src/plugin_manager.cpp
    src/device_manager.cpp
    src/device_connection_state_machine.cpp
    src/reconnect_backoff.cpp
    src/device_scheduler.cpp
    src/message_bus.cpp
    src/unix_domain_server.cpp
    src/control_command_processor.cpp
    src/shared_memory_buffer.cpp
    src/eventfd_notifier.cpp
    src/ipc_message_codec.cpp
    src/shared_memory_message_bridge.cpp
    src/mqtt_service.cpp
    src/storage_service.cpp
    src/systemd_notifier.cpp
    src/system_monitor.cpp
)

# 动态插件会链接edgelink静态库，因此核心库需要使用位置无关代码
set_target_properties(edgelink PROPERTIES POSITION_INDEPENDENT_CODE ON)

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
    ${CMAKE_DL_LIBS}   #链接linux动态加载库
    rt                  #POSIX Shared Memory接口
    PkgConfig::LIBMOSQUITTO
    PkgConfig::SQLITE3
)

# 创建一个叫edgelinkd的可执行程序
add_executable(edgelinkd
    src/main.cpp
)

# 让主程序使用EdgeLink核心库
target_link_libraries(edgelinkd
    PRIVATE
    edgelink
)

# 创建第一版Modbus TCP插件动态库
add_library(modbus_plugin SHARED
    plugins/modbus_plugin/modbus_plugin.cpp
    plugins/modbus_plugin/modbus_rtu_transport.cpp
)

target_include_directories(modbus_plugin
    PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/plugins/modbus_plugin
)

target_link_libraries(modbus_plugin
    PUBLIC
    edgelink
    PkgConfig::LIBMODBUS
)

# 创建SocketCAN动态插件
add_library(socketcan_plugin SHARED
    plugins/socketcan_plugin/socketcan_plugin.cpp
    plugins/socketcan_plugin/socketcan_transport.cpp
)

target_include_directories(socketcan_plugin
    PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/plugins/socketcan_plugin
)

target_link_libraries(socketcan_plugin
    PUBLIC
    edgelink
)
