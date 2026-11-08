// SPDX-License-Identifier: LGPL-2.1-or-later
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <string>
#include <vector>
#include "config.h"

namespace {
namespace fs = std::filesystem;
using Ini = std::map<std::string, std::string>;
std::string utf8(const std::wstring& value) {
  if (value.empty()) return {};
  int length = WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), nullptr, 0, nullptr, nullptr);
  if (length <= 0) return {};
  std::string result(length, '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), result.data(), length, nullptr, nullptr);
  return result;
}
std::string lower(std::string value) {
  for (auto& c : value) c = (char)std::tolower((unsigned char)c);
  return value;
}
std::string trim(std::string value) {
  auto blank = [](unsigned char c) { return std::isspace(c) != 0; };
  const auto first = std::find_if_not(value.begin(), value.end(), blank);
  const auto last = std::find_if_not(value.rbegin(), value.rend(), blank).base();
  return first < last ? std::string(first, last) : std::string();
}
std::string parseValue(std::string text) {
  text = trim(text);
  char quote = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (i == 0 && (c == '"' || c == '\'')) quote = c;
    else if (quote && c == quote) quote = 0;
    if (!quote && (c == ';' || c == '#') && (i == 0 || std::isspace((unsigned char)text[i - 1]))) {
      text.resize(i); break;
    }
  }
  text = trim(text);
  if (text.size() >= 2 && (text.front() == '"' || text.front() == '\'') && text.front() == text.back())
    text = text.substr(1, text.size() - 2);
  return text;
}
bool isFile(const fs::path& path) {
  std::error_code error;
  return fs::is_regular_file(path, error);
}
bool hasEffects(const fs::path& root) {
  std::error_code error;
  return fs::is_directory(root / L"Effects", error);
}
fs::path absolutePath(const fs::path& path) {
  std::error_code error;
  const auto result = fs::absolute(path, error);
  return (error ? path : result).lexically_normal();
}
Ini readIni(const fs::path& path) {
  Ini values;
  std::ifstream file(path, std::ios::binary);
  if (!file) return values;
  file.seekg(0, std::ios::end);
  const auto length = file.tellg();
  if (length < 0 || length > 1024 * 1024) return values;
  file.seekg(0);
  std::string text((size_t)length, '\0');
  if (length && !file.read(text.data(), length)) return values;
  if (text.size() >= 2 && (unsigned char)text[0] == 0xff && (unsigned char)text[1] == 0xfe) {
    std::wstring wide;
    for (size_t i = 2; i + 1 < text.size(); i += 2)
      wide.push_back((wchar_t)((unsigned char)text[i] | ((unsigned char)text[i + 1] << 8)));
    text = utf8(wide);
  } else {
    if (text.size() >= 3 && text.compare(0, 3, "\xef\xbb\xbf") == 0) text.erase(0, 3);
    if (!text.empty() && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), (int)text.size(), nullptr, 0)) {
      const int n = MultiByteToWideChar(CP_ACP, 0, text.data(), (int)text.size(), nullptr, 0);
      std::wstring wide(n, L'\0');
      if (n > 0) MultiByteToWideChar(CP_ACP, 0, text.data(), (int)text.size(), wide.data(), n);
      text = utf8(wide);
    }
  }
  bool selected = false;
  for (size_t start = 0; start <= text.size();) {
    const size_t end = text.find('\n', start);
    std::string line = trim(text.substr(start, end == std::string::npos ? end : end - start));
    if (!line.empty() && line[0] != ';' && line[0] != '#') {
      if (line[0] == '[') {
        const auto close = line.find(']');
        selected = close != std::string::npos && lower(trim(line.substr(1, close - 1))) == "reajs";
      } else if (selected) {
        const auto equal = line.find('=');
        if (equal != std::string::npos) values[lower(trim(line.substr(0, equal)))] = parseValue(line.substr(equal + 1));
      }
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return values;
}
int integer(const Ini& ini, const char* key, int fallback) {
  const auto found = ini.find(key);
  if (found == ini.end() || found->second.empty()) return fallback;
  try {
    size_t end = 0;
    const long long number = std::stoll(found->second, &end, 10);
    if (end != found->second.size()) return fallback;
    return (int)std::clamp(number, (long long)std::numeric_limits<int>::min(), (long long)std::numeric_limits<int>::max());
  } catch (...) { return fallback; }
}
fs::path registryRoot(HKEY hive, REGSAM view) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(hive, L"Software\\REAPER", 0, KEY_QUERY_VALUE | view, &key) != ERROR_SUCCESS) return {};
  DWORD type = 0, bytes = 0;
  if (RegQueryValueExW(key, nullptr, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS ||
      (type != REG_SZ && type != REG_EXPAND_SZ) || bytes > 65536) { RegCloseKey(key); return {}; }
  std::vector<wchar_t> data(bytes / sizeof(wchar_t) + 2, L'\0');
  const auto result = RegQueryValueExW(key, nullptr, nullptr, &type, reinterpret_cast<BYTE*>(data.data()), &bytes);
  RegCloseKey(key);
  if (result != ERROR_SUCCESS) return {};
  if (type == REG_EXPAND_SZ) {
    const DWORD n = ExpandEnvironmentStringsW(data.data(), nullptr, 0);
    if (!n || n > 32768) return {};
    std::vector<wchar_t> expanded(n, L'\0');
    if (!ExpandEnvironmentStringsW(data.data(), expanded.data(), n)) return {};
    return fs::path(expanded.data());
  }
  return fs::path(data.data());
}
fs::path discoverRoot(const fs::path& moduleDirectory) {
  for (fs::path root = moduleDirectory; !root.empty();) {
    if (hasEffects(root)) return root;
    auto next = root.parent_path(); if (next == root) break; root = next;
  }
  for (HKEY hive : { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE }) {
    for (REGSAM view : { KEY_WOW64_64KEY, KEY_WOW64_32KEY }) {
      auto root = registryRoot(hive, view);
      if (!root.empty() && hasEffects(root)) return absolutePath(root);
    }
  }
  const DWORD n = GetEnvironmentVariableW(L"APPDATA", nullptr, 0);
  if (n && n < 32768) {
    std::vector<wchar_t> data(n, L'\0');
    if (GetEnvironmentVariableW(L"APPDATA", data.data(), n)) {
      auto root = fs::path(data.data()) / L"REAPER";
      if (hasEffects(root)) return absolutePath(root);
    }
  }
  return moduleDirectory / L"JS";
}
fs::path resolve(const fs::path& path, const fs::path& base) {
  if (path.is_absolute()) return absolutePath(path);
  // Match the original ReaJS drive-relative spelling, e.g. \SharedJS.
  if (path.has_root_directory()) return absolutePath(base.root_name() / path);
  return absolutePath(base / path);
}
} // namespace

