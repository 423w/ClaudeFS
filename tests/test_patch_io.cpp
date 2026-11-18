// Tests for the text patch format (L3.4, D15), the factory bank, and the non-finite block check (L6.4).
#include <fss/synth/factory_bank.h>
#include <fss/synth/params.h>
#include <fss/synth/patch_io.h>
#include <fss/synth/presets.h>
#include <fss/util/finite.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <random>
#include <string>

using namespace fss;
using namespace fss::synth;

namespace {

Patch randomPatch(std::mt19937& rng)
{
    Patch p;
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    for (const ParamInfo& info : kParams)
        setParam(p, info, fromNormalized(info, u(rng)));
    return p;
}

bool sameParams(const Patch& a, const Patch& b)
{
    for (const ParamInfo& info : kParams)
        if (getParam(a, info) != getParam(b, info)) // bit-exact on purpose
            return false;
    return true;
}

} // namespace

TEST_CASE("patch text: save -> load is exact for random patches, and save is deterministic", "[patch_io]")
{
    std::mt19937 rng(11);
    for (int i = 0; i < 500; ++i) {
        const Patch p = randomPatch(rng);
        const std::string text = patchToText(p, "Random " + std::to_string(i));
        const PatchLoad r = patchFromText(text);
        REQUIRE(r.ok);
        CHECK(r.version == kPatchFormatVersion);
        CHECK(r.name == "Random " + std::to_string(i));
        CHECK(r.missingIds == 0);
        CHECK(r.unknownIds == 0);
        CHECK(r.clampedValues == 0);
        CHECK(sameParams(r.patch, p));
        CHECK(patchToText(r.patch, r.name) == text); // save(load(save(p))) == save(p), byte for byte
    }
}

TEST_CASE("patch text: an OLD file without the newer parameters loads with defaults for them", "[patch_io]")
{
    // "Simulate the future": a preset saved before L1.10 knew only algorithm, output and the operators.
    // Every parameter added since (filter, LFOs, voice modes) must come out at its default.
    const PatchLoad r = patchFromText("CFS patch 1\n"
                                      "name = From the past\n"
                                      "1 = 3\n"
                                      "2 = -6\n"
                                      "1000 = 2   # Op A ratio\n"
                                      "end\n");
    REQUIRE(r.ok);
    CHECK(r.patch.algorithmNumber == 3);
    CHECK(r.patch.ops[0].ratio.value == 2.0);
    CHECK(r.missingIds == static_cast<int>(kParams.size()) - 3);
    const Patch defaults{};
    for (const ParamInfo& info : kParams)
        if (info.id != 1 && info.id != 2 && info.id != 1000)
            CHECK(getParam(r.patch, info) == getParam(defaults, info));
}

TEST_CASE("patch text: a file from a NEWER CFS loads what it can", "[patch_io]")
{
    const PatchLoad r = patchFromText("CFS patch 7\n11 = 800\n9999 = 1 # a parameter from the future\nend\n");
    REQUIRE(r.ok);
    CHECK(r.version == 7);
    CHECK(r.unknownIds == 1);
    CHECK(r.patch.filter.cutoffHz == 800.0f);
}

TEST_CASE("patch text: hand-edited files (comments, CRLF, tabs, '+', out of range) are accepted", "[patch_io]")
{
    const PatchLoad r = patchFromText("# my favourite\r\n"
                                      "  CFS patch 1  \r\n"
                                      "\r\n"
                                      "name = Bell #2\r\n"
                                      "11\t=\t+1500  # cutoff\r\n"
                                      "12 = 99      # resonance way past the max\r\n"
                                      "10 = 1.4     # filter mode, not a whole number\r\n"
                                      "end\r\n"
                                      "anything after end is ignored\r\n");
    REQUIRE(r.ok);
    CHECK(r.name == "Bell #2"); // '#' inside a name is kept
    CHECK(r.patch.filter.cutoffHz == 1500.0f);
    CHECK(r.patch.filter.q == 25.0f); // clamped to the max
    CHECK(r.patch.filter.mode == FilterMode::LowPass); // rounded
    CHECK(r.clampedValues == 2);
}

