// editor_ui.h: what CFS's editor draws (L3.5, D14 = Dear ImGui). Platform-independent: it only talks to
// ImGui and to an EditorModel. The Win32 window, the D3D11 device and the timer live in editor_win32.*;
// tests drive this file headlessly (an ImGui context with synthetic mouse input and no renderer at all).
//
// THE MODEL/VIEW SPLIT. The editor never owns parameter values. Every frame it reads the patch from the model
// (the plugin's main-thread mirror, which follows host automation), and every user action goes back as a
// gesture: beginEdit → setEdit... → endEdit. That's what makes the knobs follow automation and the host record
// automation from the knobs, with no extra code for either. View-only state (which preset is selected,
// the text being typed, widget rectangles) lives in EditorState, owned by the window.
#pragma once

#include "clap_params.h"
#include "preset_library.h"

#include <fss/synth/patch.h>

#include <array>
#include <bitset>
#include <string>
#include <vector>

namespace fss::plugin {

class EditorModel {
public:
    virtual ~EditorModel() = default;
    /// The patch to draw (already updated with the latest host automation).
    virtual const synth::Patch& editorPatch() = 0;
    /// A gesture on parameter `index` (kParams order), plain values.
    virtual void beginEdit(std::size_t index) = 0;
    virtual void setEdit(std::size_t index, float plain) = 0;
    virtual void endEdit(std::size_t index) = 0;
    /// Replace the whole patch (a preset): not a gesture, the host is told to re-read the values.
    virtual void loadPreset(const synth::Patch& patch, const std::string& name) = 0;
    virtual const std::string& currentPresetName() = 0;
    virtual float peakLevel() = 0; // output peak, decaying; written by the audio thread (atomic)
    virtual int activeVoices() = 0;
};

struct EditorState {
    std::vector<preset_lib::PresetEntry> presets;
    bool presetsListed = false;
    char saveName[96] = "";
    std::string status;
    std::string lastPresetName; // to notice preset changes made elsewhere (state load) and update saveName
    // Which page is showing (L5.1): the editor outgrew one screen, so it has pages (kPageNames). Only the
    // current page's controls are drawn, so `drawn` below means "has a control on the current page".
    int page = 0;
    // Filled every frame: which parameters got a control, and where (client pixels: x0, y0, x1, y1).
    // Tests check "every parameter has a control"; the test host uses the rectangles to click real widgets.
    std::bitset<kNumParams> drawn;
    std::array<std::array<float, 4>, kNumParams> rects{};
};

/// The editor's pages, in button order. Page 0 is the voice (operators, filter, LFOs); page 1 the FX chain;
/// page 2 the modulation matrix and mod envelope (L4.1).
inline constexpr const char* kPageNames[] = {"Synth", "FX", "Mod"};
inline constexpr int kNumPages = static_cast<int>(std::size(kPageNames));

/// Draw the whole editor as one ImGui window covering the display. Call between ImGui::NewFrame and Render.
void drawEditor(EditorModel& model, EditorState& state);

/// The size the editor is designed for, in logical pixels (at 100 % scaling), and the smallest that works.
inline constexpr int kEditorWidth = 1120, kEditorHeight = 700;
inline constexpr int kEditorMinWidth = 900, kEditorMinHeight = 600;

} // namespace fss::plugin
