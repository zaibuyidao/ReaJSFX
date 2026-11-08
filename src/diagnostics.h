// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <clap/clap.h>
// Optional test/diagnostic interface. Loading requires a deactivated plugin.
// Peek is for stopped processing only; never use from a concurrent audio thread.
static constexpr const char* MODERN_DIAGNOSTICS = "org.modernreajs.diagnostics/1";
struct modern_diagnostics {
  bool (CLAP_ABI *load_file)(const clap_plugin_t*,const char*,const char*);
  const char* (CLAP_ABI *error)(const clap_plugin_t*);
  double (CLAP_ABI *peek)(const clap_plugin_t*,const char*);
};
