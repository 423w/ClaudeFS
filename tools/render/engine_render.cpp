#include "engine_render.h"

#include "render_util.h"
#include "wav_writer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <span>

namespace fss::render {

StereoBuffer renderWithEngine(const synth::Patch& patch, const std::vector<ScoreNote>& score,
                              const EngineRenderOptions& opt, EngineRenderStats* stats)
{
    // Absolute sample times. Note-offs sort before note-ons at the same sample, so a note that ends exactly
    // where the next one on the same key starts is released first (matches how a sequencer would send them).
    struct Timed {
        std::uint64_t at;
        Event e;
    };
    std::vector<Timed> events;
    const auto toSamples = [](double ms) { return static_cast<std::uint64_t>(std::llround(ms * 0.001 * kSampleRate)); };
    for (const ScoreNote& n : score) {
        events.push_back({toSamples(n.startMs), Event::noteOn(0, n.key, static_cast<float>(n.velocity) / 127.0f)});
        events.push_back({toSamples(n.startMs + n.durationMs), Event::noteOff(0, n.key)});
    }
    std::stable_sort(events.begin(), events.end(), [](const Timed& a, const Timed& b) {
        if (a.at != b.at)
            return a.at < b.at;
        return a.e.type == Event::Type::NoteOff && b.e.type != Event::Type::NoteOff;
    });

    const std::uint64_t length = toSamples(scoreEndMs(score) + opt.tailMs);
    Engine eng;
    eng.prepare(kSampleRate, opt.blockSize, opt.stealFadeMs);
    eng.setPatch(patch);
    eng.setPolyphony(opt.polyphony);

    StereoBuffer out;
    out.left.resize(length);
    out.right.resize(length);
    std::vector<Event> block;
    std::size_t next = 0;
    int maxBusy = 0;
    for (std::uint64_t start = 0; start < length; start += static_cast<std::uint64_t>(opt.blockSize)) {
        const auto n = static_cast<int>(std::min<std::uint64_t>(static_cast<std::uint64_t>(opt.blockSize), length - start));
        block.clear();
        while (next < events.size() && events[next].at < start + static_cast<std::uint64_t>(n)) {
            Event e = events[next].e;
            e.frame = static_cast<std::uint32_t>(events[next].at - start); // block-relative time
            block.push_back(e);
            ++next;
        }
        eng.process(block, out.left.data() + start, out.right.data() + start, n);
        maxBusy = std::max(maxBusy, eng.busyVoices());
    }
    if (stats)
        *stats = {eng.steals(), maxBusy};
    return out;
}

void writeStereo(const std::filesystem::path& path, const StereoBuffer& buf)
{
    WavWriter w(path, kSampleRate, 2, SampleFormat::Float32);
    const std::span<const float> ch[] = {buf.left, buf.right};
    w.writePlanar(ch);
    w.finish();
    float peak = 0.0f;
    for (float v : buf.left)
        peak = std::max(peak, std::abs(v));
    std::printf("wrote %-40s %7llu frames (stereo), peak %.3f, clipped %llu\n", path.string().c_str(),
                static_cast<unsigned long long>(w.framesWritten()), peak,
                static_cast<unsigned long long>(w.clippedSamples()));
}

void mixInto(StereoBuffer& a, const StereoBuffer& b, float gain)
{
    if (a.left.size() < b.left.size()) {
        a.left.resize(b.left.size(), 0.0f);
        a.right.resize(b.right.size(), 0.0f);
    }
    for (std::size_t i = 0; i < b.left.size(); ++i) {
        a.left[i] += gain * b.left[i];
        a.right[i] += gain * b.right[i];
    }
}

} // namespace fss::render
