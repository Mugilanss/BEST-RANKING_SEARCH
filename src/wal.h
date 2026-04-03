#pragma once
#include <string>
#include <mutex>
#include <fstream>
#include <functional>
#include <cstdint>

enum class WalOp : uint8_t {
    ADD = 1,
    REMOVE = 2,
    REINDEX = 3
};

class WAL {
public:
    WAL() = default;
    ~WAL();

    bool open(const std::string &path);

    void appendAdd(const std::string &path);
    void appendRemove(const std::string &path);
    void appendReindex();

    bool replay(std::function<void(WalOp, const std::string&)> handler);

private:
    std::string path;
    std::ofstream ofs;
    std::mutex mu;
};
