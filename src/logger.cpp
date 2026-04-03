#include "logger.h"
#include <iostream>
#include <chrono>
#include <ctime>
#include <iomanip>

Logger::~Logger() {
    if (ofs.is_open()) ofs.close();
}

Logger &Logger::instance() {
    static Logger L;
    return L;
}

void Logger::init(const std::string &file,
                  LogLevel minLevel,
                  bool enableConsole) {
    std::lock_guard<std::mutex> lk(mu);
    level.store(minLevel);
    toConsole = enableConsole;

    ofs.open(file, std::ios::app);
    toFile = ofs.good();
}

void Logger::setLevel(LogLevel lvl) {
    level.store(lvl);
}

void Logger::enableConsole(bool enabled) {
    std::lock_guard<std::mutex> lk(mu);
    toConsole = enabled;
}

std::string Logger::now() {
    using namespace std::chrono;

    auto t = system_clock::now();
    std::time_t tt = system_clock::to_time_t(t);

    std::tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &tt);
#else
    localtime_r(&tt, &tmv);
#endif

    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
    return std::string(buf);
}

void Logger::log(LogLevel lvl, const std::string &msg) {
    if (lvl > level.load()) return;

    std::string prefix;
    switch (lvl) {
        case LINFO:  prefix = "[INFO] "; break;
        case LWARN:  prefix = "[WARN] "; break;
        case LERROR: prefix = "[ERROR] "; break;
        case LDEBUG: prefix = "[DEBUG] "; break;
    }

    std::string line = now() + " " + prefix + msg + "\n";

    std::lock_guard<std::mutex> lk(mu);

    if (toConsole)
        std::cout << line;

    if (toFile && ofs) {
        ofs << line;
        if (lvl == LERROR)
            ofs.flush();
    }
}