TEST_CASE("patch text: malformed files are rejected with the line number", "[patch_io]")
{
    struct Case {
        const char* text;
        int line;
    };
    const Case cases[] = {
        {"", 0},
        {"hello", 1},
        {"CFS patch\n", 1},
        {"CFS patch 0\n", 1},
        {"CFS patch 1\n11 = abc\n", 2},
        {"CFS patch 1\n11 = 5 kHz\n", 2},
        {"CFS patch 1\n11 = nan\n", 2},
        {"CFS patch 1\n11 = inf\n", 2},
        {"CFS patch 1\n\n11 = 500\n11 = 600\n", 4},
        {"CFS patch 1\ncutoff = 500\n", 2},
        {"CFS patch 1\n11 500\n", 2},
        {"CFS patch 1\n-11 = 500\n", 2},
        {"CFS patch 1\n11 = 500\n", 0}, // no "end": cut off while saving
    };
    for (const Case& c : cases) {
        INFO("text: " << c.text);
        const PatchLoad r = patchFromText(c.text);
        CHECK_FALSE(r.ok);
        CHECK(r.errorLine == c.line);
        CHECK_FALSE(r.error.empty());
    }
    CHECK_FALSE(patchFromText(std::string("CFS patch 1\n11 = 5\0\0\0", 20)).ok); // binary data
    CHECK_FALSE(patchFromText(std::string(kMaxPatchTextBytes + 1, '#')).ok);    // too large
}

TEST_CASE("patch text: random and mutated input never crashes; whatever loads is in range", "[patch_io][fuzz]")
{
    std::mt19937 rng(12);
    const std::string valid = patchToText(presets::evolvingPad(), "FM pad");
    int accepted = 0;
    for (int trial = 0; trial < 20000; ++trial) {
        std::string s;
        if (trial % 3 == 0) { // pure noise, sometimes behind a valid header
            s = (trial % 2) ? "CFS patch 1\n" : "";
            const int n = static_cast<int>(rng() % 300);
            for (int k = 0; k < n; ++k)
                s += static_cast<char>(rng() % 2 ? "0123456789 =.#-+e\n"[rng() % 18] : static_cast<char>(rng()));
        } else { // a valid file with a few characters replaced, inserted or deleted
            s = valid;
            const int edits = 1 + static_cast<int>(rng() % 6);
            for (int e = 0; e < edits && !s.empty(); ++e) {
                const std::size_t at = rng() % s.size();
                const char c = "0123456789 =.#-+e\nx"[rng() % 19];
                switch (rng() % 3) {
                case 0: s[at] = c; break;
                case 1: s.insert(s.begin() + static_cast<std::ptrdiff_t>(at), c); break;
                default: s.erase(at, 1); break;
                }
            }
        }
        const PatchLoad r = patchFromText(s);
        if (!r.ok)
            continue;
        ++accepted;
        for (const ParamInfo& info : kParams) {
            const float v = getParam(r.patch, info);
            REQUIRE(std::isfinite(v));
            REQUIRE(v >= info.min - 1e-3f * std::abs(info.min));
            REQUIRE(v <= info.max * 1.0001f + 1e-6f);
        }
    }
    SUCCEED("accepted " << accepted << " of 20000");
}

TEST_CASE("factory bank: the embedded files match the code presets exactly", "[patch_io][presets]")
{
    const auto bank = factoryBank();
    REQUIRE(bank.size() == std::size(presets::kFactoryPresets));
    for (const presets::NamedPreset& pr : presets::kFactoryPresets) {
        INFO(pr.name);
        const FactoryPreset* file = nullptr;
        for (const FactoryPreset& f : bank)
            if (f.name == pr.name)
                file = &f;
        REQUIRE(file != nullptr); // run `fss_render l3.4` and re-run CMake if this fails
        const PatchLoad r = patchFromText(file->text);
        REQUIRE(r.ok);
        CHECK(r.name == pr.name);
        CHECK(r.missingIds == 0);
        CHECK(sameParams(r.patch, pr.make()));
    }
    CHECK(std::string(bank.front().name) == "Init"); // "00 Init" sorts first (std::string: this Catch2 build has no string_view printer)
}

TEST_CASE("patch text: a name with control characters stays on its line", "[patch_io]")
{
    const std::string text = patchToText(Patch{}, "two\nlines\tand\x01more");
    const PatchLoad r = patchFromText(text);
    REQUIRE(r.ok);
    CHECK(r.name == "two lines and more");
}

TEST_CASE("allFinite: finds NaN and Inf anywhere in a block", "[realtime][L6.4]")
{
    float a[64] = {}, b[64] = {};
    CHECK(util::allFinite(a, b, 64));
    CHECK(util::allFinite(a, b, 0));
    for (float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                      -std::numeric_limits<float>::infinity()})
        for (int at : {0, 17, 63}) {
            a[at] = bad;
            CHECK_FALSE(util::allFinite(a, b, 64));
            a[at] = 0.0f;
            b[at] = bad;
            CHECK_FALSE(util::allFinite(a, b, 64));
            b[at] = 0.0f;
        }
    a[5] = std::numeric_limits<float>::max(); // huge but finite is fine
    a[6] = std::numeric_limits<float>::denorm_min();
    CHECK(util::allFinite(a, b, 64));
}
