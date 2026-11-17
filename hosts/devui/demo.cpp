// demo.cpp: fss_devui_demo, a smoke test and playground for the dev UI shell (L2.4).
//
// Shows the keyboard widget, a placeholder "Patch" panel (L2.4 will generate it from the param table),
// and a log of the note events the keyboard sends. No audio: this only exercises the GUI side.
//
//   fss_devui_demo              run until the window is closed
//   fss_devui_demo --frames N   close by itself after N frames (used to check it starts and renders)
#include "dev_ui.h"
#include "keyboard.h"

#include <imgui.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// A NoteSink that records events into a fixed ring of text lines. A real host would push an
// fss::Event into a lock-free queue for the audio thread instead.
class LogSink final : public fss::devui::NoteSink {
public:
    void noteOn(std::uint8_t key, float velocity) override { add("note on  %3u  vel %.2f", key, velocity); }
    void noteOff(std::uint8_t key) override { add("note off %3u", key); }

    static constexpr int kLines = 64;
    int count() const { return count_; }
    // i = 0 is the oldest line still kept.
    const char* line(int i) const { return lines_[static_cast<size_t>((next_ + kLines - count_ + i) % kLines)].data(); }

private:
    template <typename... Args>
    void add(const char* fmt, Args... args)
    {
        std::snprintf(lines_[static_cast<size_t>(next_)].data(), 48, fmt, args...);
        next_ = (next_ + 1) % kLines;
        if (count_ < kLines)
            ++count_;
        scrollToBottom = true;
    }

    std::array<std::array<char, 48>, kLines> lines_{};
    int next_ = 0;
    int count_ = 0;

public:
    bool scrollToBottom = false;
};

} // namespace

int main(int argc, char** argv)
{
    long maxFrames = -1; // -1 = run until closed
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            maxFrames = std::strtol(argv[++i], nullptr, 10);
        } else {
            std::fprintf(stderr, "usage: fss_devui_demo [--frames N]\n");
            return 2;
        }
    }

    fss::devui::DevUi ui;
    if (!ui.open("CFS dev UI demo", 1100, 720))
        return 1;

    LogSink log;
    fss::devui::NoteInput keyboard(log);

    // Dummy parameters, only so the Patch panel has something to drag.
    float cutoffHz = 2000.0f, resonance = 0.2f, attackMs = 5.0f, releaseMs = 300.0f, levelDb = -6.0f;
    bool fakeMidiE4 = false;

    // Layout constants below are in 16 px-font units; scale them with the (DPI-scaled) font size.
    // (GetFontSize() is only valid inside a frame, so read the loaded font's size directly.)
    const float u = ImGui::GetIO().Fonts->Fonts[0]->FontSize / 16.0f;
    long frame = 0;
    while (ui.beginFrame()) {
        if (maxFrames >= 0 && frame++ >= maxFrames)
            ui.requestClose(); // takes effect on the next beginFrame(); finish this frame normally

        ImGui::SetNextWindowPos(ImVec2(10 * u, 10 * u), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(420 * u, 300 * u), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Patch")) {
            ImGui::TextDisabled("Placeholder: L2.4 generates this from the param table.");
            ImGui::SliderFloat("cutoff", &cutoffHz, 20.0f, 20000.0f, "%.0f Hz", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("resonance", &resonance, 0.0f, 1.0f);
            ImGui::SliderFloat("attack", &attackMs, 0.1f, 5000.0f, "%.1f ms", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("release", &releaseMs, 1.0f, 10000.0f, "%.0f ms", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("level", &levelDb, -60.0f, 0.0f, "%.1f dB");
            // Exercises setExternallyHeld(), which a MIDI input would drive.
            if (ImGui::Checkbox("pretend MIDI holds E4", &fakeMidiE4))
                keyboard.setExternallyHeld(64, fakeMidiE4);
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(440 * u, 10 * u), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300 * u, 300 * u), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Note log")) {
            for (int i = 0; i < log.count(); ++i)
                ImGui::TextUnformatted(log.line(i));
            if (log.scrollToBottom) {
                ImGui::SetScrollHereY(1.0f);
                log.scrollToBottom = false;
            }
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(10 * u, 320 * u), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(1080 * u, 260 * u), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Keyboard"))
            keyboard.draw();
        ImGui::End();

        ui.endFrame();
    }
    keyboard.releaseAll(); // a real host would make sure no note is left sounding
    return 0;
}
