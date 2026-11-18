// editor_ui.cpp: the CFS editor's layout and widgets (L3.5). See editor_ui.h for the model/view split.
//
//  ┌ CFS ─ [preset ▾] [Rescan] [name......] [Save]  status ─────────────── voices 3/16  ▮▮▮▮▯ ┐
//  │ Algorithm diagram    │ Operator A │ Operator B │ Operator C │ Operator D │ Filter         │
//  │ 8 thumbnails         │  envelope  │  envelope  │  envelope  │  envelope  │ LFO 1 (vibrato) │
//  │ Global, Voice        │  knobs...  │  knobs...  │  knobs...  │  knobs...  │ LFO 2           │
//  └──────────────────────────────────────────────────────────────────────────────────────────┘
// Every control is generated from the param table: groups come from ParamInfo::group, widget kinds from
// the skew (stepped → menu or switch, everything else → knob), captions and value text from the table too.
// The only hand-made parts are the arrangement and the two drawings (algorithm, envelopes).
#include "editor_ui.h"

#include <fss/engine.h>
#include <fss/synth/algorithm.h>
#include <fss/synth/params.h>

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace fss::plugin {

namespace {

using synth::kParams;
using synth::ParamInfo;

constexpr ImU32 kAccent = IM_COL32(255, 168, 64, 255);    // amber: values, carriers, the selection
constexpr ImU32 kAccentDim = IM_COL32(255, 168, 64, 90);
constexpr ImU32 kModulator = IM_COL32(110, 170, 255, 255); // blue: modulators
constexpr ImU32 kKnobFace = IM_COL32(44, 47, 55, 255);
constexpr ImU32 kTrack = IM_COL32(78, 82, 94, 255);
constexpr ImU32 kTextDim = IM_COL32(170, 174, 184, 255);
constexpr float kPi = std::numbers::pi_v<float>;

std::size_t indexOfId(std::uint32_t id) { return static_cast<std::size_t>(indexOf(id)); }

/// Remember where parameter i's control is, and that it exists this frame.
void record(EditorState& st, std::size_t i, ImVec2 a, ImVec2 b)
{
    st.drawn.set(i);
    st.rects[i] = {a.x, a.y, b.x, b.y};
}

/// A complete gesture in one go (menu pick, switch, thumbnail click).
void tap(EditorModel& m, std::size_t i, float plain)
{
    m.beginEdit(i);
    m.setEdit(i, plain);
    m.endEdit(i);
}

void centeredText(ImDrawList* dl, float cx, float y, ImU32 col, const char* text)
{
    const float w = ImGui::CalcTextSize(text).x;
    dl->AddText(ImVec2(cx - w * 0.5f, y), col, text);
}

/// A control's caption: the table name without the prefix its panel already says ("LFO1 rate" sits under
/// "LFO 1", so it reads "rate"; "F attack" sits under "Filter", so "attack"). Host-facing names keep it.
void caption(const ParamInfo& p, char* out, std::size_t size)
{
    std::string_view n = p.name;
    for (std::string_view prefix : {"LFO1 ", "LFO2 ", "F "})
        if (n.starts_with(prefix)) {
            n.remove_prefix(prefix.size());
            break;
        }
    std::snprintf(out, size, "%.*s", static_cast<int>(n.size()), n.data());
    if (out[0] >= 'a' && out[0] <= 'z')
        out[0] = static_cast<char>(out[0] - 'a' + 'A');
}

bool isSwitch(const ParamInfo& p) { return p.skew == synth::Skew::Stepped && p.unit == "on/off"; }
bool isMenu(const ParamInfo& p)
{
    return p.skew == synth::Skew::Stepped && !isSwitch(p) && !synth::valueLabel(p, static_cast<int>(p.min)).empty();
}

/// The narrowest a table cell may get; tables stretch their cells to fill the panel (see controlTable).
float minCellWidth() { return ImGui::GetFontSize() * 3.9f; }

// ---- the knob -------------------------------------------------------------------------------------------

/// A rotary knob for parameter i: drag up/down (shift = fine), double-click = default. The whole cell
/// (knob + caption + value) is one invisible button, so the gesture starts wherever the user grabs it.
void knob(EditorModel& m, EditorState& st, std::size_t i)
{
    const ParamInfo& p = kParams[i];
    ImGuiIO& io = ImGui::GetIO();
    const float u = ImGui::GetFontSize();
    const float w = std::max(ImGui::GetContentRegionAvail().x, u * 2.5f), d = u * 2.3f; // fill the table cell
    const ImVec2 top = ImGui::GetCursorScreenPos();

    ImGui::PushID(static_cast<int>(p.id));
    ImGui::InvisibleButton("##knob", ImVec2(w, d + u * 2.1f));
    const bool active = ImGui::IsItemActive();
    float norm = synth::toNormalized(p, synth::getParam(m.editorPatch(), p));
    if (ImGui::IsItemActivated()) {
        m.beginEdit(i);
        if (io.MouseClickedCount[ImGuiMouseButton_Left] == 2) { // double-click: back to the default
            m.setEdit(i, p.def);
            norm = synth::toNormalized(p, p.def);
        }
    }
    if (active && io.MouseDelta.y != 0.0f) {
        // 12 font-heights of travel = the full range (so it feels the same at any UI scale); shift = 10× finer.
        const float perPixel = 1.0f / (u * 12.0f) * (io.KeyShift ? 0.1f : 1.0f);
        norm = std::clamp(norm - io.MouseDelta.y * perPixel, 0.0f, 1.0f);
        m.setEdit(i, synth::fromNormalized(p, norm));
    }
    if (ImGui::IsItemDeactivated())
        m.endEdit(i);
    const bool hovered = ImGui::IsItemHovered();
    record(st, i, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

    // Drawing. The arc runs 270° from bottom-left (0) to bottom-right (1). Parameters whose range
    // straddles 0 (detune, key level, env amount) draw their value arc from the zero position instead.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c(top.x + w * 0.5f, top.y + d * 0.5f);
    const float r = d * 0.46f;
    const float a0 = 0.75f * kPi, sweep = 1.5f * kPi;
    const float plainNow = synth::fromNormalized(p, norm);
    const bool bipolar = p.min < 0.0f && p.max > 0.0f;
    const float zero = bipolar ? synth::toNormalized(p, 0.0f) : 0.0f;
    dl->AddCircleFilled(c, r, kKnobFace);
    dl->PathArcTo(c, r * 0.8f, a0, a0 + sweep);
    dl->PathStroke(kTrack, 0, u * 0.22f);
    const float from = a0 + sweep * std::min(zero, norm), to = a0 + sweep * std::max(zero, norm);
    if (to - from > 0.01f) {
        dl->PathArcTo(c, r * 0.8f, from, to);
        dl->PathStroke(active ? IM_COL32(255, 200, 120, 255) : kAccent, 0, u * 0.22f);
    }
    const float a = a0 + sweep * norm;
    dl->AddLine(ImVec2(c.x + std::cos(a) * r * 0.2f, c.y + std::sin(a) * r * 0.2f),
                ImVec2(c.x + std::cos(a) * r * 0.62f, c.y + std::sin(a) * r * 0.62f), IM_COL32(235, 235, 240, 255), u * 0.15f);
    char label[64];
    caption(p, label, sizeof label);
    char value[64];
    valueToText(p, toHost(p, plainNow), value, sizeof value);
    centeredText(dl, c.x, top.y + d + u * 0.05f, ImGui::GetColorU32(ImGuiCol_Text), label);
    centeredText(dl, c.x, top.y + d + u * 1.0f, kTextDim, value);
    if (hovered && !active)
        ImGui::SetTooltip("%.*s: %s\ndrag up/down (shift: fine), double-click: default", static_cast<int>(p.name.size()),
                          p.name.data(), value);
    ImGui::PopID();
}

/// A drop-down for a stepped parameter with value names (filter mode, LFO shape, voice mode...).
void menu(EditorModel& m, EditorState& st, std::size_t i)
{
    const ParamInfo& p = kParams[i];
    const auto v = static_cast<int>(synth::getParam(m.editorPatch(), p));
    const float u = ImGui::GetFontSize();
    ImGui::PushID(static_cast<int>(p.id));
    ImGui::BeginGroup();
    char label[64];
    caption(p, label, sizeof label);
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(-FLT_MIN); // fill the table cell
    const std::string_view current = synth::valueLabel(p, v);
    char preview[64];
    std::snprintf(preview, sizeof preview, "%.*s", static_cast<int>(current.size()), current.data());
    if (ImGui::BeginCombo("##menu", preview, ImGuiComboFlags_HeightLarge)) {
        for (int k = static_cast<int>(p.min); k <= static_cast<int>(p.max); ++k) {
            const std::string_view name = synth::valueLabel(p, k);
            char item[64];
            std::snprintf(item, sizeof item, "%.*s", static_cast<int>(name.size()), name.data());
            if (ImGui::Selectable(item, k == v) && k != v)
                tap(m, i, static_cast<float>(k));
        }
        ImGui::EndCombo();
    }
    ImGui::Dummy(ImVec2(0.0f, u * 0.35f));
    ImGui::EndGroup();
    record(st, i, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    ImGui::PopID();
}

/// An on/off switch (LFO key sync, fixed frequency, glide legato).
void toggle(EditorModel& m, EditorState& st, std::size_t i)
{
    const ParamInfo& p = kParams[i];
    bool on = synth::getParam(m.editorPatch(), p) >= 0.5f;
    ImGui::PushID(static_cast<int>(p.id));
    ImGui::BeginGroup();
    char label[64];
    caption(p, label, sizeof label);
    ImGui::TextUnformatted(label); // above the box, like a menu's caption: long names don't get cut off
    if (ImGui::Checkbox(on ? "on" : "off", &on))
        tap(m, i, on ? 1.0f : 0.0f);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%.*s", static_cast<int>(p.name.size()), p.name.data());
    ImGui::EndGroup();
    record(st, i, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    ImGui::PopID();
}

void control(EditorModel& m, EditorState& st, std::size_t i)
{
    const ParamInfo& p = kParams[i];
    if (isSwitch(p))
        toggle(m, st, i);
    else if (isMenu(p))
        menu(m, st, i);
    else
        knob(m, st, i); // continuous, and stepped counts like bend range (the knob rounds them)
}

/// Lay out a list of parameters in a table with as many columns as fit.
void controlTable(EditorModel& m, EditorState& st, const char* id, const std::vector<std::size_t>& items)
{
    // As many columns as fit at the minimum width, then every column stretched to share the panel equally,
    // so the layout fills any window size (the host may resize the editor).
    const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / minCellWidth()));
    if (ImGui::BeginTable(id, columns, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX)) {
        for (std::size_t i : items) {
            ImGui::TableNextColumn();
            control(m, st, i);
        }
        ImGui::EndTable();
    }
}

/// Every global parameter of one param-table group, in table order (except the ones listed in `skip`).
std::vector<std::size_t> group(std::string_view name, std::uint32_t skip = 0)
{
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < kNumParams; ++i)
        if (kParams[i].op < 0 && kParams[i].group == name && kParams[i].id != skip)
            out.push_back(i);
    return out;
}

// ---- the algorithm drawing ------------------------------------------------------------------------------

/// Draw an algorithm (any 4×4 matrix, not just the presets) into a rectangle: modulators above what they
/// modulate, carriers on the bottom row feeding the output bar, feedback as a loop on the box's side.
/// `levels` dims silent operators (level 0), so the drawing shows what actually sounds.
void drawAlgorithm(ImDrawList* dl, ImVec2 a, ImVec2 b, const synth::Algorithm& alg, const float* levels, bool labels)
{
    constexpr int n = synth::kNumOps;
    auto edge = [&](int s, int t) { return s != t && alg.mod[static_cast<std::size_t>(s)][static_cast<std::size_t>(t)] > 1e-4f; };
    // Depth = longest chain of modulation from this operator down to a carrier (carriers: 0).
    int depth[n] = {};
    for (int pass = 0; pass < n; ++pass) // relax n times: enough for any 4-node graph (cycles just stop growing)
        for (int s = 0; s < n; ++s)
            for (int t = 0; t < n; ++t)
                if (edge(s, t) && s < t)
                    depth[s] = std::max(depth[s], std::min(depth[t] + 1, n - 1));
    int rows = 1;
    for (int d : depth)
        rows = std::max(rows, d + 1);
    const float outBar = (b.y - a.y) * 0.12f;
    const float rowH = (b.y - a.y - outBar) / static_cast<float>(rows);
    const float box = std::min(rowH * 0.55f, (b.x - a.x) / 5.5f);
    ImVec2 pos[n];
    for (int r = 0; r < rows; ++r) {
        int count = 0;
        for (int k = 0; k < n; ++k)
            count += depth[k] == r ? 1 : 0;
        int slot = 0;
        for (int k = 0; k < n; ++k)
            if (depth[k] == r) {
                const float x = a.x + (b.x - a.x) * (static_cast<float>(slot) + 0.5f) / static_cast<float>(count);
                const float y = b.y - outBar - rowH * (static_cast<float>(r) + 0.5f);
                pos[k] = ImVec2(x, y);
                ++slot;
            }
    }
    const float th = std::max(1.0f, box * 0.07f);
    // Carriers → output bar.
    const float barY = b.y - outBar * 0.45f;
    for (int k = 0; k < n; ++k)
        if (alg.carrier[static_cast<std::size_t>(k)] > 0.0f)
            dl->AddLine(ImVec2(pos[k].x, pos[k].y + box * 0.5f), ImVec2(pos[k].x, barY), kAccentDim, th);
    dl->AddLine(ImVec2(a.x + (b.x - a.x) * 0.08f, barY), ImVec2(b.x - (b.x - a.x) * 0.08f, barY), kAccentDim, th * 1.5f);
    // Modulation edges, with an arrowhead at the target's top.
    for (int s = 0; s < n; ++s)
        for (int t = 0; t < n; ++t)
            if (edge(s, t)) {
                const ImVec2 p0(pos[s].x, pos[s].y + box * 0.5f), p1(pos[t].x, pos[t].y - box * 0.5f - th * 2.0f);
                if (p1.y > p0.y) {
                    dl->AddLine(p0, p1, kModulator, th);
                } else { // a back edge (target drawn above the source): curve around the side
                    const float side = box * 1.4f;
                    dl->AddBezierCubic(ImVec2(pos[s].x + box * 0.5f, pos[s].y), ImVec2(pos[s].x + side, pos[s].y),
                                       ImVec2(pos[t].x + side, pos[t].y), ImVec2(pos[t].x + box * 0.5f, pos[t].y), kModulator, th);
                    continue;
                }
                const float ah = box * 0.18f;
                const ImVec2 dir(p1.x - p0.x, p1.y - p0.y);
                const float len = std::max(1e-3f, std::sqrt(dir.x * dir.x + dir.y * dir.y));
                const ImVec2 u(dir.x / len, dir.y / len), nrm(-u.y, u.x);
                dl->AddTriangleFilled(ImVec2(p1.x + u.x * th * 2.0f, p1.y + u.y * th * 2.0f),
                                      ImVec2(p1.x - u.x * ah + nrm.x * ah * 0.6f, p1.y - u.y * ah + nrm.y * ah * 0.6f),
                                      ImVec2(p1.x - u.x * ah - nrm.x * ah * 0.6f, p1.y - u.y * ah - nrm.y * ah * 0.6f), kModulator);
            }
    // Boxes, with feedback loops.
    for (int k = 0; k < n; ++k) {
        const bool carrier = alg.carrier[static_cast<std::size_t>(k)] > 0.0f;
        const bool silent = levels && levels[k] <= 0.0f;
        ImU32 col = carrier ? kAccent : kModulator;
        if (silent)
            col = (col & 0x00FFFFFFu) | 0x50000000u; // same hue, faded: this operator makes no sound
        const ImVec2 lo(pos[k].x - box * 0.5f, pos[k].y - box * 0.5f), hi(pos[k].x + box * 0.5f, pos[k].y + box * 0.5f);
        const float fb = alg.mod[static_cast<std::size_t>(k)][static_cast<std::size_t>(k)];
        if (fb > 1e-4f) {
            const float rr = box * 0.32f;
            dl->AddCircle(ImVec2(hi.x, lo.y), rr, col, 0, th * (1.0f + std::min(fb, 4.0f) * 0.5f));
        }
        dl->AddRectFilled(lo, hi, IM_COL32(30, 32, 38, 255), box * 0.15f);
        dl->AddRect(lo, hi, col, box * 0.15f, 0, th * 1.4f);
        if (labels) {
            const char name[2] = {static_cast<char>('A' + k), 0};
            const ImVec2 ts = ImGui::CalcTextSize(name);
            dl->AddText(ImVec2(pos[k].x - ts.x * 0.5f, pos[k].y - ts.y * 0.5f), col, name);
        }
    }
}

void algorithmPanel(EditorModel& m, EditorState& st)
{
    const synth::Patch& patch = m.editorPatch();
    const std::size_t algIndex = indexOfId(synth::param_id::kAlgorithm);
    const float u = ImGui::GetFontSize();
    ImGui::SeparatorText("Algorithm");
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const float h = std::min(w * 0.75f, u * 11.0f);
    float levels[synth::kNumOps];
    for (std::size_t k = 0; k < synth::kNumOps; ++k)
        levels[k] = patch.ops[k].level;
    // The big drawing shows the LIVE matrix (with this patch's feedback amounts), so it's always the truth.
    drawAlgorithm(ImGui::GetWindowDrawList(), a, ImVec2(a.x + w, a.y + h), patch.algorithm, levels, true);
    ImGui::Dummy(ImVec2(w, h));
    const std::string_view name = synth::valueLabel(kParams[algIndex], patch.algorithmNumber);
    ImGui::TextColored(ImVec4(0.67f, 0.68f, 0.72f, 1.0f), "%.*s", static_cast<int>(name.size()), name.data());

    // Eight thumbnails: click to pick (one complete gesture, so the host records an algorithm change).
    const float t = (w - ImGui::GetStyle().ItemSpacing.x * 3.0f) / 4.0f;
    const ImVec2 first = ImGui::GetCursorScreenPos();
    for (int k = 0; k < 8; ++k) {
        if (k % 4)
            ImGui::SameLine();
        ImGui::PushID(k);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##alg", ImVec2(t, t * 0.8f)) && patch.algorithmNumber != k + 1)
            tap(m, algIndex, static_cast<float>(k + 1));
        const bool selected = patch.algorithmNumber == k + 1;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + t, p.y + t * 0.8f), selected ? IM_COL32(70, 56, 36, 255) : IM_COL32(36, 38, 44, 255), u * 0.2f);
        if (ImGui::IsItemHovered()) {
            dl->AddRect(p, ImVec2(p.x + t, p.y + t * 0.8f), kTrack, u * 0.2f);
            const std::string_view label = synth::valueLabel(kParams[algIndex], k + 1);
            ImGui::SetTooltip("%.*s", static_cast<int>(label.size()), label.data());
        }
        drawAlgorithm(dl, ImVec2(p.x + t * 0.08f, p.y + t * 0.06f), ImVec2(p.x + t * 0.92f, p.y + t * 0.76f),
                      synth::kAlgorithms[static_cast<std::size_t>(k)], nullptr, false);
        ImGui::PopID();
    }
    record(st, algIndex, first, ImVec2(first.x + w, ImGui::GetItemRectMax().y));
}

