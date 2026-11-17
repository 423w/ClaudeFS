// fss_play (L2.4, milestone M3): the synth you can play and tweak.
//
//   fss_play [--exclusive] [--period FRAMES] [--frames N] [--demo]
//
//   - Audio: the default output device (WASAPI; --exclusive --period 64 for ~6 ms latency, see L2.1).
//   - Notes: mouse on the on-screen piano, the computer keyboard (Z..M / Q..U rows), and EVERY MIDI input
//     device connected at startup.
//   - Sound: every parameter in the param table, as sliders generated from it; preset buttons; MIDI CCs
//     mapped through the table (CC 74 = op A level, CC 71 = op C level, CC 1 = op A feedback, CC 7 = output).
//   - --frames N closes the window after N frames (automated smoke tests).
//
// Threads:
//   GUI (main) thread ── uiEvents_ (SPSC) ──► audio thread: notes from mouse/QWERTY, ParamChange from sliders
//   WinMM thread(s)  ── per-device SPSC ──► audio thread: raw MIDI (notes, bend, CCs)
//   audio thread     ── toGui_ (SPSC) ───► GUI thread: CC-driven parameter values (so sliders follow your
//                                           hardware knobs) and MIDI-held keys (piano highlighting)
//   audio thread     ── atomics ─────────► GUI thread: meters (peak, voices)
// The GUI keeps its own copy of the Patch: it's the "what the knobs show" state. The Engine's copy is the
// "what you hear" state; ParamChange events keep the two in step without either thread touching the other.
#include "audio_device.h"
#include "dev_ui.h"
#include "keyboard.h"
#include "midi_input_winmm.h"
#include "midi_stream.h"

#include <fss/engine.h>
#include <fss/synth/params.h>
#include <fss/synth/presets.h>
#include <fss/util/math.h>
#include <fss/util/realtime_guard.h>
#include <fss/util/spsc_queue.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fss::play {
namespace {

constexpr int kMaxBlock = 1024;
constexpr std::size_t kMaxMidiInputs = 4;

/// Audio thread → GUI thread messages.
struct GuiMessage {
    enum class Kind : std::uint8_t { Param, KeyDown, KeyUp } kind;
    std::uint32_t id;  // param id (Param) or key (KeyDown/KeyUp)
    float value;
};

class PlayHost final : public standalone::RenderCallback, public devui::NoteSink {
public:
    void prepare(double sampleRate, const synth::Patch& patch)
    {
        engine_.prepare(sampleRate, kMaxBlock);
        engine_.setPatch(patch); // audio not running yet: a direct call is safe
    }

    /// Open every MIDI input that exists right now (main thread, before audio starts).
    void openMidiInputs()
    {
        const auto names = midi::MidiInputWinmm::listDevices();
        for (std::size_t i = 0; i < names.size() && midiCount_ < kMaxMidiInputs; ++i) {
            std::string err;
            if (midi_[midiCount_].open(static_cast<unsigned>(i), err)) {
                midiNames_.push_back(names[i]);
                ++midiCount_;
            } else {
                std::fprintf(stderr, "MIDI input %zu (%s) not opened: %s\n", i, names[i].c_str(), err.c_str());
            }
        }
    }
    void closeMidiInputs() noexcept
    {
        for (std::size_t i = 0; i < midiCount_; ++i)
            midi_[i].close();
    }
    [[nodiscard]] const std::vector<std::string>& midiNames() const { return midiNames_; }

    // ---- GUI thread -------------------------------------------------------------------------------
    bool post(const Event& e) noexcept
    {
        const bool ok = uiEvents_.push(e);
        if (!ok)
            ++droppedUi_;
        return ok;
    }
    void noteOn(std::uint8_t key, float velocity) override { post(Event::noteOn(0, key, velocity)); }
    void noteOff(std::uint8_t key) override { post(Event::noteOff(0, key)); }
    bool popGui(GuiMessage& m) noexcept { return toGui_.pop(m); }
    [[nodiscard]] float peak() const noexcept { return peak_.load(std::memory_order_relaxed); }
    [[nodiscard]] int voices() const noexcept { return voices_.load(std::memory_order_relaxed); }
    [[nodiscard]] float load() const noexcept { return load_.load(std::memory_order_relaxed); }
    [[nodiscard]] int droppedUi() const noexcept { return droppedUi_; }

