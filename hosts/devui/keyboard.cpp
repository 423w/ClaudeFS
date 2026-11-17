// keyboard.cpp: the on-screen piano and QWERTY note input. See keyboard.h for the user-facing behavior.
#include "keyboard.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fss::devui {

namespace {

// Tracker layout. Index i plays (Z-row base note + i): the first 12 entries are the bottom octave,
// the last 12 the octave above. Black keys sit on the row above their white neighbours, like a piano.
constexpr ImGuiKey kQwerty[24] = {
    ImGuiKey_Z, ImGuiKey_S, ImGuiKey_X, ImGuiKey_D, ImGuiKey_C, ImGuiKey_V,
    ImGuiKey_G, ImGuiKey_B, ImGuiKey_H, ImGuiKey_N, ImGuiKey_J, ImGuiKey_M,
    ImGuiKey_Q, ImGuiKey_2, ImGuiKey_W, ImGuiKey_3, ImGuiKey_E, ImGuiKey_R,
    ImGuiKey_5, ImGuiKey_T, ImGuiKey_6, ImGuiKey_Y, ImGuiKey_7, ImGuiKey_U,
};

// Semitone of each white key inside an octave (C D E F G A B), and whether a black key follows it
// (C#, D#, -, F#, G#, A#, -).
constexpr int kWhiteSemitone[7] = {0, 2, 4, 5, 7, 9, 11};
constexpr bool kBlackAfter[7] = {true, true, false, true, true, true, false};

constexpr ImU32 kWhite = IM_COL32(235, 235, 235, 255);
constexpr ImU32 kBlack = IM_COL32(25, 25, 28, 255);
constexpr ImU32 kHeldUi = IM_COL32(255, 160, 40, 255);   // held by mouse/QWERTY
constexpr ImU32 kHeldExt = IM_COL32(70, 160, 255, 255);  // held only externally (MIDI)
constexpr ImU32 kOutline = IM_COL32(60, 60, 60, 255);

} // namespace

void NoteInput::setOctave(int octave)
{
    // -1 shows keys 0..35. At 7 the piano shows 96..131: keys above 127 are drawn grey and never sent.
    octave_ = std::clamp(octave, -1, 7);
}

void NoteInput::press(int key, float velocity)
{
    if (key < 0 || key > 127)
        return;
    if (holders_[static_cast<size_t>(key)]++ == 0)
        sink_.noteOn(static_cast<std::uint8_t>(key), velocity);
}

void NoteInput::release(int key)
{
    if (key < 0 || key > 127 || holders_[static_cast<size_t>(key)] == 0)
        return;
    if (--holders_[static_cast<size_t>(key)] == 0)
        sink_.noteOff(static_cast<std::uint8_t>(key));
}

void NoteInput::releaseAll()
{
    for (int k = 0; k < 128; ++k)
        if (holders_[static_cast<size_t>(k)] != 0) {
            holders_[static_cast<size_t>(k)] = 0;
            sink_.noteOff(static_cast<std::uint8_t>(k));
        }
    mouseKey_ = -1;
    qwertyNote_.fill(-1);
}

void NoteInput::handleQwerty()
{
    // State-based, not event-based: each frame we compare "is the key down now?" with "did we start a
    // note for it?". That ignores OS auto-repeat for free (a held key just stays down), and when the
    // window loses focus ImGui reports all keys up, so nothing is left hanging.
    const bool typing = ImGui::GetIO().WantTextInput; // an InputText (or Ctrl+click on a slider) is active
    const int zRowBase = 12 * (octave_ + 2);          // the Z row plays the middle shown octave
    for (int i = 0; i < kQwertyKeys; ++i) {
        const bool down = !typing && ImGui::IsKeyDown(kQwerty[i]);
        int& playing = qwertyNote_[static_cast<size_t>(i)];
        if (down && playing < 0) {
            playing = zRowBase + i;
            press(playing, velocity_);
        } else if (!down && playing >= 0) {
            release(playing);
            playing = -1;
        }
    }
}

