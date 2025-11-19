#include "util/Logger.hpp"

namespace orbital::util {

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

const char* Logger::levelToString(Level level) {
    switch (level) {
    case Level::Info:
        return "INFO";
    case Level::Warning:
        return "WARN";
    case Level::Error:
        return "ERROR";
    }
    return "INFO";
}

} // namespace orbital::util
