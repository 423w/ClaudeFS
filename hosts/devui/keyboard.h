// keyboard.h: play notes from the dev UI (L2.4): an on-screen piano and the computer keyboard.
//
// NoteInput is an ImGui widget: call draw() once per frame, between DevUi::beginFrame() and endFrame().
// It turns mouse clicks and QWERTY keys into note on/off calls on a NoteSink you supply. The sink is
// called on the GUI thread; forwarding the notes to the audio thread (a lock-free queue) is the host's job.
//
//   Mouse:  press a key = note on (velocity from how far down the key you click: low = soft, near the
//           front edge = loud), release = note off, drag across keys = glissando.
//   QWERTY: Z S X D C V G B H N J M = C..B, Q 2 W 3 E R 5 T 6 Y 7 U = the octave above (the classic
//           tracker layout: the bottom letter row is the white keys, the row above it the black keys).
//           Ignored while an ImGui text field has focus, so typing a value doesn't play notes.
#pragma once

#include <array>
#include <cstdint>

namespace fss::devui {

// Where NoteInput sends notes. Two virtual calls, plain values, no allocation per event.
struct NoteSink {
    virtual void noteOn(std::uint8_t key, float velocity) = 0; // key 0..127, velocity 0..1
    virtual void noteOff(std::uint8_t key) = 0;

protected:
    ~NoteSink() = default; // NoteInput never owns or deletes a sink, so no virtual destructor is needed
};

class NoteInput {
public:
    explicit NoteInput(NoteSink& sink) : sink_(sink) {}

    // Draws the controls (octave, velocity) and the piano into the current ImGui window, and processes
    // mouse/QWERTY input. Call once per frame.
    void draw();

    // Highlight a key held by some other source (e.g. a MIDI keyboard), so every held note lights up no
    // matter where it came from. Purely visual: it never calls the sink.
    void setExternallyHeld(std::uint8_t key, bool held) { external_[key & 0x7f] = held; }

    // Releases every note this widget is holding (sends note offs). Call when focus/hosts change.
    void releaseAll();

    // Lowest octave shown, in MIDI octave numbers where C4 = key 60 (so octave 3 shows C3..B5).
    int octave() const { return octave_; }
    void setOctave(int octave);

    static constexpr int kOctavesShown = 3;

private:
    // A key can be held by the mouse and a QWERTY key at once. We count holders and only send note on
    // for 0 -> 1 and note off for 1 -> 0, so the synth sees one clean on/off pair per key.
    void press(int key, float velocity);
    void release(int key);
    void handleQwerty();
    void drawPiano();

    NoteSink& sink_;
    int octave_ = 3;
    float velocity_ = 0.8f;                 // used by QWERTY notes (no click position to derive it from)
    std::array<std::uint8_t, 128> holders_{}; // mouse + QWERTY hold count per key
    std::array<bool, 128> external_{};
    int mouseKey_ = -1;                     // key the mouse is holding, -1 = none
    // The note each QWERTY key started, so a key released after an octave change stops the right note.
    static constexpr int kQwertyKeys = 24;
    std::array<int, kQwertyKeys> qwertyNote_{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                                             -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
};

} // namespace fss::devui