HostConfig loadHostConfigForPath(const std::wstring& moduleFile) {
  HostConfig config;
  const auto module = absolutePath(fs::path(moduleFile));
  const auto directory = module.parent_path();
  fs::path iniPath = directory / (module.stem().wstring() + L".ini");
  if (!isFile(iniPath)) iniPath = directory / L"ReaJSFX.ini";
  if (!isFile(iniPath) && lower(directory.filename().u8string()) == "x86_64-win" &&
      lower(directory.parent_path().filename().u8string()) == "contents") {
    const auto bundle = directory.parent_path().parent_path();
    if (lower(bundle.extension().u8string()) == ".vst3") {
      auto adjacent = bundle.parent_path() / (bundle.stem().wstring() + L".ini");
      if (!isFile(adjacent)) adjacent = bundle.parent_path() / L"ReaJSFX.ini";
      if (isFile(adjacent)) iniPath = adjacent;
    }
  }
  const auto ini = readIni(iniPath);
  const auto rootSetting = ini.find("rootpath");
  config.rootConfigured = rootSetting != ini.end() && !rootSetting->second.empty();
  const auto root = config.rootConfigured ? resolve(fs::u8path(rootSetting->second), iniPath.parent_path()) : discoverRoot(directory);
  config.resourceRoot = root.u8string();
  const auto effectSetting = ini.find("defaulteffect");
  if (effectSetting != ini.end()) {
    std::string effect = effectSetting->second;
    if (!effect.empty() && effect[0] == '!') { config.lockEffect = true; effect = trim(effect.substr(1)); }
    if (!effect.empty()) config.defaultEffect = resolve(fs::u8path(effect), root / L"Effects").u8string();
    else config.lockEffect = false;
  }
  config.allowEdit = integer(ini, "preventedit", 0) == 0;
  config.midiFlags = std::clamp(integer(ini, "midiflags", 3), 0, 3);
  config.channelsConfigured = ini.count("inputs") != 0 || ini.count("outputs") != 0;
  config.inputs = std::clamp(integer(ini, "inputs", 2), 0, 64);
  config.outputs = std::clamp(integer(ini, "outputs", 2), 0, 64);
  config.channels = std::max(1, std::max(config.inputs, config.outputs));
  config.parameterCount = std::clamp(integer(ini, "numparms", 256), 0, 256);
  if (auto name=ini.find("appendname"); name!=ini.end()) config.appendName=name->second;
  config.variantId=integer(ini,"vstid",0);
  config.instrument=integer(ini,"vstcat",1)==2;
  return config;
}

HostConfig loadHostConfig(HINSTANCE module) {
  std::vector<wchar_t> path(1024);
  for (;;) {
    const DWORD length = GetModuleFileNameW(module, path.data(), (DWORD)path.size());
    if (!length) return {};
    if (length < path.size() - 1) return loadHostConfigForPath(std::wstring(path.data(), length));
    if (path.size() >= 32768) return {};
    path.resize(path.size() * 2);
  }
}


