// patch_io.cpp: the text patch format (L3.4, D15). See patch_io.h for the rules.
//
// Parsing never trusts its input: a preset file may be truncated, hand-edited, from a newer CFS, or not a
// patch at all (a host handing us another plugin's state by mistake). Every path below either produces a
// valid patch or a clear error; tests feed it random bytes and mutated files to keep it that way.
#include <fss/synth/patch_io.h>

#include <fss/synth/params.h>

#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace fss::synth {

namespace {

std::string_view trim(std::string_view s) noexcept
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
        s.remove_suffix(1);
    return s;
}

/// Shortest decimal text that reads back as exactly `v` (std::to_chars without a precision does that).
std::string shortest(float v)
{
    std::array<char, 32> buf{};
    const auto r = std::to_chars(buf.data(), buf.data() + buf.size(), v);
    return std::string(buf.data(), r.ptr);
}

/// What changed between format versions, applied after parsing. Format 1 is the first text format, so
/// there is nothing to migrate yet. The rule that keeps this function short: a parameter's id never changes
/// meaning. If one ever must (say, a range widened in a way that changes the sound), the change gets a NEW
/// id, the format version goes up, and this function maps the old id's value onto the new one for files
/// whose version is older.
void migrate(int /*fromVersion*/, Patch& /*patch*/) {}

PatchLoad fail(std::string error, int line)
{
    PatchLoad r;
    r.error = std::move(error);
    r.errorLine = line;
    return r;
}

} // namespace

std::string patchToText(const Patch& patch, std::string_view name)
{
    std::string out;
    out.reserve(4096);
    out += "CFS patch " + std::to_string(kPatchFormatVersion) + "\n";
    out += "# CFS (Claude's FirstStreetSynthesis) patch. One \"<parameter id> = <value>\" per line, values in\n"
           "# the units shown in the comments. Ids are permanent (core/include/fss/synth/params.h); a file that\n"
           "# leaves an id out gets that parameter's default. Text after '#' is a comment.\n";
    // The name: one line, printable characters only (a newline in a name would start a new "line").
    std::string clean;
    for (char c : name)
        clean += (static_cast<unsigned char>(c) < 0x20) ? ' ' : c;
    out += "name = " + std::string(trim(clean)) + "\n";

    std::string_view group;
    int op = -2;
    for (const ParamInfo& p : kParams) {
        if (p.op != op || (p.op < 0 && p.group != group)) {
            op = p.op;
            group = p.group;
            out += "\n# ";
            out += p.op >= 0 ? "Operator " + std::string(kOpNames[static_cast<std::size_t>(p.op)]) : std::string(group);
            out += "\n";
        }
        const float v = getParam(patch, p);
        std::string line = std::to_string(p.id) + " = " + shortest(v);
        if (line.size() < 18)
            line.resize(18, ' '); // align the comments: easier to read and to diff
        line += "  # ";
        line += p.name;
        if (!p.unit.empty() && p.unit != "on/off")
            line += " [" + std::string(p.unit) + "]";
        const std::string_view label = valueLabel(p, static_cast<int>(v));
        if (!label.empty())
            line += " (" + std::string(label) + ")";
        out += line + "\n";
    }
    out += "\nend\n"; // required: a file cut off while saving (crash, full disk) is then detected
    return out;
}

PatchLoad patchFromText(std::string_view text)
{
    if (text.size() > kMaxPatchTextBytes)
        return fail("too large to be a CFS patch", 0);
    for (char c : text) // binary data (another plugin's state, random bytes): stop early, clearly
        if (static_cast<unsigned char>(c) < 0x20 && c != '\n' && c != '\r' && c != '\t')
            return fail("contains binary data: not a CFS patch file", 0);

    PatchLoad r;
    bool header = false, ended = false;
    std::array<bool, kParams.size()> seen{};
    int lineNo = 0;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t end = std::min(text.find('\n', pos), text.size());
        const std::string_view line = trim(text.substr(pos, end - pos));
        pos = end + 1;
        ++lineNo;
        if (line.empty() || line.front() == '#')
            continue;

        if (header && line == "end") {
            ended = true;
            break; // anything after "end" is ignored (room for future trailers)
        }
        if (!header) {
            constexpr std::string_view kHeader = "CFS patch ";
            int version = 0;
            const char* vEnd = line.data() + line.size();
            if (!line.starts_with(kHeader) ||
                std::from_chars(line.data() + kHeader.size(), vEnd, version).ptr != vEnd || version < 1)
                return fail("not a CFS patch (the first line must be \"CFS patch <version>\")", lineNo);
            // A NEWER version is read best-effort: ids never change meaning, so the ids we know still mean
            // what we think, and the ones we don't are skipped (counted in unknownIds).
            r.version = version;
            header = true;
            continue;
        }

        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos)
            return fail("expected \"<id> = <value>\"", lineNo);
        const std::string_view key = trim(line.substr(0, eq));
        if (key == "name") { // the rest of the line, verbatim ('#' allowed: "Bell #2")
            r.name = std::string(trim(line.substr(eq + 1)));
            continue;
        }
        std::string_view value = line.substr(eq + 1);
        value = trim(value.substr(0, value.find('#')));

        std::uint32_t id = 0;
        if (key.empty() || std::from_chars(key.data(), key.data() + key.size(), id).ptr != key.data() + key.size())
            return fail("the key must be a parameter id (a whole number)", lineNo);
        float v = 0.0f;
        const char* first = value.data();
        if (!value.empty() && *first == '+')
            ++first;
        const auto [vp, ec] = std::from_chars(first, value.data() + value.size(), v);
        if (value.empty() || ec != std::errc{} || vp != value.data() + value.size() || !std::isfinite(v))
            return fail("the value must be a finite number", lineNo);

        const ParamInfo* p = findParam(id);
        if (!p) {
            ++r.unknownIds;
            continue;
        }
        const auto index = static_cast<std::size_t>(p - kParams.data());
        if (seen[index])
            return fail("parameter " + std::to_string(id) + " appears twice", lineNo);
        seen[index] = true;
        if (clampToRange(*p, v) != v)
            ++r.clampedValues;
        setParam(r.patch, *p, v);
    }
    if (!header)
        return fail("empty, or not a CFS patch (no \"CFS patch <version>\" line)", 0);
    if (!ended)
        return fail("incomplete: no \"end\" line (was the file cut off while saving?)", 0);
    for (bool s : seen)
        r.missingIds += s ? 0 : 1;
    migrate(r.version, r.patch);
    r.ok = true;
    return r;
}

} // namespace fss::synth
