#include "edgelink/logger.h"

#include <chrono>  //时间，时间间隔
#include <ctime>     //年月日...
#include <iomanip>    //输出格式控制
#include <iostream>
#include <sstream>   //把很多内容拼成一个string

namespace edgelink
{

// 静态成员定义
LogLevel Logger::minLevel_ = LogLevel::Info;
std::mutex Logger::mutex_;

void Logger::setLevel(LogLevel level)
{
    std::lock_guard<std::mutex> lock(mutex_);
    minLevel_ = level;
}

void Logger::log(LogLevel level, const std::string& message)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!shouldLog(level))
    {
        return;
    }

    // [时间] [级别] 消息
    std::clog << "[" << currentTimestamp() << "] "
              << "[" << levelToString(level) << "] "
              << message << std::endl;
}

void Logger::info(const std::string& message)
{
    log(LogLevel::Info, message);
}

void Logger::warn(const std::string& message)
{
    log(LogLevel::Warn, message);
}

void Logger::error(const std::string& message)
{
    log(LogLevel::Error, message);
}

bool Logger::shouldLog(LogLevel level)
{
    return static_cast<int>(level) >= static_cast<int>(minLevel_);
}

const char* Logger::levelToString(LogLevel level)
{
    switch (level)
    {
        case LogLevel::Info:
            return "INFO";

        case LogLevel::Warn:
            return "WARN";

        case LogLevel::Error:
            return "ERROR";
    }

    return "UNKNOWN";
}

std::string Logger::currentTimestamp()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime =
        std::chrono::system_clock::to_time_t(now);

    std::tm localTime{};
    ::localtime_r(&nowTime, &localTime);

    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) %
        1000;

    std::ostringstream oss;

    oss << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S")
        << "."
        << std::setfill('0')
        << std::setw(3)
        << milliseconds.count();

    return oss.str();
}

}  // namespace edgelink