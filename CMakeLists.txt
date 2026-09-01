#CMake配置文件：这些代码如何编译，用CMake管理构建过程
#最低CMake版本要求
cmake_minimum_required(VERSION 3.16)

#定义一个工程：项目名 版本号 CXX即用C++）
project(EdgeLink VERSION 0.1.0 LANGUAGES CXX)

#项目统一用C++17
set(CMAKE_CXX_STANDARD 17)
#必须用指定的17
set(CMAKE_CXX_STANDARD_REQUIRED ON)
#尽量写标准 C++17，不依赖 GCC 自己私有的一些语法扩展
set(CMAKE_CXX_EXTENSIONS OFF)

#创建一个可执行程序：程序名 用哪个源文件来编译（以后可以继续添加） 
add_executable(edgelinkd
    src/main.cpp
)


#g++和gcc即：把源代码翻译成电脑能运行程序的编译器，gcc编译C，g++编译C++