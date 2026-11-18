// plugin.h: CFS as a CLAP plugin (L3.2). The only things the outside world needs: the descriptor (what the
// factory advertises) and a way to make an instance (what the factory's create_plugin calls).
//
// Everything else a host reaches through the returned clap_plugin's function pointers and get_extension().
// See docs/PLUGIN-LIFECYCLE.md for the call order and the thread of every call.
#pragma once

#include <clap/plugin.h>

namespace fss::plugin {

/// id, name, vendor, version, features. The id is permanent: hosts store it in every project that uses CFS.
extern const clap_plugin_descriptor kDescriptor;

/// A new, not yet initialized instance (the host calls plugin->init() next). Owned by the host until it
/// calls plugin->destroy(). Never returns null for a valid host pointer.
[[nodiscard]] const clap_plugin* createPlugin(const clap_host* host);

} // namespace fss::plugin
