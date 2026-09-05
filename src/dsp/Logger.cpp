#include "dsp/Logger.h"

#include <chrono>

namespace cold::dsp
{

Logger::Logger() : startMs(nowMs()) {}

Logger& Logger::instance()
{
    static Logger logger; // Meyers singleton: thread-safe initialisation.
    return logger;
}

std::int64_t Logger::nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void Logger::log(const std::string& message)
{
    const std::lock_guard<std::mutex> lock(mutex);
    const auto ms = nowMs() - startMs;
    std::string line = "[+" + std::to_string(ms) + "ms #" + std::to_string(seq) + "] " + message;
    ++seq;
    lines.push_back(std::move(line));
    while (lines.size() > cap)
        lines.pop_front();
}

std::vector<std::string> Logger::snapshot() const
{
    const std::lock_guard<std::mutex> lock(mutex);
    return {lines.begin(), lines.end()};
}

std::uint64_t Logger::sequence() const
{
    const std::lock_guard<std::mutex> lock(mutex);
    return seq;
}

void Logger::clear()
{
    const std::lock_guard<std::mutex> lock(mutex);
    lines.clear();
}

void Logger::setCapacity(std::size_t capacity)
{
    const std::lock_guard<std::mutex> lock(mutex);
    cap = capacity == 0 ? 1 : capacity;
    while (lines.size() > cap)
        lines.pop_front();
}

} // namespace cold::dsp
