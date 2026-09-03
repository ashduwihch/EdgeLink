#pragma once    //这个头文件在一次编译过程中只算一次,防止后续多个cpp都包含导致多次编译
#include <string>
#include <mutex>

namespace edgelink
{
    enum class LogLevel
    {
        Info,
        Warn,
        Error
    };

    class Logger{
        public:
        //设置最低日志输出级别
        static void setLevel(LogLevel level);

        //通用日志接口
        static void log(LogLevel level,const std::string& message);

        //常用分级日志接口
        static void info(const std::string& message);
        static void warn(const std::string& message);
        static void error(const std::string& message);
        private:
        //判断当前日志级别是否应该输出
        static bool shouldLog(LogLevel level);

        //将日志级别转为字符串
        static const char* levelToString(LogLevel level);
        //生成当前时间字符串
        static std::string currentTimestamp();
        //当前最低日志级别
        static LogLevel minLevel_;
        //保证多个线程同时打印时不会互相穿插
        static std::mutex mutex_;
    };
}