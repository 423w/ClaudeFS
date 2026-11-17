// audio_device.cpp: miniaudio glue. The only file in the host that sees miniaudio's C API.
#include "audio_device.h"

#include <miniaudio.h>

namespace fss::standalone {

struct AudioDevice::Impl {
    ma_device device{};
    bool initialized = false;
    RenderCallback* callback = nullptr;

    // The C callback miniaudio calls on its audio thread. It must be a plain function: C has no idea
    // what a lambda's captures or a member function's `this` are. (That's the L2.1 C++ question: a
    // captureless lambda converts to a function pointer because it has no state; a capturing one is an
    // object with data members, and a bare function pointer has nowhere to put them.) The state travels
    // instead through miniaudio's `void* pUserData`, which we cast back to our Impl.
    static void dataCallback(ma_device* device, void* output, const void* /*input*/, ma_uint32 frames)
    {
        auto* self = static_cast<Impl*>(device->pUserData);
        // No null checks, logging or try/catch here: render() is noexcept, and anything that could
        // block or allocate (printf, std::string, a mutex) would risk missing the deadline.
        self->callback->render(static_cast<float*>(output), frames, device->playback.channels);
    }
};

AudioDevice::AudioDevice() = default;

AudioDevice::~AudioDevice()
{
    close();
}

bool AudioDevice::open(const DeviceConfig& config, RenderCallback& callback)
{
    close();
    impl_ = std::make_unique<Impl>(); // heap, so the ma_device has a stable address the driver can keep
    impl_->callback = &callback;

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;  // float32: what the DSP produces; no int16 conversion on our side
    cfg.playback.channels = config.channels;
    cfg.sampleRate = config.sampleRateHz; // if the device runs at another rate, miniaudio resamples for us
    cfg.periodSizeInFrames = config.periodFrames;
    cfg.performanceProfile = ma_performance_profile_low_latency;
    // Exclusive mode (WASAPI): bypass the Windows mixer and talk to the device directly. Smaller periods and
    // lower latency become possible, but the device must support our format/rate and no other app can play.
    cfg.playback.shareMode = config.exclusive ? ma_share_mode_exclusive : ma_share_mode_shared;
    cfg.noFixedSizedCallback = config.fixedSizeCallback ? MA_FALSE : MA_TRUE;
    // Pre-silence the buffer, and don't clip: we want to *see* overs in our own code, not hide them.
    cfg.noPreSilencedOutputBuffer = MA_FALSE;
    cfg.noClip = MA_TRUE;
    cfg.dataCallback = &Impl::dataCallback;
    cfg.pUserData = impl_.get();

    // nullptr context: miniaudio picks the best backend in its priority order (WASAPI first on Windows),
    // shared mode, default device.
    const ma_result r = ma_device_init(nullptr, &cfg, &impl_->device);
    if (r != MA_SUCCESS) {
        lastError_ = std::string("ma_device_init failed: ") + ma_result_description(r);
        impl_.reset();
        return false;
    }
    impl_->initialized = true;

    const ma_device& d = impl_->device;
    info_ = {};
    info_.backendName = ma_get_backend_name(d.pContext->backend);
    char name[256] = {};
    if (ma_device_get_name(&impl_->device, ma_device_type_playback, name, sizeof(name), nullptr) == MA_SUCCESS) {
        info_.deviceName = name;
    }
    info_.sampleRateHz = d.sampleRate;
    info_.channels = d.playback.channels;
    info_.internalSampleRateHz = d.playback.internalSampleRate;
    info_.internalPeriodFrames = d.playback.internalPeriodSizeInFrames;
    info_.internalPeriods = d.playback.internalPeriods;
    return true;
}

bool AudioDevice::start()
{
    if (!impl_) {
        lastError_ = "start() before a successful open()";
        return false;
    }
    const ma_result r = ma_device_start(&impl_->device);
    if (r != MA_SUCCESS) {
        lastError_ = std::string("ma_device_start failed: ") + ma_result_description(r);
        return false;
    }
    return true;
}

void AudioDevice::stop()
{
    if (impl_) {
        // Synchronous: when this returns, the audio thread is no longer inside render(), so the main
        // thread may read whatever render() wrote without further synchronization.
        ma_device_stop(&impl_->device);
    }
}

void AudioDevice::close()
{
    if (impl_ && impl_->initialized) {
        ma_device_uninit(&impl_->device); // stops first if still running
    }
    impl_.reset();
}

} // namespace fss::standalone