// ---- operators --------------------------------------------------------------------------------------------

/// A small picture of one envelope: attack, decay to sustain, a held stretch, release. Time is drawn on a
/// square-root scale so 1 ms and 10 s envelopes are both readable.
void envelopeSketch(const dsp::AdsrParams& env, ImU32 col)
{
    const float w = ImGui::GetContentRegionAvail().x, h = ImGui::GetFontSize() * 2.6f;
    const ImVec2 a = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), IM_COL32(30, 32, 38, 255), h * 0.1f);
    auto span = [](float ms) { return std::sqrt(std::max(ms, 1.0f)); };
    const float total = span(env.attackMs) + span(env.decayMs) + span(400.0f) + span(env.releaseMs);
    const float sx = (w * 0.94f) / total, x0 = a.x + w * 0.03f, base = a.y + h * 0.92f, top = a.y + h * 0.1f;
    const float sus = base - (base - top) * std::clamp(env.sustain, 0.0f, 1.0f);
    const float x1 = x0 + span(env.attackMs) * sx, x2 = x1 + span(env.decayMs) * sx, x3 = x2 + span(400.0f) * sx;
    const float x4 = x3 + span(env.releaseMs) * sx;
    const ImVec2 pts[] = {{x0, base}, {x1, top}, {x2, sus}, {x3, sus}, {x4, base}};
    dl->AddPolyline(pts, 5, col, 0, std::max(1.0f, ImGui::GetFontSize() * 0.12f));
    ImGui::Dummy(ImVec2(w, h));
}

