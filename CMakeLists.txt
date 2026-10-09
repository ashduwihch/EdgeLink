# CMake配置文件：这些代码如何编译，用CMake管理构建过程

# 最低CMake版本要求
cmake_minimum_required(VERSION 3.16)
# 定义工程：项目名 版本号 CXX即使用C++
project(EdgeLink VERSION 0.3.0 LANGUAGES CXX)
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
    src/message_bus.cpp
    src/unix_domain_server.cpp
    src/control_command_processor.cpp
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

# 创建Mock插件动态库
add_library(mock_plugin SHARED  #构建动态共享库
    plugins/mock_plugin/mock_plugin.cpp
)

target_include_directories(mock_plugin
    PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/plugins/mock_plugin
)

target_link_libraries(mock_plugin
    PRIVATE
    edgelink
)

# 创建第一版Modbus TCP插件动态库
add_library(modbus_plugin SHARED
    plugins/modbus_plugin/modbus_plugin.cpp
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

# 把test_timer.cpp编译成一个叫test_timer的普通可执行程序
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

# 创建直接调用Mock插件接口的测试程序
add_executable(test_plugin
    tests/test_plugin.cpp
)

target_include_directories(test_plugin
    PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(test_plugin
    PRIVATE
    mock_plugin
)

# 创建PluginManager动态加载测试程序
add_executable(test_plugin_manager
    tests/test_plugin_manager.cpp
)

target_link_libraries(test_plugin_manager
    PRIVATE
    edgelink
)

# 保证运行PluginManager测试前Mock插件已经生成
add_dependencies(test_plugin_manager mock_plugin)

# 创建Device/Tag数据模型测试程序
add_executable(test_device_model
    tests/test_device_model.cpp
)

target_link_libraries(test_device_model
    PRIVATE
    edgelink
)

# 创建设备管理测试程序
add_executable(test_device_manager
    tests/test_device_manager.cpp
)

target_link_libraries(test_device_manager
    PRIVATE
    edgelink
)

# 创建Modbus TCP插件读寄存器测试程序
add_executable(test_modbus_plugin
    tests/test_modbus_plugin.cpp
)

target_link_libraries(test_modbus_plugin
    PRIVATE
    modbus_plugin
)

# 验证PluginManager可以动态加载Modbus插件
add_executable(test_modbus_plugin_load
    tests/test_modbus_plugin_load.cpp
)

target_link_libraries(test_modbus_plugin_load
    PRIVATE
    edgelink
)

add_dependencies(test_modbus_plugin_load modbus_plugin)

# 创建进程内MessageBus测试程序
add_executable(test_message_bus
    tests/test_message_bus.cpp
)

target_link_libraries(test_message_bus
    PRIVATE
    edgelink
)

# 创建MessageBus并发发布测试程序
add_executable(test_message_bus_concurrent
    tests/test_message_bus_concurrent.cpp
)

target_link_libraries(test_message_bus_concurrent
    PRIVATE
    edgelink
)

# 创建Unix Domain Socket本地进程通信测试程序
add_executable(test_unix_domain_server
    tests/test_unix_domain_server.cpp
)

target_link_libraries(test_unix_domain_server
    PRIVATE
    edgelink
)

# 创建本地控制命令解析测试程序
add_executable(test_control_command_processor
    tests/test_control_command_processor.cpp
)

target_link_libraries(test_control_command_processor
    PRIVATE
    edgelink
)

# 把自动测试程序注册给CTest
add_test(NAME EventLoopTest COMMAND test_event_loop)
add_test(NAME TimerTest COMMAND test_timer)
add_test(NAME WakeupTest COMMAND test_wakeup)
add_test(NAME PluginTest COMMAND test_plugin)
add_test(NAME PluginManagerTest COMMAND test_plugin_manager $<TARGET_FILE:mock_plugin>)
add_test(NAME DeviceModelTest COMMAND test_device_model)
add_test(NAME DeviceManagerTest COMMAND test_device_manager)
add_test(NAME ModbusPluginTest COMMAND test_modbus_plugin)
add_test(NAME ModbusPluginLoadTest COMMAND test_modbus_plugin_load $<TARGET_FILE:modbus_plugin>)
add_test(NAME MessageBusTest COMMAND test_message_bus)
add_test(NAME MessageBusConcurrentTest COMMAND test_message_bus_concurrent)
add_test(NAME UnixDomainServerTest COMMAND test_unix_domain_server)
add_test(NAME ControlCommandProcessorTest COMMAND test_control_command_processor)
