// fss/synth/algorithm.h: how 4 operators are wired together (L1.5, D7).
//
// An Algorithm is DATA, not code: a 4×4 modulation matrix plus an output mix. The voice's tick loop
// (fm_voice.h) executes any matrix; the 8 classic algorithms below are just preset matrices, and a
// "free matrix" editor (L4.1) would edit the same struct.
//
//   mod[src][dst]  how much of operator src's output is added to operator dst's phase input.
//                  Operator outputs are already "level × env × sine", and a modulator's level is its
//                  index in radians (operator.h), so 1.0 means "full: pass src's index straight through".
//                  The diagonal mod[i][i] is operator i's self-FEEDBACK amount.
//   carrier[i]     gain of operator i in the output. Non-zero = carrier. Presets use 1/(number of
//                  carriers) so switching algorithms keeps roughly the same loudness.
//
// EVALUATION ORDER: operators run A, B, C, D (0, 1, 2, 3) every sample. An edge from an earlier operator
// to a later one (src < dst) uses this sample's output: a normal modulator. Any other edge (src ≥ dst:
// self-feedback, or a later op feeding an earlier one) is a BACK EDGE and reads the source's previous
// output (averaged over its last two samples, see fm_voice.h). So every matrix, even one with cycles,
// can be evaluated in one pass with no graph sorting. The presets are numbered so all their normal
// edges point forward: the carrier is the last op in each chain.
//
// The 8 presets (the same set as the classic 4-op Yamaha TX81Z/DX21 family). "X > Y" = X modulates Y;
// carriers are marked with *. Diagrams read top to bottom; the bottom row goes to the output.
//
//   1 stack           2 two into one     3 one + stack      4 stack + one
//     A                 A   B              B                  A
//     |                  \ /               |                  |
//     B                   C                C   A              B   C
//     |                   |                 \ /                \ /
//     C                   D*                 D*                 D*
//     |
//     D*
//
//   5 two stacks      6 one into three   7 stack + 2        8 organ
//     A   C               A                A
//     |   |             / | \              |
//     B*  D*           B* C* D*            B*  C*  D*        A*  B*  C*  D*
#pragma once

#include <array>
#include <cstddef>
#include <initializer_list>
#include <utility>

namespace fss::synth {

inline constexpr int kNumOps = 4; // D7

struct Algorithm {
    const char* name = "";
    std::array<std::array<float, kNumOps>, kNumOps> mod{}; // mod[src][dst]
    std::array<float, kNumOps> carrier{};                  // output gain per operator
};

namespace detail {

/// Build an algorithm from (src, dst) edges and a carrier list; carrier gains become 1/N.
constexpr Algorithm makeAlgorithm(const char* name, std::initializer_list<std::pair<int, int>> edges,
                                  std::initializer_list<int> carriers)
{
    Algorithm a{};
    a.name = name;
    for (const auto& [src, dst] : edges)
        a.mod[static_cast<std::size_t>(src)][static_cast<std::size_t>(dst)] = 1.0f;
    for (int c : carriers)
        a.carrier[static_cast<std::size_t>(c)] = 1.0f / static_cast<float>(carriers.size());
    return a;
}

} // namespace detail

// Operator names for the preset table (A is evaluated first; see EVALUATION ORDER above).
enum OpIndex : int { opA = 0, opB = 1, opC = 2, opD = 3 };

inline constexpr std::array<Algorithm, 8> kAlgorithms = {
    detail::makeAlgorithm("1 stack: A>B>C>D*", {{opA, opB}, {opB, opC}, {opC, opD}}, {opD}),
    detail::makeAlgorithm("2 (A+B)>C>D*", {{opA, opC}, {opB, opC}, {opC, opD}}, {opD}),
    detail::makeAlgorithm("3 A + (B>C) > D*", {{opB, opC}, {opA, opD}, {opC, opD}}, {opD}),
    detail::makeAlgorithm("4 (A>B) + C > D*", {{opA, opB}, {opB, opD}, {opC, opD}}, {opD}),
    detail::makeAlgorithm("5 two stacks: A>B*, C>D*", {{opA, opB}, {opC, opD}}, {opB, opD}),
    detail::makeAlgorithm("6 A > B*, C*, D*", {{opA, opB}, {opA, opC}, {opA, opD}}, {opB, opC, opD}),
    detail::makeAlgorithm("7 A>B*, C*, D*", {{opA, opB}}, {opB, opC, opD}),
    detail::makeAlgorithm("8 organ: A* B* C* D*", {}, {opA, opB, opC, opD}),
};

} // namespace fss::synth
