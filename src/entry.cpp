// SPDX-License-Identifier: LGPL-2.1-or-later
#include <clap/clap.h>
extern bool CLAP_ABI modern_init(const char*);
extern void CLAP_ABI modern_deinit();
extern const void* CLAP_ABI modern_factory(const char*);
extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
  CLAP_VERSION, modern_init, modern_deinit, modern_factory
};
