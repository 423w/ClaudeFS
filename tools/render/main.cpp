// fss_render: offline renderer (host).
//
//   fss_render [SCENARIO ...] [--out DIR]
//       SCENARIO: l1.1, l1.2, ... (default: all of them). DIR defaults to "renders".
//
// Hosts are the only code allowed to do I/O (CLAUDE.md). Each lesson's renders live in their own file
// (l1_1.cpp, l1_2.cpp, ...) so earlier lessons stay reproducible as later ones change the engine.

#include "scenarios.h"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <string_view>
#include <vector>

namespace {

struct Scenario {
    std::string_view name;
    void (*render)(const std::filesystem::path&);
};

constexpr Scenario kScenarios[] = {
    {"l1.1", fss::render::renderL1_1},
    {"l1.2", fss::render::renderL1_2},
    {"l1.3", fss::render::renderL1_3},
    {"l1.4", fss::render::renderL1_4},
    {"l1.5", fss::render::renderL1_5},
    {"l1.6", fss::render::renderL1_6},
    {"l1.7", fss::render::renderL1_7},
    {"l1.8", fss::render::renderL1_8},
    {"l1.9", fss::render::renderL1_9},
    {"l1.10", fss::render::renderL1_10},
    {"l1.11", fss::render::renderL1_11},
    {"l2.4", fss::render::renderL2_4},
    {"l2.5", fss::render::renderL2_5},
    {"l2.6", fss::render::renderL2_6},
};

int usage()
{
    std::fprintf(stderr, "usage: fss_render [SCENARIO ...] [--out DIR]\nscenarios:");
    for (const auto& s : kScenarios)
        std::fprintf(stderr, " %.*s", static_cast<int>(s.name.size()), s.name.data());
    std::fprintf(stderr, "\n");
    return 2;
}

} // namespace

int main(int argc, char** argv)
{
    std::filesystem::path outDir = "renders";
    std::vector<const Scenario*> chosen;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--out" && i + 1 < argc) {
            outDir = argv[++i];
            continue;
        }
        const Scenario* found = nullptr;
        for (const auto& s : kScenarios)
            if (s.name == arg)
                found = &s;
        if (!found)
            return usage();
        chosen.push_back(found);
    }
    if (chosen.empty())
        for (const auto& s : kScenarios)
            chosen.push_back(&s);

    try {
        std::filesystem::create_directories(outDir);
        for (const Scenario* s : chosen) {
            std::printf("== %.*s\n", static_cast<int>(s->name.size()), s->name.data());
            s->render(outDir);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fss_render: %s\n", e.what());
        return 1;
    }
    return 0;
}
