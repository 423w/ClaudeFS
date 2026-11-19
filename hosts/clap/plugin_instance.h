// plugin_instance.h: FssClap, one CFS plugin instance (L3.2). Private to hosts/clap: hosts only ever see the
// clap_plugin struct inside it, through plugin.h.
//
// THREADS (docs/PLUGIN-LIFECYCLE.md §2). Every member is owned by one role:
//   [main]   mirror_, gesture_, active_, host extension pointers: only touched by main-thread calls
//   [audio]  engine_ (while active), events_, snap_, stats_: only touched by process()/flush()/reset()
//            (while INACTIVE there is no audio thread, and the main thread owns engine_ too)
//   [shared] the queue and the atomics below, each with exactly one writer
//
// PARAMETER SYNC, without locks:
//   host automation  → process() → Engine, then fromAudio_[i] + fromAudioDirty_[i]  → main thread mirror_
//   GUI / main edits → mirror_ + mainValues_[i], and a UiMsg in toAudio_ → process()/flush() → Engine, and
//                      out_events to the host (GESTURE_BEGIN, PARAM_VALUE, GESTURE_END)
// The audio side is the truth: every value it applies (from the host OR the GUI) is published back, and the
// main thread's mirror converges to it, except while the user is dragging that very parameter (gesture_) or
// editor values for it are still in flight (uiPending_), so a knob never jitters or snaps back under the mouse.
// If toAudio_ is ever full, nothing is lost: resync_ tells the audio thread to compare every parameter with
// mainValues_ (the main thread's latest values) and apply the differences.
#pragma once

#include "clap_params.h"
#include "editor_ui.h"
#include "trace.h"

#include <clap/clap.h>
#include <fss/engine.h>
#include <fss/event.h>
#include <fss/synth/patch.h>
#include <fss/util/spsc_queue.h>

#include <array>
#include <atomic>
#include <bitset>
#include <cstdint>
#include <memory>
#include <string>

namespace fss::plugin {

class EditorWindow;

class FssClap final : public EditorModel {
public:
    explicit FssClap(const clap_host* host);
    ~FssClap() override;

    clap_plugin plugin_{}; // what the host holds; plugin_data points back at this object
    [[nodiscard]] static FssClap* self(const clap_plugin* p) noexcept { return static_cast<FssClap*>(p->plugin_data); }

    // ---- clap_plugin ([main] unless noted) ---------------------------------------------------------
    bool init();
    void destroy();
    bool activate(double sampleRate, std::uint32_t minFrames, std::uint32_t maxFrames);
    void deactivate();
    bool startProcessing() noexcept { return true; } // [audio] nothing to start: activate prepared everything
    void stopProcessing() noexcept {}                // [audio]
    void reset() noexcept;                            // [audio] kill all voices, keep parameter values
    clap_process_status process(const clap_process* p) noexcept; // [audio]
    [[nodiscard]] const void* getExtension(const char* id) const noexcept; // [thread-safe]

    // ---- clap.params -------------------------------------------------------------------------------
    bool paramValue(clap_id id, double* out);
    bool paramValueToText(clap_id id, double value, char* out, std::uint32_t size) const;
    bool paramTextToValue(clap_id id, const char* text, double* out) const;
    void paramsFlush(const clap_input_events* in, const clap_output_events* out) noexcept; // [active ? audio : main]

    // ---- clap.state --------------------------------------------------------------------------------
    bool stateSave(const clap_ostream* stream);
    bool stateLoad(const clap_istream* stream);

    // ---- the editor's (and tests') side of the parameter sync [main] ---------------------------------
    /// A user gesture on parameter `index` (kParams order): begin, any number of sets, end.
    void uiBegin(std::size_t index);
    void uiSet(std::size_t index, float plain);
    void uiEnd(std::size_t index);
    /// The main thread's view of the patch, updated from the audio thread first. What the GUI draws.
    const synth::Patch& mainPatch();
    /// Replace the whole patch from the main thread (state load, presets): no gestures, no host recording.
    void applyPatchFromMain(const synth::Patch& patch);
    [[nodiscard]] const std::string& presetName() const noexcept { return presetName_; }
    void setPresetName(std::string name) { presetName_ = std::move(name); }
    [[nodiscard]] float meterPeak() const noexcept { return peak_.load(std::memory_order_relaxed); }
    [[nodiscard]] int meterVoices() const noexcept { return voices_.load(std::memory_order_relaxed); }

    // ---- clap.tail (L5.1) [main, audio] ----------------------------------------------------------------
    /// The FX chain's tail in samples, as of the last block (an atomic: the host may ask from either thread).
    [[nodiscard]] std::uint32_t tailSamples() const noexcept { return tail_.load(std::memory_order_relaxed); }

    // ---- clap.latency (L5.2) [main] ------------------------------------------------------------------------
    /// The output's delay in samples, as of the last activate (only an activate may change it).
    [[nodiscard]] std::uint32_t latencySamples() const noexcept { return latency_.load(std::memory_order_relaxed); }

