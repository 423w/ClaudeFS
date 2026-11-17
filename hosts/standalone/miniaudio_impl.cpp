// miniaudio_impl.cpp: the one translation unit that compiles miniaudio's implementation.
//
// Single-header libraries ship declarations AND definitions in one file. Every file may #include it for
// the declarations, but exactly one must define MINIAUDIO_IMPLEMENTATION first, or the linker sees each
// function either zero times (unresolved externals) or many times (duplicate symbols). Keeping it in its
// own file also means editing audio_device.cpp doesn't recompile 90k lines of miniaudio.
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
