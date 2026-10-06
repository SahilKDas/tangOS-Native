#pragma once
#include "core.h"
#include <atomic>
#include <functional>
namespace lite {
struct Result { unsigned long code; std::string output; };
using Sink = std::function<void(const std::string&)>;
class Runner {
    std::atomic<bool> cancelled{false};
public:
    void cancel(){cancelled=true;}
    void reset(){cancelled=false;}
    bool isCancelled()const{return cancelled;}
    Result run(const Command& c, const Sink& sink={}, const fs::path& log={});
};
fs::path localData();
std::string uniqueId();
std::string sha256File(const fs::path& path);
std::string selfExecutable();
}
