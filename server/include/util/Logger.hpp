#pragma once

#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

namespace orbital::util {

class Logger {
public:
    enum class Level { Info, Warning, Error };

    static Logger& instance();

    template <typename... Args>
    void log(Level level, Args&&... args) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream oss;
        (oss << ... << std::forward<Args>(args));
        auto now = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(now);
        auto tm = *std::localtime(&tt);
        std::cout << std::put_time(&tm, "%H:%M:%S") << " [" << levelToString(level) << "] " << oss.str() << std::endl;
    }

private:
    Logger() = default;
    std::mutex mutex_;

    const char* levelToString(Level level);
};

#define ORBITAL_LOG(level, ...) \
    ::orbital::util::Logger::instance().log(level, __VA_ARGS__)

} // namespace orbital::util
