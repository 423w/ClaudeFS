// preset_library.cpp: see preset_library.h.
#include "preset_library.h"

#include <fss/synth/factory_bank.h>
#include <fss/synth/patch_io.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#include <shlobj.h> // SHGetKnownFolderPath
#include <windows.h>
#endif

namespace fss::preset_lib {

namespace {
// ImGui and patch files speak UTF-8; Windows paths are UTF-16. path::string() would use the ANSI code page
// and fail on a name like "Pad für Anna", so convert through u8string explicitly.
std::string toUtf8(const std::filesystem::path& p)
{
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}
std::filesystem::path fromUtf8(std::string_view s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }
} // namespace

std::filesystem::path userPresetDir()
{
    // Tests (and anyone who keeps presets elsewhere) can point the library at another folder.
#ifdef _WIN32
    char* over = nullptr; // _dupenv_s: MSVC's /W4 deprecates getenv (not thread-safe)
    std::size_t len = 0;
    if (_dupenv_s(&over, &len, "CFS_PRESET_DIR") == 0 && over) {
        const std::string value(over);
        std::free(over);
        if (!value.empty())
            return fromUtf8(value);
    }
#else
    if (const char* over = std::getenv("CFS_PRESET_DIR"); over && *over)
        return fromUtf8(over);
#endif
#ifdef _WIN32
    // The real Documents folder (it may be redirected to OneDrive or another drive), not a guess.
    PWSTR docs = nullptr;
    std::filesystem::path dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)))
        dir = std::filesystem::path(docs) / L"CFS Presets";
    CoTaskMemFree(docs);
    return dir;
#else
    const char* home = std::getenv("HOME");
    return home ? std::filesystem::path(home) / "CFS Presets" : std::filesystem::path("CFS Presets");
#endif
}

std::vector<PresetEntry> listPresets()
{
    std::vector<PresetEntry> out;
    for (const synth::FactoryPreset& f : synth::factoryBank())
        out.push_back({std::string(f.name), true, {}, f.text});
    std::vector<PresetEntry> user;
    std::error_code ec; // never throw from a GUI frame: a missing or unreadable folder is just "no presets"
    const std::filesystem::path dir = userPresetDir();
    if (!dir.empty() && std::filesystem::is_directory(dir, ec))
        for (const auto& e : std::filesystem::directory_iterator(dir, ec))
            if (e.is_regular_file(ec) && e.path().extension() == ".cfspatch")
                user.push_back({toUtf8(e.path().stem()), false, e.path(), {}});
    std::sort(user.begin(), user.end(), [](const PresetEntry& a, const PresetEntry& b) { return a.name < b.name; });
    out.insert(out.end(), user.begin(), user.end());
    return out;
}

bool loadPreset(const PresetEntry& entry, synth::Patch& patch, std::string& name, std::string& error)
{
    std::string text;
    if (entry.factory) {
        text = std::string(entry.text);
    } else {
        std::ifstream f(entry.path, std::ios::binary);
        if (!f) {
            error = "cannot open " + toUtf8(entry.path);
            return false;
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        text = ss.str();
    }
    const synth::PatchLoad r = synth::patchFromText(text);
    if (!r.ok) {
        error = entry.name + ": " + r.error + (r.errorLine ? " (line " + std::to_string(r.errorLine) + ")" : "");
        return false;
    }
    patch = r.patch;
    name = r.name.empty() ? entry.name : r.name;
    error.clear();
    return true;
}

bool savePreset(const synth::Patch& patch, std::string_view name, std::string& error)
{
    std::string file(name);
    for (char& c : file)
        if (std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos || static_cast<unsigned char>(c) < 0x20)
            c = '_';
    if (file.empty() || file.find_first_not_of(" .") == std::string::npos) {
        error = "give the preset a name first";
        return false;
    }
    const std::filesystem::path dir = userPresetDir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path path = dir / fromUtf8(file + ".cfspatch");
    // Write to a temporary file, then rename it over the real one: a crash mid-write leaves the old preset
    // intact instead of half a file. (The format's "end" line would catch the half file anyway; this avoids it.)
    const std::filesystem::path tmp = dir / fromUtf8(file + ".cfspatch.tmp");
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc); // binary: LF line endings, like the factory files
        f << synth::patchToText(patch, name);
        if (!f) {
            error = "cannot write " + toUtf8(tmp);
            return false;
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        error = "cannot save " + toUtf8(path) + ": " + ec.message();
        return false;
    }
    error.clear();
    return true;
}

} // namespace fss::preset_lib