/// What an operator does in the current algorithm, in words: "carrier", "modulates B", "silent".
void operatorRole(const synth::Patch& patch, int op, char* out, std::size_t size)
{
    const auto o = static_cast<std::size_t>(op);
    if (patch.ops[o].level <= 0.0f) {
        std::snprintf(out, size, "silent (level 0)");
        return;
    }
    if (patch.algorithm.carrier[o] > 0.0f) {
        std::snprintf(out, size, "carrier");
        return;
    }
    int n = std::snprintf(out, size, "modulates");
    for (std::size_t t = 0; t < synth::kNumOps; ++t)
        if (t != o && patch.algorithm.mod[o][t] > 1e-4f && n > 0 && static_cast<std::size_t>(n) < size)
            n += std::snprintf(out + n, size - static_cast<std::size_t>(n), " %c", static_cast<char>('A' + t));
}

void operatorPanel(EditorModel& m, EditorState& st, int op)
{
    using F = synth::OpField;
    const synth::Patch& patch = m.editorPatch();
    const bool carrier = patch.algorithm.carrier[static_cast<std::size_t>(op)] > 0.0f;
    char title[48], role[64];
    std::snprintf(title, sizeof title, "Operator %c", static_cast<char>('A' + op));
    operatorRole(patch, op, role, sizeof role);
    ImGui::PushStyleColor(ImGuiCol_Text, carrier ? ImVec4(1.0f, 0.66f, 0.25f, 1.0f) : ImVec4(0.43f, 0.67f, 1.0f, 1.0f));
    ImGui::SeparatorText(title);
    ImGui::PopStyleColor();
    ImGui::TextColored(ImVec4(0.67f, 0.68f, 0.72f, 1.0f), "%s", role);
    envelopeSketch(patch.ops[static_cast<std::size_t>(op)].env, carrier ? kAccent : kModulator);
    // Field order chosen for playing, not table order: pitch, level, envelope, scaling, then the rarer ones.
    static constexpr F kOrder[] = {F::Ratio,   F::Detune, F::Level,  F::Feedback, F::Attack, F::Decay, F::Sustain,
                                   F::Release, F::VelSens, F::KeyLevel, F::KeyRate, F::Fixed, F::FixedHz};
    std::vector<std::size_t> items;
    for (F f : kOrder)
        items.push_back(indexOfId(synth::param_id::op(op, f)));
    char id[16];
    std::snprintf(id, sizeof id, "op%d", op);
    controlTable(m, st, id, items);
}

