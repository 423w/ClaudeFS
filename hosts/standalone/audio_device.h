// audio_device.h: a thin RAII wrapper over a miniaudio playback device (L2.1, D9).
//
// The pull model: we don't push audio to the sound card, the sound card *pulls* it. A driver thread we
// didn't create wakes up every period (periodFrames / sampleRate seconds, e.g. 256 / 48000 = 5.3 ms)
// and calls render() to fill the next buffer. If render() hasn't returned by the time the hardware needs
// that buffer, the hardware plays whatever is there (silence or stale samples): a click. That deadline is
// what every real-time rule in Branch 2 protects.
//
// Why a wrapper at all: miniaudio is a C library with an init/uninit pair. Forget ma_device_uninit() on an
// early return and the device (and its thread) leaks. RAII ties it to a C++ object's lifetime, and the
// header doesn't include miniaudio.h, so the rest of the host never sees C types (pimpl).
#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace fss::standalone {

/// What the host asks for. The backend may grant something else; see DeviceInfo.
struct DeviceConfig {
    std::uint32_t sampleRateHz = 48000;
    std::uint32_t periodFrames = 256;    // frames per callback requested from the backend
    std::uint32_t channels = 2;          // stereo, interleaved L R L R ...
    // true: miniaudio buffers internally so render() always gets exactly the granted period size
    // (easier DSP, but up to one more period of latency). false: render() gets whatever the backend
    // delivers, which can vary from callback to callback (see the L2.1 [THINK]).
    bool fixedSizeCallback = true;
    // WASAPI exclusive mode: lower latency possible, device must accept the format; may fail to open.
    bool exclusive = false;
};

/// What the backend actually granted. Never assume this equals DeviceConfig.
struct DeviceInfo {
    std::string backendName;             // "WASAPI", "DirectSound", ...
    std::string deviceName;
    std::uint32_t sampleRateHz = 0;      // rate render() runs at (what we asked for; miniaudio resamples)
    std::uint32_t channels = 0;
    std::uint32_t internalSampleRateHz = 0; // the device's own rate (shared-mode WASAPI: the Windows mixer's)
    std::uint32_t internalPeriodFrames = 0; // the backend's real period, at the internal rate
    std::uint32_t internalPeriods = 0;      // how many periods the backend buffers (double/triple buffering)

    /// Lower bound on output latency from buffering alone: all queued periods must play before a new
    /// sample is heard. Driver, DAC and Windows mixer delays come on top and aren't reported by the API.
    [[nodiscard]] double bufferLatencyMs() const noexcept
    {
        if (internalSampleRateHz == 0) {
            return 0.0;
        }
        return 1000.0 * internalPeriodFrames * internalPeriods / internalSampleRateHz;
    }
};

/// The audio-thread side. render() runs on the driver's real-time thread, so implementations must obey
/// the real-time rules: no allocation, no locks, no I/O (printf included), no exceptions, bounded time.
/// An abstract class instead of std::function: std::function may allocate when constructed, adds an
/// indirection, and can't be noexcept-qualified. A virtual call per *block* (not per sample) costs nothing.
class RenderCallback {
public:
    /// Fill `frames` frames of interleaved float32 audio (frames * channels samples, range about -1..1).
    virtual void render(float* interleaved, std::uint32_t frames, std::uint32_t channels) noexcept = 0;

protected:
    // Protected non-virtual destructor: nobody deletes a RenderCallback through this interface.
    ~RenderCallback() = default;
};

class AudioDevice {
public:
    AudioDevice();
    ~AudioDevice();            // stops and closes the device
    AudioDevice(const AudioDevice&) = delete;            // one OS device handle, one owner
    AudioDevice& operator=(const AudioDevice&) = delete;
    AudioDevice(AudioDevice&&) = delete;                 // the driver holds a pointer to us: we must not move
    AudioDevice& operator=(AudioDevice&&) = delete;

    /// Open the default playback device. `callback` must outlive the device. Returns false and sets
    /// lastError() on failure (no device, unsupported format...). Allocates: call from the main thread.
    bool open(const DeviceConfig& config, RenderCallback& callback);
    bool start();              // the driver starts calling render() on its own thread
    void stop();               // blocks until the audio thread has left render() for good
    void close();

    [[nodiscard]] const DeviceInfo& info() const noexcept { return info_; }
    [[nodiscard]] const std::string& lastError() const noexcept { return lastError_; }

private:
    struct Impl;                      // holds the ma_device; defined in audio_device.cpp
    std::unique_ptr<Impl> impl_;
    DeviceInfo info_;
    std::string lastError_;
};

} // namespace fss::standalone