    // ---- clap.gui (L3.5) [main] -------------------------------------------------------------------------
    bool guiCreate(const char* api, bool isFloating);
    void guiDestroy();
    bool guiSetScale(double scale);
    bool guiGetSize(std::uint32_t* width, std::uint32_t* height);
    bool guiAdjustSize(std::uint32_t* width, std::uint32_t* height);
    bool guiSetSize(std::uint32_t width, std::uint32_t height);
    bool guiSetParent(const clap_window* window);
    bool guiShow();
    bool guiHide();
    [[nodiscard]] EditorWindow* editorWindow() noexcept { return editor_.get(); } // tests / debug exports

    // ---- EditorModel: what the editor sees of the plugin [main] ------------------------------------------
    const synth::Patch& editorPatch() override { return mainPatch(); }
    void beginEdit(std::size_t index) override { uiBegin(index); }
    void setEdit(std::size_t index, float plain) override { uiSet(index, plain); }
    void endEdit(std::size_t index) override { uiEnd(index); }
    void loadPreset(const synth::Patch& patch, const std::string& name) override;
    const std::string& currentPresetName() override { return presetName_; }
    float peakLevel() override { return meterPeak(); }
    int activeVoices() override { return meterVoices(); }

private:
    struct UiMsg {
        enum class Kind : std::uint8_t { Begin, Value, End } kind = Kind::Value;
        bool notifyHost = true;  // false for state loads: the host already knows (it asked us to load)
        std::uint16_t index = 0; // into kParams
        float plain = 0.0f;
    };

    // One audio block (or a flush: frames = 0, no buffers). Shared by process() and paramsFlush().
    void runBlock(const clap_input_events* in, const clap_output_events* out, std::uint32_t frames, float* outL,
                  float* outR) noexcept;
    bool drainUi(const clap_output_events* out) noexcept; // true = the queue is now empty
    void resyncFromMain(const clap_output_events* out) noexcept;
    void translate(const clap_event_header* h, std::uint32_t frame, std::uint32_t blockTime,
                   const clap_output_events* out) noexcept;
    void applyParam(std::size_t index, float plain, std::uint32_t frame) noexcept;
    void renderEngine(float* outL, float* outR, std::uint32_t frames) noexcept;
    void reportNoteEnds(const clap_output_events* out, std::uint32_t frames) noexcept;
    void tempoEvent(double bpm, std::uint32_t frame) noexcept; // L5.3: a changed host tempo → Event::Type::Tempo
    void pollAudio(); // [main] fold fromAudio_ into mirror_
    void pushUi(const UiMsg& m);

    // [main]
    const clap_host* host_;
    const clap_host_params* hostParams_ = nullptr;
    const clap_host_log* hostLog_ = nullptr;
    const clap_host_state* hostState_ = nullptr;
    const clap_host_tail* hostTail_ = nullptr; // L5.1: "my tail changed" (called from the audio thread)
    const clap_host_latency* hostLatency_ = nullptr; // L5.2: "my latency changed" (only inside activate)
    std::unique_ptr<EditorWindow> editor_;
    synth::Patch mirror_{};
    std::string presetName_ = "Init";      // saved with the state; shown by the editor (L3.5)
    std::array<bool, kNumParams> gesture_{};
    bool active_ = false;

    // [audio] (and [main] while inactive)
    Engine engine_;
    static constexpr std::size_t kMaxEvents = 1024; // fixed: process() never grows anything
    static constexpr std::size_t kSplitAt = kMaxEvents - 64; // room for one CLAP event's worth of fss::Events
    std::array<Event, kMaxEvents> events_{};
    std::size_t numEvents_ = 0;
    double blockTempo_ = 0.0; // [audio] this block's clap_process.transport tempo (0 = none)
    double lastTempo_ = 0.0;  // [audio] the tempo the Engine last got from us
    std::bitset<kNumParams> touched_; // parameters applied this block, to publish back to the main thread
    static constexpr std::uint32_t kScratchFrames = 512;
    std::array<float, kScratchFrames> scratchL_{}, scratchR_{}; // when the host gives us no (or 1) channel
    struct VoiceSnap {
        bool busy = false;
        std::uint64_t age = 0; // a voice slot's note identity (Voice::age is unique per started note)
        std::int32_t noteId = -1;
        std::int16_t channel = -1;
        std::uint8_t key = 0;
    };
    std::array<VoiceSnap, Engine::kNumVoices> snap_{};
    std::array<std::int16_t, 128> lastChannel_{}; // channel of the latest note-on per key, for NOTE_END
    AudioStats stats_;

    // [shared]
    util::SpscQueue<UiMsg, 1024> toAudio_;               // main → audio
    std::atomic<bool> resync_{false};                     // main → audio: "toAudio_ overflowed, compare all"
    std::array<std::atomic<float>, kNumParams> mainValues_{};   // main writes, audio reads (resync only)
    std::array<std::atomic<float>, kNumParams> fromAudio_{};    // audio writes, main reads
    std::array<std::atomic<bool>, kNumParams> fromAudioDirty_{};
    std::array<std::atomic<std::uint32_t>, kNumParams> uiPending_{}; // editor values queued, not yet applied
    std::atomic<float> peak_{0.0f};
    std::atomic<int> voices_{0};
    std::atomic<std::uint32_t> tail_{0}; // audio writes (end of each block), any thread reads
    std::atomic<std::uint32_t> latency_{0};        // main writes (activate), main reads (latency.get)
    std::atomic<bool> restartRequested_{false};    // audio sets (request_restart sent), main clears (activate)
};

} // namespace fss::plugin