// ---- top bar --------------------------------------------------------------------------------------------

void topBar(EditorModel& m, EditorState& st)
{
    const float u = ImGui::GetFontSize();
    if (!st.presetsListed) {
        st.presets = preset_lib::listPresets();
        st.presetsListed = true;
    }
    if (m.currentPresetName() != st.lastPresetName) { // a state load or preset pick: show its name
        st.lastPresetName = m.currentPresetName();
        std::snprintf(st.saveName, sizeof st.saveName, "%s", st.lastPresetName.c_str());
    }
    ImGui::TextColored(ImVec4(1.0f, 0.66f, 0.25f, 1.0f), "CFS");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", FSS_VERSION_STRING);
    ImGui::SameLine(u * 5.5f);
    ImGui::SetNextItemWidth(u * 12.0f);
    const char* preview = st.lastPresetName.empty() ? "(presets)" : st.lastPresetName.c_str();
    if (ImGui::BeginCombo("##presets", preview, ImGuiComboFlags_HeightLargest)) {
        for (std::size_t k = 0; k < st.presets.size(); ++k) {
            if (k > 0 && st.presets[k - 1].factory && !st.presets[k].factory)
                ImGui::SeparatorText("yours");
            ImGui::PushID(static_cast<int>(k));
            if (ImGui::Selectable(st.presets[k].name.c_str(), st.presets[k].name == st.lastPresetName)) {
                synth::Patch p;
                std::string name;
                if (preset_lib::loadPreset(st.presets[k], p, name, st.status)) {
                    m.loadPreset(p, name);
                    st.status.clear();
                }
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Rescan"))
        st.presets = preset_lib::listPresets();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(u * 10.0f);
    ImGui::InputTextWithHint("##name", "preset name", st.saveName, sizeof st.saveName);
    ImGui::SameLine();
    if (ImGui::Button("Save") && preset_lib::savePreset(m.editorPatch(), st.saveName, st.status)) {
        st.status = std::string("saved to Documents\\CFS Presets");
        st.presets = preset_lib::listPresets();
        m.loadPreset(m.editorPatch(), st.saveName); // the current sound now has this name
    }
    if (!st.status.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", st.status.c_str());
    }

    // Meters on the right: voices in use, and the output peak (atomics written by the audio thread).
    const float meterW = u * 7.0f;
    ImGui::SameLine(ImGui::GetWindowWidth() - meterW - u * 7.5f); // x from the window's left edge
    ImGui::Text("voices %2d/%d", m.activeVoices(), Engine::kMaxPolyphony);
    ImGui::SameLine();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float peak = std::clamp(m.peakLevel(), 0.0f, 1.0f);
    // Meter in dB, -48..0, so quiet notes still move it.
    const float frac = peak > 0.0f ? std::clamp(1.0f + 20.0f * std::log10(peak) / 48.0f, 0.0f, 1.0f) : 0.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(p.x, p.y + u * 0.2f), ImVec2(p.x + meterW, p.y + u * 0.9f), kKnobFace, u * 0.15f);
    dl->AddRectFilled(ImVec2(p.x, p.y + u * 0.2f), ImVec2(p.x + meterW * frac, p.y + u * 0.9f),
                      peak > 0.89f ? IM_COL32(240, 80, 60, 255) : kAccent, u * 0.15f);
    ImGui::Dummy(ImVec2(meterW, u));
}

/// The page buttons under the top bar (L5.1). Plain buttons rather than an ImGui tab bar: the page lives in
/// EditorState, so tests and the test host can choose it directly.
void pageButtons(EditorState& st)
{
    for (int k = 0; k < kNumPages; ++k) {
        if (k > 0)
            ImGui::SameLine();
        const bool current = st.page == k;
        if (current)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(kPageNames[k]))
            st.page = k;
        if (current)
            ImGui::PopStyleColor();
    }
}

/// The FX groups of the param table (every group with an FX parameter), in table order, which is chain order.
std::vector<std::string_view> fxGroups()
{
    std::vector<std::string_view> out;
    for (const ParamInfo& p : kParams)
        if (p.target == synth::ParamTarget::Fx && std::find(out.begin(), out.end(), p.group) == out.end())
            out.push_back(p.group);
    return out;
}

/// Page 1: one panel per effect, side by side in chain order, with a signal-flow caption.
void fxPage(EditorModel& m, EditorState& st)
{
    const std::vector<std::string_view> groups = fxGroups();
    ImGui::TextDisabled("voices  >  %s  >  output", groups.empty() ? "" : "the effects below, left to right");
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    // At least four panel widths, so one effect doesn't stretch across the whole window.
    const float n = static_cast<float>(std::max<std::size_t>(groups.size(), 4));
    const float w = (avail.x - style.ItemSpacing.x * (n - 1.0f)) / n;
    for (std::size_t k = 0; k < groups.size(); ++k) {
        if (k > 0)
            ImGui::SameLine();
        const std::string name(groups[k]);
        ImGui::BeginChild(name.c_str(), ImVec2(w, avail.y * 0.6f), ImGuiChildFlags_Borders);
        ImGui::SeparatorText(name.c_str());
        controlTable(m, st, name.c_str(), group(name));
        ImGui::EndChild();
    }
}

} // namespace