    // ---- audio thread -----------------------------------------------------------------------------
    void render(float* out, std::uint32_t frames, std::uint32_t channels) noexcept override
    {
        const util::RealtimeGuard rt;
        const auto t0 = std::chrono::steady_clock::now();

        std::size_t n = 0;
        Event e;
        while (n < events_.size() && uiEvents_.pop(e))
            events_[n++] = e;
        for (std::size_t d = 0; d < midiCount_; ++d) {
            midi::MidiMessage m;
            while (n < events_.size() && midi_[d].pop(m))
                n = handleMidi(midi::classify(m), n);
        }

        float peak = 0.0f;
        std::uint32_t done = 0;
        while (done < frames) {
            const auto chunk = static_cast<int>(std::min<std::uint32_t>(frames - done, kMaxBlock));
            engine_.process(std::span<const Event>(events_.data(), done == 0 ? n : 0), left_.data(), right_.data(), chunk);
            for (int i = 0; i < chunk; ++i) {
                float* f = out + (done + static_cast<std::uint32_t>(i)) * channels;
                f[0] = left_[static_cast<std::size_t>(i)];
                for (std::uint32_t c = 1; c < channels; ++c)
                    f[c] = right_[static_cast<std::size_t>(i)];
                peak = std::max(peak, std::abs(left_[static_cast<std::size_t>(i)]));
            }
            done += static_cast<std::uint32_t>(chunk);
        }

        // Meters for the GUI. Peak "decays" on the GUI side by taking the max with the previous value.
        peak_.store(std::max(peak, peak_.load(std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed);
        voices_.store(engine_.busyVoices(), std::memory_order_relaxed);
        const double budget = static_cast<double>(frames) / sampleRateGuess_;
        const double used = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        load_.store(static_cast<float>(used / budget), std::memory_order_relaxed);
    }

    void setSampleRate(double sr) noexcept { sampleRateGuess_ = sr; }

private:
    /// MIDI → Engine events (+ GUI notifications). Returns the new event count.
    std::size_t handleMidi(const midi::DecodedMidi& d, std::size_t n) noexcept
    {
        Event e;
        if (d.kind == midi::MidiKind::ControlChange) {
            // CC → every parameter mapped to it in the table. Value 0..127 → normalized → plain.
            for (const synth::ParamInfo& p : synth::kParams) {
                if (p.midiCC != d.number || n >= events_.size())
                    continue;
                const float plain = synth::fromNormalized(p, static_cast<float>(d.value) / 127.0f);
                events_[n++] = Event::paramChange(0, p.id, plain);
                (void)toGui_.push({GuiMessage::Kind::Param, p.id, plain}); // if full, the slider lags; fine
            }
        }
        if (midi::toEvent(d, 0, e) && n < events_.size()) {
            events_[n++] = e;
            if (e.type == Event::Type::NoteOn || e.type == Event::Type::NoteOff)
                (void)toGui_.push({e.type == Event::Type::NoteOn ? GuiMessage::Kind::KeyDown : GuiMessage::Kind::KeyUp, e.key, 0.0f});
        }
        return n;
    }

    Engine engine_;
    std::array<float, kMaxBlock> left_{}, right_{};
    std::array<Event, 256> events_{};
    util::SpscQueue<Event, 1024> uiEvents_;      // GUI → audio (a preset load sends ~56 events at once)
    util::SpscQueue<GuiMessage, 1024> toGui_;    // audio → GUI
    std::array<midi::MidiInputWinmm, kMaxMidiInputs> midi_{};
    std::size_t midiCount_ = 0;                  // fixed before audio starts, so a plain size_t is fine
    std::vector<std::string> midiNames_;         // GUI thread only
    std::atomic<float> peak_{0.0f}, load_{0.0f};
    std::atomic<int> voices_{0};
    static_assert(std::atomic<float>::is_always_lock_free);
    int droppedUi_ = 0;                          // GUI thread only
    double sampleRateGuess_ = 48000.0;
};

// ---- GUI ------------------------------------------------------------------------------------------

/// Lay panels out as fractions of the window on first use, so the layout fits any size and DPI (the user
/// can still move and resize them afterwards).
void placePanel(float x, float y, float w, float h)
{
    const ImVec2 d = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(x * d.x, y * d.y), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(w * d.x, h * d.y), ImGuiCond_FirstUseEver);
}

/// Display a plain value with sensible precision and its unit.
void formatValue(char* buf, std::size_t size, const synth::ParamInfo& p, float v)
{
    const float a = std::abs(v);
    const char* fmt = a >= 1000.0f ? "%.0f %.*s" : a >= 100.0f ? "%.1f %.*s" : a >= 10.0f ? "%.2f %.*s" : "%.3f %.*s";
    std::snprintf(buf, size, fmt, static_cast<double>(v), static_cast<int>(p.unit.size()), p.unit.data());
}

/// One slider, generated from the param table. Returns true if the user changed it.
/// Sliders move in *normalized* space, so every skew (Exp, Power2) feels right under the mouse.
bool paramWidget(const synth::ParamInfo& p, synth::Patch& patch, PlayHost& host)
{
    float plain = synth::getParam(patch, p);
    bool changed = false;
    ImGui::PushID(static_cast<int>(p.id));
    if (p.skew == synth::Skew::Stepped && p.max - p.min == 1.0f) {
        bool on = plain >= 0.5f;
        if (ImGui::Checkbox("##v", &on)) {
            plain = on ? 1.0f : 0.0f;
            changed = true;
        }
    } else if (p.skew == synth::Skew::Stepped) {
        int v = static_cast<int>(plain);
        if (ImGui::SliderInt("##v", &v, static_cast<int>(p.min), static_cast<int>(p.max))) {
            plain = static_cast<float>(v);
            changed = true;
        }
    } else {
        float n = synth::toNormalized(p, plain);
        char label[48];
        formatValue(label, sizeof label, p, plain); // no '%' in it: ImGui shows it verbatim
        if (ImGui::SliderFloat("##v", &n, 0.0f, 1.0f, label, ImGuiSliderFlags_NoInput)) {
            plain = synth::fromNormalized(p, n);
            changed = true;
        }
    }
    // Right-click: back to the default.
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        plain = p.def;
        changed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%.*s  (id %u)%s\nright-click: default", static_cast<int>(p.name.size()), p.name.data(), p.id,
                          p.midiCC ? "  [MIDI CC mapped]" : "");
    ImGui::PopID();
    if (changed) {
        synth::setParam(patch, p, plain);
        host.post(Event::paramChange(0, p.id, synth::getParam(patch, p)));
    }
    return changed;
}

/// Send a whole patch as ParamChange events (presets). The Engine never sees setPatch while playing.
void loadPatch(synth::Patch& guiPatch, const synth::Patch& preset, PlayHost& host)
{
    guiPatch = preset;
    for (const synth::ParamInfo& p : synth::kParams)
        host.post(Event::paramChange(0, p.id, synth::getParam(guiPatch, p)));
}

void drawMain(synth::Patch& patch, PlayHost& host, const standalone::DeviceInfo& info, double latencyMs)
{
    placePanel(0.0f, 0.0f, 0.28f, 0.62f);
    ImGui::Begin("CFS");
    ImGui::TextDisabled("Claude's FirstStreetSynthesis - L2.4 dev surface");
    ImGui::Separator();

    ImGui::Text("Presets");
    if (ImGui::Button("E-piano"))
        loadPatch(patch, synth::presets::epiano(), host);
    ImGui::SameLine();
    if (ImGui::Button("FM bass"))
        loadPatch(patch, synth::presets::fmBass(), host);
    ImGui::SameLine();
    if (ImGui::Button("Init"))
        loadPatch(patch, synth::Patch{}, host);
    ImGui::SameLine();
    if (ImGui::Button("Panic"))
        host.post({Event::Type::AllNotesOff});

    ImGui::Separator();
    for (const synth::ParamInfo& p : synth::kParams) {
        if (p.op >= 0)
            continue;
        ImGui::Text("%-10.*s", static_cast<int>(p.name.size()), p.name.data());
        ImGui::SameLine(ImGui::GetFontSize() * 6.5f); // label column scales with font (= with DPI)
        ImGui::SetNextItemWidth(-1);
        paramWidget(p, patch, host);
        if (p.id == synth::param_id::kAlgorithm)
            ImGui::TextDisabled("  %s", synth::kAlgorithms[static_cast<std::size_t>(patch.algorithmNumber - 1)].name);
    }

    ImGui::Separator();
    const float peakDb = util::gainToDb(host.peak());
    ImGui::Text("Output peak");
    ImGui::SameLine(ImGui::GetFontSize() * 6.5f);
    ImGui::ProgressBar(std::clamp((peakDb + 60.0f) / 60.0f, 0.0f, 1.0f), ImVec2(-1, 0),
                       peakDb > -59.0f ? (std::to_string(static_cast<int>(peakDb)) + " dB").c_str() : "-inf");
    ImGui::Text("Voices %2d / %d   CPU %4.1f %%", host.voices(), Engine::kMaxPolyphony, 100.0f * host.load());
    ImGui::TextDisabled("%s, %u Hz, %u x %u frames, ~%.1f ms", info.backendName.c_str(), info.sampleRateHz,
                        info.internalPeriodFrames, info.internalPeriods, latencyMs);
    if (host.midiNames().empty())
        ImGui::TextDisabled("MIDI: no input devices (connect one and restart)");
    for (const std::string& n : host.midiNames())
        ImGui::TextDisabled("MIDI in: %s", n.c_str());
    if (host.droppedUi() > 0)
        ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "dropped GUI events: %d", host.droppedUi());
    ImGui::End();
}

