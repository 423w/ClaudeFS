// fss_midi_monitor: lists MIDI input devices, or prints decoded messages from one (L2.3).
//
//   fss_midi_monitor                          list devices
//   fss_midi_monitor --device N --seconds S   open device N, print messages for S seconds
//
// The main thread here plays the audio thread's role: it is the queue's single consumer, polling every
// millisecond. A real audio callback drains the queue once per block instead.
#include "midi_input_winmm.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

using namespace fss::midi;

namespace {

void printMessage(const MidiMessage& m)
{
    const DecodedMidi d = classify(m);
    std::printf("%8u ms  %02X %02X %02X  %-12s ch %2d", m.timestampMs, m.status, m.data1, m.data2,
                kindName(d.kind), d.channel + 1);
    switch (d.kind)
    {
    case MidiKind::NoteOn:
    case MidiKind::NoteOff: std::printf("  key %3d vel %3d", d.number, d.value); break;
    case MidiKind::ControlChange: std::printf("  cc %3d val %3d", d.number, d.value); break;
    case MidiKind::PitchBend: std::printf("  bend %+.4f", static_cast<double>(d.pitchBend)); break;
    case MidiKind::ProgramChange: std::printf("  prog %3d", d.number); break;
    case MidiKind::ChannelPressure: std::printf("  press %3d", d.value); break;
    case MidiKind::PolyPressure: std::printf("  key %3d press %3d", d.number, d.value); break;
    default: break;
    }
    std::printf("\n");
}

} // namespace

int main(int argc, char** argv)
{
    int device = -1;
    double seconds = 10.0;
    for (int i = 1; i + 1 < argc; i += 2)
    {
        if (std::strcmp(argv[i], "--device") == 0)
            device = std::atoi(argv[i + 1]);
        else if (std::strcmp(argv[i], "--seconds") == 0)
            seconds = std::atof(argv[i + 1]);
    }

    const auto names = MidiInputWinmm::listDevices();
    std::printf("MIDI input devices: %zu\n", names.size());
    for (std::size_t i = 0; i < names.size(); ++i)
        std::printf("  [%zu] %s\n", i, names[i].c_str());
    if (device < 0)
    {
        std::printf("Usage: fss_midi_monitor --device N [--seconds S]\n");
        return 0;
    }

    MidiInputWinmm in;
    std::string error;
    if (!in.open(static_cast<unsigned>(device), error))
    {
        std::printf("Could not open device %d: %s\n", device, error.c_str());
        return 1;
    }
    std::printf("Listening on [%d] for %.1f s...\n", device, seconds);

    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    std::size_t count = 0;
    while (std::chrono::steady_clock::now() < end)
    {
        MidiMessage m;
        while (in.pop(m)) // drain everything available, like an audio block would
        {
            printMessage(m);
            ++count;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    in.close();
    std::printf("%zu messages, %u dropped\n", count, in.droppedCount());
    return 0;
}
