// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <string>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

struct HostConfig {
  // UTF-8 absolute paths. resourceRoot contains Effects/ and optionally Data/.
  std::string resourceRoot;
  std::string defaultEffect;
  bool rootConfigured = false;
  bool channelsConfigured = false;
  bool lockEffect = false;
  bool allowEdit = true;
  int inputs = 2;
  int outputs = 2;
  int channels = 2;
  int midiFlags = 3;
  int parameterCount = 256;
  std::string appendName;
  int variantId = 0;
  bool instrument = false;
};

HostConfig loadHostConfig(HINSTANCE module);
// Public path-based entry point enables isolated configuration tests without loading a DLL.
HostConfig loadHostConfigForPath(const std::wstring& moduleFile);

