// fss/util/inline.h: FSS_FORCEINLINE, for the few per-sample functions where the compiler's own inlining
// decision measurably goes wrong.
//
// Measured in L4.1: when the mod matrix made Voice::tick bigger, MSVC stopped inlining it (and FmVoice::tick)
// into the Engine's per-sample voice loop. Same arithmetic, 39 % more CPU for the full test patch; forcing
// the inline won back about 40 % of that (journal L4.1). Use sparingly: forcing everything inline bloats the
// code and can make it slower.
#pragma once

#if defined(_MSC_VER)
#define FSS_FORCEINLINE [[msvc::forceinline]]
#elif defined(__GNUC__) || defined(__clang__)
#define FSS_FORCEINLINE [[gnu::always_inline]]
#else
#define FSS_FORCEINLINE
#endif