void drawEditor(EditorModel& m, EditorState& st)
{
    st.drawn.reset();
    st.page = std::clamp(st.page, 0, kNumPages - 1);
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("CFS editor", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    topBar(m, st);
    pageButtons(st);
    ImGui::Separator();
    if (st.page == 1) {
        fxPage(m, st);
        ImGui::End();
        return;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float leftW = avail.x * 0.19f, rightW = avail.x * 0.26f;
    const float opW = (avail.x - leftW - rightW - style.ItemSpacing.x * 5.0f) / 4.0f;
    const ImGuiChildFlags panel = ImGuiChildFlags_Borders;

    ImGui::BeginChild("left", ImVec2(leftW, avail.y), panel);
    algorithmPanel(m, st);
    ImGui::SeparatorText("Global");
    controlTable(m, st, "global", group("Global", synth::param_id::kAlgorithm));
    ImGui::SeparatorText("Voice");
    controlTable(m, st, "voice", group("Voice"));
    ImGui::EndChild();

    for (int op = 0; op < synth::kNumOps; ++op) {
        ImGui::SameLine();
        char id[8];
        std::snprintf(id, sizeof id, "op%d", op);
        ImGui::BeginChild(id, ImVec2(opW, avail.y), panel);
        operatorPanel(m, st, op);
        ImGui::EndChild();
    }

    ImGui::SameLine();
    ImGui::BeginChild("right", ImVec2(rightW, avail.y), panel);
    for (const char* g : {"Filter", "LFO 1 (vibrato)", "LFO 2 (filter, index)"}) {
        ImGui::SeparatorText(g);
        controlTable(m, st, g, group(g));
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace fss::plugin
