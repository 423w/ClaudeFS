// trace.h: an optional log of what the host asks the plugin to do (L3.2), so the owner can check the L3.1
// sequence diagram against what FL Studio really does.
//
// OFF unless the environment variable CFS_CLAP_TRACE is set when the host starts:
//   CFS_CLAP_TRACE=1                    log to %TEMP%\cfs_clap_trace.txt
//   CFS_CLAP_TRACE=C:\path\to\log.txt   log to that file
// Main-thread calls are written as they happen (with a millisecond timestamp and the OS thread id).
// The audio thread never touches the file: process() only bumps counters in AudioStats, and the main
// thread writes a summary at deactivate() (see plugin.cpp).
#pragma once

#include <cstdint>

namespace fss::plugin {

/// Counters the audio thread updates (plain fields: only the audio thread writes them, and the main thread
/// reads them only after the host has stopped processing, so the host's own ordering keeps it race-free).
struct AudioStats {
    std::uint64_t processCalls = 0, zeroFrameCalls = 0, flushCalls = 0;
    std::uint32_t minFrames = 0xFFFFFFFFu, maxFrames = 0;
    std::uint64_t noteOn = 0, noteOff = 0, noteChoke = 0, noteExpression = 0, midi = 0, paramValue = 0,
                  paramMod = 0, transport = 0, other = 0, noteEndSent = 0, noBuffers = 0, eventChunks = 0;
};

class Trace {
public:
    /// Reads CFS_CLAP_TRACE once (first call). Cheap to call again.
    static bool enabled() noexcept;
    /// printf-style line, prefixed with time, thread id and the instance pointer. Main thread only.
    static void log(const void* instance, const char* fmt, ...) noexcept;
    static void logStats(const void* instance, const AudioStats& s) noexcept;
};

} // namespace fss::plugin
