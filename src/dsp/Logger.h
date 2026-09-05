#pragma once

// Framework-agnostic (no JUCE) in-memory debug log ring.
//
// A process-wide, thread-safe ring buffer of recent log lines, surfaced by the
// plugin's "Logs" editor tab so hard-to-reproduce UI issues (a click that stops
// registering after another plugin window opens, say) can be traced without an
// external console. Each entry is stamped with a millisecond offset and a
// sequence number.
//
// THREAD SAFETY / REAL-TIME: log() takes a mutex and allocates, so it must be
// called only from non-realtime threads (the message thread). Never call it from
// the audio callback.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace cold::dsp
{

class Logger
{
public:
    // The single shared logger.
    static Logger& instance();

    // Append a line (stamped "[+<ms>ms #<seq>] <message>"). Thread-safe. Oldest
    // lines are dropped once the ring is full.
    void log(const std::string& message);

    // Every retained line, oldest first.
    [[nodiscard]] std::vector<std::string> snapshot() const;

    // Number of lines ever logged (not just retained) — lets a poller tell when
    // something new arrived without copying the whole ring.
    [[nodiscard]] std::uint64_t sequence() const;

    void clear();

    // Max retained lines (default 500). Applied on the next log().
    void setCapacity(std::size_t capacity);

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger();

    mutable std::mutex mutex;
    std::deque<std::string> lines;
    std::size_t cap = 500;
    std::uint64_t seq = 0;
    std::int64_t startMs = 0;

    [[nodiscard]] static std::int64_t nowMs();
};

} // namespace cold::dsp
