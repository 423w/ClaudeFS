// Tests for the editor's UI logic (L3.5), headless: a Dear ImGui context with no window and no renderer,
// synthetic mouse input, and a fake model that records every gesture. (The test host, fss_clap_host,
// does the same through the real DLL and a real Win32 window.)
#include "editor_ui.h"

#include <fss/synth/params.h>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>

#include <cstdlib>
#include <string>
#include <vector>

using namespace fss;
using plugin::EditorState;

namespace {

struct FakeModel final : plugin::EditorModel {
    struct Call {
        char kind; // 'b'egin, 's'et, 'e'nd
        std::size_t index;
        float value;
    };
    synth::Patch patch{};
    std::string name = "Init";
    std::vector<Call> calls;
    const synth::Patch& editorPatch() override { return patch; }
    void beginEdit(std::size_t i) override { calls.push_back({'b', i, 0.0f}); }
    void setEdit(std::size_t i, float v) override
    {
        calls.push_back({'s', i, v});
        synth::setParam(patch, synth::kParams[i], v);
    }
    void endEdit(std::size_t i) override { calls.push_back({'e', i, 0.0f}); }
    void loadPreset(const synth::Patch& p, const std::string& n) override
    {
        patch = p;
        name = n;
    }
    const std::string& currentPresetName() override { return name; }
    float peakLevel() override { return 0.25f; }
    int activeVoices() override { return 3; }
};

/// An ImGui context for one test, with a built font atlas and no backend.
struct HeadlessImGui {
    HeadlessImGui()
    {
        _putenv_s("CFS_PRESET_DIR", "cfs_no_such_folder"); // the preset menu must not read the real Documents
        ctx = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(static_cast<float>(plugin::kEditorWidth), static_cast<float>(plugin::kEditorHeight));
        io.DeltaTime = 1.0f / 60.0f;
        unsigned char* px = nullptr;
        int w = 0, h = 0;
        io.Fonts->GetTexDataAsRGBA32(&px, &w, &h); // builds the atlas; without a renderer nothing uploads it
    }
    ~HeadlessImGui()
    {
        ImGui::DestroyContext(ctx);
        _putenv_s("CFS_PRESET_DIR", "");
    }
    void frame(FakeModel& m, EditorState& st)
    {
        ImGui::NewFrame();
        plugin::drawEditor(m, st);
        ImGui::Render();
    }
    ImGuiContext* ctx;
};

ImVec2 centerOf(const EditorState& st, std::uint32_t id, float yFraction = 0.3f)
{
    const auto& r = st.rects[static_cast<std::size_t>(plugin::indexOf(id))];
    return ImVec2((r[0] + r[2]) * 0.5f, r[1] + (r[3] - r[1]) * yFraction);
}

} // namespace

TEST_CASE("editor: every parameter in the table has a control", "[editor]")
{
    HeadlessImGui ui;
    FakeModel m;
    EditorState st;
    ui.frame(m, st);
    for (std::size_t i = 0; i < plugin::kNumParams; ++i) {
        INFO(synth::kParams[i].name << " (id " << synth::kParams[i].id << ")");
        CHECK(st.drawn[i]);
    }
    CHECK(m.calls.empty()); // drawing alone never edits anything
}

TEST_CASE("editor: a knob drag is one gesture: begin, sets, end; double-click resets", "[editor]")
{
    HeadlessImGui ui;
    FakeModel m;
    EditorState st;
    ImGuiIO& io = ImGui::GetIO();
    ui.frame(m, st);
    const std::size_t cutoff = static_cast<std::size_t>(plugin::indexOf(synth::param_id::kCutoff));
    const ImVec2 c = centerOf(st, synth::param_id::kCutoff);

    io.AddMousePosEvent(c.x, c.y);
    ui.frame(m, st);
    io.AddMouseButtonEvent(0, true);
    ui.frame(m, st);
    for (int k = 1; k <= 5; ++k) {
        io.AddMousePosEvent(c.x, c.y - 8.0f * static_cast<float>(k));
        ui.frame(m, st);
    }
    io.AddMouseButtonEvent(0, false);
    ui.frame(m, st);

    REQUIRE(m.calls.size() >= 3);
    CHECK(m.calls.front().kind == 'b');
    CHECK(m.calls.back().kind == 'e');
    int sets = 0;
    float last = 0.0f;
    for (std::size_t k = 1; k + 1 < m.calls.size(); ++k) {
        CHECK(m.calls[k].kind == 's');
        CHECK(m.calls[k].index == cutoff);
        CHECK(m.calls[k].value > last); // dragging up only ever raises it
        last = m.calls[k].value;
        ++sets;
    }
    CHECK(sets == 5);
    CHECK(m.patch.filter.cutoffHz > 2000.0f);

    // Double-click (two presses within ImGui's double-click time): back to the default, in one gesture.
    m.calls.clear();
    io.AddMousePosEvent(c.x, c.y); // back onto the knob (the drag ended 40 px above it)
    ui.frame(m, st);
    for (int click = 0; click < 2; ++click) {
        io.AddMouseButtonEvent(0, true);
        ui.frame(m, st);
        io.AddMouseButtonEvent(0, false);
        ui.frame(m, st);
    }
    CHECK(m.patch.filter.cutoffHz == synth::findParam(synth::param_id::kCutoff)->def);
    REQUIRE_FALSE(m.calls.empty());
    CHECK(m.calls.back().kind == 'e');
}

TEST_CASE("editor: clicking an algorithm thumbnail switches the algorithm in one gesture", "[editor]")
{
    HeadlessImGui ui;
    FakeModel m;
    EditorState st;
    ImGuiIO& io = ImGui::GetIO();
    ui.frame(m, st);
    // The thumbnails sit four per row at the top of the algorithm control's rectangle; aim at the third.
    const auto& r = st.rects[static_cast<std::size_t>(plugin::indexOf(synth::param_id::kAlgorithm))];
    const float x = r[0] + (r[2] - r[0]) * (2.5f / 4.0f), y = r[1] + 10.0f;
    io.AddMousePosEvent(x, y);
    ui.frame(m, st);
    io.AddMouseButtonEvent(0, true);
    ui.frame(m, st);
    io.AddMouseButtonEvent(0, false);
    ui.frame(m, st);
    REQUIRE(m.calls.size() == 3);
    CHECK(m.calls[0].kind == 'b');
    CHECK(m.calls[1].value == 3.0f);
    CHECK(m.calls[2].kind == 'e');
    CHECK(m.patch.algorithmNumber == 3);
}
