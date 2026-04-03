#pragma once
#include <string>
#include <mutex>
#include <fstream>
#include <atomic>

enum LogLevel {
    LINFO = 0,
    LWARN = 1,
    LERROR = 2,
    LDEBUG = 3
};

class Logger {
public:
    static Logger &instance();

    void init(const std::string &file,
              LogLevel minLevel = LINFO,
              bool enableConsole = true);

    void log(LogLevel lvl, const std::string &msg);
    void setLevel(LogLevel lvl);
    void enableConsole(bool enabled);

private:
    Logger() = default;
    ~Logger();

    std::mutex mu;
    std::ofstream ofs;
    std::atomic<LogLevel> level{LINFO};

    bool toFile = false;
    bool toConsole = true;

    std::string now();
};