void NoteInput::drawPiano()
{
    const int whiteCount = 7 * kOctavesShown;
    const float width = std::max(ImGui::GetContentRegionAvail().x, 200.0f);
    const float whiteW = width / static_cast<float>(whiteCount);
    const float height = std::clamp(whiteW * 4.5f, 80.0f, 200.0f);
    const float blackW = whiteW * 0.6f;
    const float blackH = height * 0.62f;
    const int base = 12 * (octave_ + 1); // MIDI key of the leftmost C (octave 4 -> 60)

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    // One invisible button covers the whole piano: it captures the mouse on press and stays "active"
    // while the button is held, even if the mouse leaves it. That's what makes dragging (glissando) work.
    ImGui::InvisibleButton("##piano", ImVec2(width, height));
    const bool active = ImGui::IsItemActive();

    // Hit test: which key is under point p, and how far down that key (0 = back, 1 = front edge)?
    // Black keys are on top, so test them first.
    auto hitTest = [&](ImVec2 p, float& depth) -> int {
        const float x = p.x - origin.x, y = p.y - origin.y;
        if (x < 0 || x >= width || y < 0 || y >= height)
            return -1;
        if (y < blackH) {
            for (int w = 0; w < whiteCount; ++w) {
                if (!kBlackAfter[w % 7] || w == whiteCount - 1)
                    continue;
                const float cx = static_cast<float>(w + 1) * whiteW;
                if (x >= cx - blackW * 0.5f && x < cx + blackW * 0.5f) {
                    depth = y / blackH;
                    return base + 12 * (w / 7) + kWhiteSemitone[w % 7] + 1;
                }
            }
        }
        const int w = std::min(static_cast<int>(x / whiteW), whiteCount - 1);
        depth = y / height;
        return base + 12 * (w / 7) + kWhiteSemitone[w % 7];
    };

    // Mouse: follow the key under the cursor while the button is held.
    int target = -1;
    float depth = 0.0f;
    if (active)
        target = hitTest(ImGui::GetIO().MousePos, depth);
    if (target != mouseKey_) {
        release(mouseKey_);
        mouseKey_ = target;
        // Velocity from click depth: like a real key, playing near the front edge gives more leverage.
        // Floor at 0.15 so a click at the very top still makes sound.
        press(mouseKey_, 0.15f + 0.85f * std::clamp(depth, 0.0f, 1.0f));
    }

    auto colorFor = [&](int key, ImU32 normal) -> ImU32 {
        if (key < 0 || key > 127)
            return IM_COL32(90, 90, 90, 255); // outside the MIDI range: greyed out
        if (holders_[static_cast<size_t>(key)] != 0)
            return kHeldUi;
        if (external_[static_cast<size_t>(key)])
            return kHeldExt;
        return normal;
    };

    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int w = 0; w < whiteCount; ++w) { // white keys first, so the black keys paint over them
        const int key = base + 12 * (w / 7) + kWhiteSemitone[w % 7];
        const ImVec2 a(origin.x + static_cast<float>(w) * whiteW, origin.y);
        const ImVec2 b(a.x + whiteW, origin.y + height);
        dl->AddRectFilled(a, b, colorFor(key, kWhite));
        dl->AddRect(a, b, kOutline);
        if (w % 7 == 0) { // label every C with its name, e.g. "C4" (C4 = 60, so octave = key / 12 - 1)
            char label[8];
            std::snprintf(label, sizeof label, "C%d", key / 12 - 1);
            dl->AddText(ImVec2(a.x + 3.0f, b.y - ImGui::GetFontSize() - 2.0f), IM_COL32(60, 60, 60, 255), label);
        }
    }
    for (int w = 0; w < whiteCount - 1; ++w) {
        if (!kBlackAfter[w % 7])
            continue;
        const int key = base + 12 * (w / 7) + kWhiteSemitone[w % 7] + 1;
        const float cx = origin.x + static_cast<float>(w + 1) * whiteW;
        dl->AddRectFilled(ImVec2(cx - blackW * 0.5f, origin.y), ImVec2(cx + blackW * 0.5f, origin.y + blackH),
                          colorFor(key, kBlack), 2.0f, ImDrawFlags_RoundCornersBottom);
    }
}

void NoteInput::draw()
{
    ImGui::PushID(this); // lets two NoteInputs coexist without their widget IDs colliding

    if (ImGui::ArrowButton("##octdown", ImGuiDir_Left))
        setOctave(octave_ - 1);
    ImGui::SameLine();
    ImGui::Text("Octave C%d", octave_);
    ImGui::SameLine();
    if (ImGui::ArrowButton("##octup", ImGuiDir_Right))
        setOctave(octave_ + 1);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderFloat("QWERTY velocity", &velocity_, 0.05f, 1.0f, "%.2f");
    ImGui::SameLine();
    ImGui::TextDisabled("(Z..M / Q..U play notes)");

    handleQwerty();
    drawPiano();

    ImGui::PopID();
}

} // namespace fss::devui