void drawOperators(synth::Patch& patch, PlayHost& host)
{
    placePanel(0.28f, 0.0f, 0.72f, 0.62f);
    ImGui::Begin("Operators");
    // One row per field, one column per operator: the grid is generated from the table, so a new
    // operator parameter appears here automatically.
    if (ImGui::BeginTable("ops", 1 + synth::kNumOps, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 6.0f);
        for (int op = 0; op < synth::kNumOps; ++op) {
            const synth::Algorithm& a = patch.algorithm;
            const bool carrier = a.carrier[static_cast<std::size_t>(op)] != 0.0f;
            static char headers[synth::kNumOps][24];
            std::snprintf(headers[op], sizeof headers[op], "Op %.*s %s", 1, synth::kOpNames[op].data(), carrier ? "(carrier)" : "(mod)");
            ImGui::TableSetupColumn(headers[op]);
        }
        ImGui::TableHeadersRow();
        for (const synth::detail::OpFieldInfo& f : synth::detail::kOpFields) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(f.name.data(), f.name.data() + f.name.size());
            for (int op = 0; op < synth::kNumOps; ++op) {
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                paramWidget(*synth::findParam(synth::param_id::op(op, f.field)), patch, host);
            }
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("Level is loudness on a carrier and brightness (modulation index) on a modulator.");
    ImGui::End();
}

template <typename T>
bool parseNumber(std::string_view text, T& out)
{
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    return ec == std::errc{} && ptr == text.data() + text.size();
}

int run(int argc, char** argv)
{
    standalone::DeviceConfig cfg;
    long frameLimit = -1;
    bool demo = false; // --demo: play a chord through the normal event path (smoke test without a human)
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--exclusive") {
            cfg.exclusive = true;
        } else if (a == "--period" && i + 1 < argc && parseNumber(argv[i + 1], cfg.periodFrames)) {
            ++i;
        } else if (a == "--frames" && i + 1 < argc && parseNumber(argv[i + 1], frameLimit)) {
            ++i;
        } else if (a == "--demo") {
            demo = true;
        } else {
            std::fprintf(stderr, "usage: fss_play [--exclusive] [--period FRAMES] [--frames N] [--demo]\n");
            return 2;
        }
    }

    auto host = std::make_unique<PlayHost>();
    standalone::AudioDevice device;
    if (!device.open(cfg, *host)) {
        std::fprintf(stderr, "error: audio device: %s\n", device.lastError().c_str());
        return 1;
    }
    const standalone::DeviceInfo& info = device.info();
    const double latencyMs = info.bufferLatencyMs() + 1000.0 * cfg.periodFrames / info.sampleRateHz;
    std::printf("audio: %s \"%s\", %u Hz, ~%.1f ms latency\n", info.backendName.c_str(), info.deviceName.c_str(),
                info.sampleRateHz, latencyMs);

    synth::Patch guiPatch = synth::presets::epiano();
    host->setSampleRate(info.sampleRateHz);
    host->prepare(info.sampleRateHz, guiPatch);
    host->openMidiInputs();
    for (const std::string& n : host->midiNames())
        std::printf("midi:  %s\n", n.c_str());

    devui::DevUi ui;
    if (!ui.open("CFS - fss_play", 1200, 760)) {
        std::fprintf(stderr, "error: could not create the window\n");
        return 1;
    }
    devui::NoteInput keys(*host);

    util::setRealtimeTrapMode(util::RealtimeTrapMode::Abort); // Debug builds: allocation on audio thread = stop
    if (!device.start()) {
        std::fprintf(stderr, "error: %s\n", device.lastError().c_str());
        return 1;
    }

    for (long frame = 0; ui.beginFrame(); ++frame) {
        if (demo && (frame == 30 || frame == 150))
            for (std::uint8_t k : {std::uint8_t{57}, std::uint8_t{60}, std::uint8_t{64}, std::uint8_t{69}})
                frame == 30 ? host->noteOn(k, 0.8f) : host->noteOff(k);
        GuiMessage m;
        while (host->popGui(m)) {
            if (m.kind == GuiMessage::Kind::Param) {
                if (const synth::ParamInfo* p = synth::findParam(m.id))
                    synth::setParam(guiPatch, *p, m.value); // a hardware knob moved: the slider follows
            } else {
                keys.setExternallyHeld(static_cast<std::uint8_t>(m.id), m.kind == GuiMessage::Kind::KeyDown);
            }
        }
        drawMain(guiPatch, *host, info, latencyMs);
        drawOperators(guiPatch, *host);
        placePanel(0.0f, 0.62f, 1.0f, 0.38f);
        ImGui::Begin("Keyboard");
        keys.draw();
        ImGui::End();
        ui.endFrame();
        if (frameLimit >= 0 && frame + 1 >= frameLimit)
            ui.requestClose();
    }

    keys.releaseAll();
    device.stop();          // the audio thread stops reading the MIDI queues...
    host->closeMidiInputs(); // ...before the MIDI threads stop writing them
    std::printf("rt trap: %s, %llu violations\n", util::realtimeTrapInstalled() ? "installed" : "not linked",
                static_cast<unsigned long long>(util::realtimeViolations()));
    return 0;
}

} // namespace
} // namespace fss::play

int main(int argc, char** argv)
{
    return fss::play::run(argc, argv);
}
