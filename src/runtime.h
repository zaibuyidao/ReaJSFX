// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
class SX_Instance;
void runtime_init(HINSTANCE module);
void runtime_shutdown();
using RuntimeApiGetter=void* (*)(void*,const char*);
using RuntimeParentGetter=void* (*)(void*,int);
bool runtime_bind_reaper(RuntimeApiGetter getter,void* context);
void* runtime_reaper_api(const char* name);
void* runtime_parent_context(SX_Instance* instance,int selector);
class ModernRuntime {
public:
  using MidiCallback = double (*)(void*,int,double*,double*,double*,double*);
  using ParamCallback = void (*)(void*,int,bool);
  ModernRuntime();
  ~ModernRuntime();
  ModernRuntime(const ModernRuntime&)=delete;
  ModernRuntime& operator=(const ModernRuntime&)=delete;
  bool load(const std::string& absolutePath, const std::string& resourceRoot="");
  const std::string& error() const { return error_; }
  const std::string& path() const { return path_; }
  const std::string& root() const { return root_; }
  SX_Instance* instance() const { return inst_; }
  void callbacks(MidiCallback midi, ParamCallback param, void* context);
  void hostContext(void* realFxDsp,RuntimeParentGetter parentGetter=nullptr,void* parentContext=nullptr);
  // Main-thread activation: establish the host rate before any audio is rendered.
  bool prepare(int sampleRate,int channels);
  int process(double* interleaved,int frames,int channels,int sampleRate,double tempo=120,int numerator=4,int denominator=4,double playState=1,double seconds=0,double beats=0);
  void reset();
  int parameterCount() const;
  double parameter(int index,double* min=nullptr,double* max=nullptr,double* step=nullptr) const;
  void setParameter(int index,double value,int offset=0);
  std::string parameterName(int index) const;
  std::string parameterText(int index,double* value=nullptr) const;
  bool parameterFromText(int index,const char* text,double* value) const;
  double parameterDefault(int index) const;
  bool parameterIsEnum(int index) const;
  int sliderIndex(int parameter) const;
  std::vector<uint8_t> save();
  bool restore(const uint8_t* data,size_t size,const std::string& fallbackRoot="");
  HWND attach(HWND parent);
  void detach();
  void resize(int width,int height);
  int latency() const;
  double tail() const;
  // Call with the same host lock used for process/load; values are copied, never mutated.
  double gainReduction() const;
  struct NoteName { int mode=0, note=0, channel=-1; std::string name; };
  int noteNameCount() const;
  bool noteName(int index, NoteName& result) const;
  double peek(const char* variable,bool* exists=nullptr) const;
private:
  SX_Instance* inst_=nullptr;
  int sampleRate_=44100,channelCount_=2;
  bool processed_=false;
  std::string path_,root_,error_;
  MidiCallback midi_=nullptr;
  ParamCallback param_=nullptr;
  void* context_=nullptr;
  void* hostContext_=nullptr;
  RuntimeParentGetter parentGetter_=nullptr;
  void* parentContext_=nullptr;
};
