// trace.cpp: see trace.h. Plain C stdio on purpose: no allocation that could surprise anyone, and it works
// the same in every host. Thread-safety: several plugin instances may log from the host's main thread; it's
// one thread, so a static FILE* with fflush after each line is enough.
#include "trace.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <share.h> // _SH_DENYNO
#include <windows.h>
#endif

namespace fss::plugin {

namespace {

std::FILE* openLog() noexcept
{
#ifdef _WIN32
    // _wdupenv_s/_wfopen: the path may contain non-ASCII characters (a user name like "José").
    wchar_t* value = nullptr;
    std::size_t len = 0;
    if (_wdupenv_s(&value, &len, L"CFS_CLAP_TRACE") != 0 || value == nullptr)
        return nullptr;
    std::FILE* f = nullptr;
    if (std::wcscmp(value, L"1") == 0) {
        wchar_t temp[MAX_PATH + 1]{};
        const DWORD n = GetTempPathW(MAX_PATH, temp);
        if (n > 0 && n < MAX_PATH - 20) {
            wcscat_s(temp, L"cfs_clap_trace.txt");
            f = _wfsopen(temp, L"a", _SH_DENYNO); // shared: several processes (validator jobs) may log at once
        }
    } else {
        f = _wfsopen(value, L"a", _SH_DENYNO);
    }
    std::free(value);
    return f;
#else
    const char* value = std::getenv("CFS_CLAP_TRACE");
    if (!value)
        return nullptr;
    return std::fopen(std::strcmp(value, "1") == 0 ? "/tmp/cfs_clap_trace.txt" : value, "a");
#endif
}

std::FILE* logFile() noexcept
{
    static std::FILE* f = openLog(); // C++11 "magic static": initialized once, thread-safe
    return f;
}

unsigned long threadId() noexcept
{
#ifdef _WIN32
    return GetCurrentThreadId();
#else
    return 0;
#endif
}

} // namespace

bool Trace::enabled() noexcept { return logFile() != nullptr; }

void Trace::log(const void* instance, const char* fmt, ...) noexcept
{
    std::FILE* f = logFile();
    if (!f)
        return;
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    const auto ms = duration_cast<milliseconds>(steady_clock::now() - t0).count();
    std::fprintf(f, "%8lld ms  thread %5lu  %p  ", static_cast<long long>(ms), threadId(), instance);
    va_list args;
    va_start(args, fmt);
    std::vfprintf(f, fmt, args);
    va_end(args);
    std::fputc('\n', f);
    std::fflush(f); // a host that crashes later must not lose the lines that led up to it
}

void Trace::logStats(const void* instance, const AudioStats& s) noexcept
{
    if (!enabled())
        return;
    log(instance,
        "  audio thread summary: process %llu calls (%llu with 0 frames, %llu without buffers), frames %u..%u, "
        "flush %llu; events in: note on %llu, off %llu, choke %llu, expression %llu, midi %llu, param %llu, "
        "param mod %llu, transport %llu, other %llu; NOTE_END out %llu; event-list splits %llu; "
        "tempo %.3f -> %.3f BPM (%llu changes); restart requests %llu",
        static_cast<unsigned long long>(s.processCalls), static_cast<unsigned long long>(s.zeroFrameCalls),
        static_cast<unsigned long long>(s.noBuffers), s.processCalls ? s.minFrames : 0u, s.maxFrames,
        static_cast<unsigned long long>(s.flushCalls), static_cast<unsigned long long>(s.noteOn),
        static_cast<unsigned long long>(s.noteOff), static_cast<unsigned long long>(s.noteChoke),
        static_cast<unsigned long long>(s.noteExpression), static_cast<unsigned long long>(s.midi),
        static_cast<unsigned long long>(s.paramValue), static_cast<unsigned long long>(s.paramMod),
        static_cast<unsigned long long>(s.transport), static_cast<unsigned long long>(s.other),
        static_cast<unsigned long long>(s.noteEndSent), static_cast<unsigned long long>(s.eventChunks), s.firstTempo,
        s.lastTempo, static_cast<unsigned long long>(s.tempoChanges), static_cast<unsigned long long>(s.restartRequests));
}

} // namespace fss::plugin
