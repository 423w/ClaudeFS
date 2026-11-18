// entry.cpp: the one symbol CFS.clap exports, `clap_entry` (L3.2).
//
// A host does: LoadLibrary("CFS.clap") → GetProcAddress("clap_entry") → read the struct → init(path) →
// get_factory(CLAP_PLUGIN_FACTORY_ID) → ask the factory for descriptors and instances.
//
// clap_entry is a VARIABLE, not a function (L3.1's surprise): a const struct, initialized at compile time,
// so no code of ours runs when the DLL is loaded. That matters during a scan, when a host may load hundreds
// of plugins just to read their names.
#include "editor_win32.h"
#include "plugin.h"
#include "plugin_instance.h"

#include <clap/clap.h>
#include <fss/util/realtime_guard.h>

#include <cstring>

namespace {

bool entryInit(const char* /*pluginPath*/)
{
    // Nothing to set up: no files, no global state. (A plugin that ships data files next to the .clap
    // would remember pluginPath here.) Since CLAP 1.2 this may be called on any thread.
    return true;
}

void entryDeinit() {}

std::uint32_t factoryCount(const clap_plugin_factory*) { return 1; }

const clap_plugin_descriptor* factoryDescriptor(const clap_plugin_factory*, std::uint32_t index)
{
    return index == 0 ? &fss::plugin::kDescriptor : nullptr;
}

const clap_plugin* factoryCreate(const clap_plugin_factory*, const clap_host* host, const char* pluginId)
{
    // Refuse hosts from an incompatible CLAP major version, and ids that aren't exactly ours (clap-validator
    // checks "id with trailing garbage" on purpose: a prefix match would be a bug).
    if (!host || !pluginId || !clap_version_is_compatible(host->clap_version) ||
        std::strcmp(pluginId, fss::plugin::kDescriptor.id) != 0)
        return nullptr;
    return fss::plugin::createPlugin(host);
}

const clap_plugin_factory kFactory = {factoryCount, factoryDescriptor, factoryCreate};

const void* entryGetFactory(const char* factoryId)
{
    return (factoryId && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0) ? &kFactory : nullptr;
}

} // namespace

extern "C" {

// CLAP_EXPORT is __declspec(dllexport) on Windows: it puts the symbol in the DLL's export table, where
// GetProcAddress finds it. extern "C" stops C++ name mangling, so the exported name is exactly "clap_entry".
CLAP_EXPORT const clap_plugin_entry clap_entry = {CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory};

// For the headless test host: draw the editor of an instance created by this DLL, find a control, save a
// screenshot. Not part of CLAP; harmless if a host never calls them. Main thread only, like clap.gui.
__declspec(dllexport) bool fss_debug_editor_render(const clap_plugin* p)
{
    auto* e = fss::plugin::FssClap::self(p)->editorWindow();
    if (e)
        e->renderNow();
    return e != nullptr;
}
__declspec(dllexport) bool fss_debug_editor_control_rect(const clap_plugin* p, std::uint32_t paramId, int* rect4)
{
    auto* e = fss::plugin::FssClap::self(p)->editorWindow();
    return e && e->controlRect(paramId, rect4);
}
__declspec(dllexport) bool fss_debug_editor_page(const clap_plugin* p, int page)
{
    // False for a page that doesn't exist, so the test host can find out how many there are.
    auto* e = fss::plugin::FssClap::self(p)->editorWindow();
    if (!e || page < 0 || page >= fss::plugin::kNumPages)
        return false;
    e->setPage(page);
    return true;
}
__declspec(dllexport) bool fss_debug_editor_snapshot(const clap_plugin* p, const char* bmpPath)
{
    auto* e = fss::plugin::FssClap::self(p)->editorWindow();
    return e && e->snapshot(bmpPath);
}

#ifdef FSS_RT_TRAP_LINKED
// Debug builds only: how many allocations happened inside a RealtimeGuard (process/flush) in this DLL.
// The test host (fss_clap_host) reads it to prove the audio path never allocates. Not part of CLAP.
__declspec(dllexport) std::uint64_t fss_debug_rt_violations() { return fss::util::realtimeViolations(); }
#endif

} // extern "C"
