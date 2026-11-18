// fss/util/finite.h: is a block of audio free of NaN and Inf? (L6.4, pulled into L3.2)
//
// Used by Engine::process as the last line of defence before samples reach a host. `x - x` is 0 for
// every finite x and NaN for NaN and ±Inf, so one sum over the block answers the question without a
// branch per sample. It relies on IEEE arithmetic: MSVC's default /fp:precise keeps `x - x`, whereas
// /fp:fast (or -ffast-math) may fold it to 0, which would silently disable this check.
#pragma once

namespace fss::util {

[[nodiscard]] inline bool allFinite(const float* a, const float* b, int n) noexcept
{
    float check = 0.0f;
    for (int i = 0; i < n; ++i)
        check += (a[i] - a[i]) + (b[i] - b[i]);
    return check == 0.0f; // NaN compares unequal to everything, including 0
}

} // namespace fss::util
