// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <clap/clap.h>
inline constexpr char MODERN_REAPER_HOST[]="org.modernreajs.reaper-host/1";
struct modern_reaper_host {
  void* (CLAP_ABI *get_func)(const clap_host_t*,const char*);
  void* (CLAP_ABI *get_context)(const clap_host_t*,uint32_t);
  void* (CLAP_ABI *extended)(const clap_host_t*,uint32_t,void*,void*,void*);
};
inline constexpr char MODERN_MIDI_LABELS[]="org.modernreajs.midi-labels/1";
struct modern_midi_label { int32_t mode,note,channel;char name[256]; };
struct modern_midi_labels {
  uint32_t (CLAP_ABI *count)(const clap_plugin_t*);
  bool (CLAP_ABI *get)(const clap_plugin_t*,uint32_t,modern_midi_label*);
};
