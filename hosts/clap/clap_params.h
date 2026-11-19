// clap_params.h: how CFS's parameter table looks to a CLAP host (L3.2). Pure functions, no state, so the
// plugin, the unit tests and the test host all share exactly one mapping.
//
// THE HOST-VALUE CONVENTION (recorded in DECISIONS.md, L3.2):
//   continuous parameters  host value = the KNOB POSITION 0..1 (fss::synth::toNormalized, skew applied)
//   stepped parameters     host value = the PLAIN integer (algorithm 1..8, filter mode 0..4, on/off 0..1)
// CLAP has no "skew" field: a host draws automation and generic sliders linearly between min and max. A
// cutoff exposed as plain 20..20000 Hz would put everything below 1 kHz in the bottom 5 % of an FL Studio
// automation clip. Exposed as knob position, the clip moves the way CFS's own knob does (equal travel =
// equal musical step). Stepped parameters stay plain because CLAP asks for that (hosts store them as plain
// values and show them as lists).
// CONSEQUENCE: a parameter's skew and range are now part of the stable contract, like its id. Changing
// either changes what every saved automation clip means. (Presets store plain values, so they're safe.)
#pragma once

#include <fss/synth/params.h>

#include <clap/ext/params.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string_view>

namespace fss::plugin {

inline constexpr std::size_t kNumParams = synth::kParams.size();

[[nodiscard]] inline bool isStepped(const synth::ParamInfo& p) noexcept { return p.skew == synth::Skew::Stepped; }

/// Plain value (the param table's unit) → the value the host sees.
[[nodiscard]] inline double toHost(const synth::ParamInfo& p, float plain) noexcept
{
    return isStepped(p) ? static_cast<double>(synth::clampToRange(p, plain))
                        : static_cast<double>(synth::toNormalized(p, plain));
}

/// Host value → plain value (clamped; NaN → default via the table's own guards).
[[nodiscard]] inline float fromHost(const synth::ParamInfo& p, double hostValue) noexcept
{
    const auto v = static_cast<float>(hostValue);
    return isStepped(p) ? synth::clampToRange(p, v) : synth::fromNormalized(p, v);
}

/// Index of a parameter in kParams (the order the host sees them in), or -1.
[[nodiscard]] inline int indexOf(clap_id id) noexcept
{
    for (std::size_t i = 0; i < kNumParams; ++i)
        if (synth::kParams[i].id == id)
            return static_cast<int>(i);
    return -1;
}

/// The host's cookie for a parameter is a pointer to its row in kParams: hosts hand it back with every
/// event, so the plugin finds the row without a search. It's only a speed-up: a null cookie (allowed by the
/// spec, and tested by clap-validator) falls back to indexOf().
[[nodiscard]] inline int indexFromEvent(clap_id id, const void* cookie) noexcept
{
    if (cookie) {
        const auto* row = static_cast<const synth::ParamInfo*>(cookie);
        if (row >= synth::kParams.data() && row < synth::kParams.data() + kNumParams && row->id == id)
            return static_cast<int>(row - synth::kParams.data());
    }
    return indexOf(id);
}

inline void fillInfo(std::size_t index, clap_param_info& info) noexcept
{
    const synth::ParamInfo& p = synth::kParams[index];
    std::memset(&info, 0, sizeof info);
    info.id = p.id;
    info.flags = CLAP_PARAM_IS_AUTOMATABLE;
    if (isStepped(p)) {
        info.flags |= CLAP_PARAM_IS_STEPPED;
        if (!synth::valueLabel(p, static_cast<int>(p.min)).empty())
            info.flags |= CLAP_PARAM_IS_ENUM; // the host may show a drop-down of the names
    }
    info.cookie = const_cast<synth::ParamInfo*>(&p); // CLAP's field is void*; we never write through it
    // L4.1: polyphonic modulation. The host may send PARAM_MOD for these, for all notes or for one note (by note
    // id, or by key). It's an offset on top of the parameter, per voice, never written into the patch.
    if (p.polyMod)
        info.flags |= CLAP_PARAM_IS_MODULATABLE | CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID | CLAP_PARAM_IS_MODULATABLE_PER_KEY;
    // Operator parameters share field names ("Ratio"), so the host's name carries the operator: "Op A Ratio".
    if (p.op >= 0)
        std::snprintf(info.name, sizeof info.name, "Op %.*s %.*s", static_cast<int>(synth::kOpNames[p.op].size()),
                      synth::kOpNames[p.op].data(), static_cast<int>(p.name.size()), p.name.data());
    else
        std::snprintf(info.name, sizeof info.name, "%.*s", static_cast<int>(p.name.size()), p.name.data());
    // module = a folder path in the host's parameter list ("Operators/A", "Filter").
    if (p.op >= 0)
        std::snprintf(info.module, sizeof info.module, "Operators/%.*s", static_cast<int>(synth::kOpNames[p.op].size()),
                      synth::kOpNames[p.op].data());
    else
        std::snprintf(info.module, sizeof info.module, "%.*s", static_cast<int>(p.group.size()), p.group.data());
    info.min_value = isStepped(p) ? static_cast<double>(p.min) : 0.0;
    info.max_value = isStepped(p) ? static_cast<double>(p.max) : 1.0;
    info.default_value = toHost(p, p.def);
}

// ---- value <-> text -----------------------------------------------------------------------------------
// Text shows PLAIN units ("2000 Hz", "Low-pass"), whatever the host value is. Continuous values print with
// 4 significant digits, and the number of decimals is chosen from the value AFTER rounding, so
// text → value → text is stable (999.96 rounds to "1000", and 1000 prints as "1000" again, not "1000.0").

/// Round to 4 significant digits and return how many decimals that needs (0..6).
[[nodiscard]] inline int decimalsFor(double v) noexcept
{
    const double a = std::abs(v);
    if (a < 1e-6)
        return 3;
    const int magnitude = static_cast<int>(std::floor(std::log10(a))); // 2000 → 3, 0.5 → -1
    return std::clamp(3 - magnitude, 0, 6);
}

inline bool valueToText(const synth::ParamInfo& p, double hostValue, char* out, std::size_t size) noexcept
{
    if (size == 0)
        return false;
    const float plain = fromHost(p, hostValue);
    if (isStepped(p)) {
        const int v = static_cast<int>(plain);
        const std::string_view label = synth::valueLabel(p, v);
        if (!label.empty())
            std::snprintf(out, size, "%.*s", static_cast<int>(label.size()), label.data());
        else
            std::snprintf(out, size, "%d%s%.*s", v, p.unit.empty() ? "" : " ", static_cast<int>(p.unit.size()), p.unit.data());
        return true;
    }
    int dec = decimalsFor(plain);
    // Round first, then decide the decimals again from the rounded value (the 999.96 → 1000 case).
    const double scale = std::pow(10.0, dec);
    double rounded = std::round(static_cast<double>(plain) * scale) / scale;
    dec = decimalsFor(rounded);
    if (rounded == 0.0)
        rounded = 0.0; // turn -0.0 into 0.0, so it never prints as "-0.000"
    std::snprintf(out, size, "%.*f%s%.*s", dec, rounded, p.unit.empty() ? "" : " ", static_cast<int>(p.unit.size()),
                  p.unit.data());
    return true;
}

/// Parse what a user typed (or what valueToText printed) back to a host value. Accepts a value name
/// ("low-pass", any case) or a number with an optional unit after it ("2000", "2000 Hz", "-6dB").
[[nodiscard]] inline std::optional<double> textToValue(const synth::ParamInfo& p, std::string_view text) noexcept
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
        text.remove_suffix(1);
    if (text.empty())
        return std::nullopt;
    if (isStepped(p)) {
        auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; };
        for (int v = static_cast<int>(p.min); v <= static_cast<int>(p.max); ++v) {
            const std::string_view label = synth::valueLabel(p, v);
            if (!label.empty() && label.size() == text.size() &&
                std::equal(label.begin(), label.end(), text.begin(), [&](char a, char b) { return lower(a) == lower(b); }))
                return toHost(p, static_cast<float>(v));
        }
    }
    // std::from_chars: locale-independent (a German Windows locale won't turn "0.5" into 0), no allocation.
    float plain = 0.0f;
    const char* first = text.data();
    if (*first == '+')
        ++first; // from_chars rejects a leading '+', people type it
    const auto [ptr, ec] = std::from_chars(first, text.data() + text.size(), plain);
    if (ec != std::errc{} || ptr == first || !std::isfinite(plain))
        return std::nullopt;
    return toHost(p, plain);
}

} // namespace fss::plugin
