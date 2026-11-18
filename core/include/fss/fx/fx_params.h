// fss/fx/fx_params.h: the settings of the global FX chain, as plain data inside the Patch (L5.1).
//
// Like every other part of the Patch, each field is stored in the parameter table's own unit (L3.4 rule:
// percent stays percent, no lossy conversions), and a default-constructed FxParams is "every effect off", so
// presets made before an effect existed sound exactly as they did.
#pragma once

namespace fss::fx {

/// Stereo width (L5.1): mid/side scaling. 100 % = unchanged, 0 % = mono, 200 % = the side doubled.
struct WidthParams {
    bool enabled = false;
    float widthPct = 100.0f;
};

/// Everything the FX chain needs. One member per effect, in chain order (fx_chain.h).
struct FxParams {
    WidthParams width{};
};

} // namespace fss::fx
