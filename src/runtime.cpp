// SPDX-License-Identifier: LGPL-2.1-or-later
#include <filesystem>
#include <memory>
#include <cstring>
#include <algorithm>
#include <mutex>
#include <map>
#include <cstdlib>
#include <cmath>
#include <cctype>
#include "runtime.h"
#include "../upstream/jsfx/jsfx/sfxui.h"
// Upstream's curses UI exports an erase() macro, unrelated to std::map::erase.
#ifdef erase
#undef erase
#endif
namespace fs=std::filesystem;
namespace {
std::mutex serializationMutex,parentMutex;
struct ParentBinding {RuntimeParentGetter getter;void* context;};
// Exact VM ownership, never a process-global current take or project.
std::map<SX_Instance*,ParentBinding> parentBindings;
void bindParent(SX_Instance* instance,RuntimeParentGetter getter,void* context){
 if(!instance)return;std::lock_guard<std::mutex> lock(parentMutex);
 if(getter)parentBindings[instance]={getter,context};else parentBindings.erase(instance);
}
}
void* runtime_parent_context(SX_Instance* instance,int selector){
 ParentBinding binding{};
 {std::lock_guard<std::mutex> lock(parentMutex);auto i=parentBindings.find(instance);if(i==parentBindings.end())return nullptr;binding=i->second;}
 return binding.getter(binding.context,selector);
}
extern HINSTANCE g_hInst;
ModernRuntime::ModernRuntime()=default;
ModernRuntime::~ModernRuntime(){ detach();bindParent(inst_,nullptr,nullptr); if(inst_) sx_destroyInstance(inst_); }
bool ModernRuntime::load(const std::string& filename,const std::string& resourceRoot) {
  error_.clear();
  std::error_code ec;
  auto file=fs::absolute(fs::u8path(filename),ec).lexically_normal();
  if(ec || !fs::is_regular_file(file,ec)) {error_="File not found: "+filename;return false;}
  fs::path effects=file.parent_path(), root;
  if(!resourceRoot.empty()) {root=fs::u8path(resourceRoot);effects=root/"Effects";}
  else {
    for(auto p=file.parent_path();!p.empty();) {
      auto name=p.filename().u8string();
      if(_stricmp(name.c_str(),"Effects")==0){ effects=p;root=p.parent_path();break;}
      auto next=p.parent_path();if(next==p) break;p=next;
    }
    if(root.empty())root=file.parent_path();
  }
  auto relative=file.lexically_relative(effects);
  if(relative.empty() || relative.u8string().find("..")!=std::string::npos){effects=file.parent_path();relative=file.filename();}
  std::unique_ptr<SX_Instance> next(new SX_Instance);
  next->m_last_srate=sampleRate_;
  if(next->m_srate)*next->m_srate=sampleRate_;
  next->m_last_nch=channelCount_;
  sx_updateHostNch(next.get(),channelCount_);
  next->m_name.Set(relative.u8string().c_str());
  next->m_effectdir.Set(effects.u8string().c_str());
  next->m_datadir.Set((root/"Data").u8string().c_str());
  bool loaded=next->LoadEffect(next->m_name.Get(),SX_Instance::LOADEFFECT_RESETFLAG_CONFIGITEMS|SX_Instance::LOADEFFECT_RESETFLAG_SLIDERS,nullptr);
  if(!loaded || next->m_last_error[0]) {error_=file.u8string()+": "+next->m_last_error;return false;}
  next->DoImageLoads(false);
  sx_updateHostNch(next.get(),channelCount_);
  sx_set_midi_ctx(next.get(),midi_,context_);
  sx_set_host_ctx(next.get(),hostContext_,param_);
  detach();bindParent(inst_,nullptr,nullptr);if(inst_)sx_destroyInstance(inst_);
  inst_=next.release();path_=file.u8string();root_=root.u8string();processed_=false;
  bindParent(inst_,parentGetter_,parentContext_);
  return true;
}
void ModernRuntime::callbacks(MidiCallback midi,ParamCallback param,void* context){midi_=midi;param_=param;context_=context;if(inst_){sx_set_midi_ctx(inst_,midi,context);sx_set_host_ctx(inst_,hostContext_,param);}}
void ModernRuntime::hostContext(void* realFxDsp,RuntimeParentGetter parentGetter,void* parentContext){hostContext_=realFxDsp;parentGetter_=parentGetter;parentContext_=parentContext;if(inst_){sx_set_host_ctx(inst_,hostContext_,param_);bindParent(inst_,parentGetter_,parentContext_);}}
bool ModernRuntime::prepare(int sr,int ch){
  sampleRate_=sr;channelCount_=ch;
  // A preset can be restored before the host reveals its sample rate. Recreate
  // that not-yet-processed VM at the actual rate, preserving its parameters and
  // @serialize state. ext_noinit must not freeze the provisional 44.1kHz init.
  // Once processing has begun, live rate changes retain upstream semantics.
  if(inst_&&!processed_&&inst_->m_last_srate!=sr){
    HWND child=sx_getUIwnd(inst_),parent=child?GetParent(child):nullptr;
    auto state=save();if(!restore(state.data(),state.size()))return false;
    if(parent)attach(parent);
  }
  return true;
}
int ModernRuntime::process(double* b,int n,int ch,int sr,double tempo,int num,int den,double state,double sec,double beat){if(!inst_)return 0;sampleRate_=sr;channelCount_=ch;if(n>0)processed_=true;sx_updateHostNch(inst_,ch);return sx_processSamples(inst_,b,n,ch,sr,tempo,num,den,state,sec,beat,1,1,0);}
void ModernRuntime::reset(){if(inst_)sx_processSamples(inst_,nullptr,0,0,0,0,0,0,-1,0,0,1,1,0);}
int ModernRuntime::parameterCount()const{return sx_getNumParms(inst_);}
double ModernRuntime::parameter(int i,double* lo,double* hi,double* step)const{if(step)*step=0;return sx_getParmVal(inst_,i,lo,hi,step);}
void ModernRuntime::setParameter(int i,double v,int offset){sx_setParmVal(inst_,i,v,offset);}
std::string ModernRuntime::parameterName(int i)const{char s[512]={};sx_getParmName(inst_,i,s,sizeof(s));return s;}
std::string ModernRuntime::parameterText(int i,double* v)const{char s[512]={};sx_getParmDisplay(inst_,i,s,sizeof(s),v);return s;}
bool ModernRuntime::parameterFromText(int i,const char* s,double* v)const{if(!inst_||!s||!v||i<0||i>=parameterCount())return false;if(sx_getParmValueFromString(inst_,i,s,v))return true;char* end=nullptr;double value=std::strtod(s,&end);if(end==s||!std::isfinite(value))return false;while(*end&&std::isspace((unsigned char)*end))++end;if(*end)return false;*v=value;return true;}
int ModernRuntime::sliderIndex(int i)const{return inst_?inst_->parmToIndex(i):-1;}
bool ModernRuntime::parameterIsEnum(int i)const{return sx_parmIsEnum(inst_,i)>0;}
double ModernRuntime::parameterDefault(int i)const{if(!inst_)return 0;auto s=inst_->m_sliders.Get(sliderIndex(i));return s?s->scaleToExternal(s->default_val):0;}
namespace {
void put(std::vector<uint8_t>& out,const void* p,size_t n){uint32_t z=(uint32_t)n;for(int i=0;i<4;i++)out.push_back((uint8_t)(z>>(i*8)));if(n)out.insert(out.end(),(const uint8_t*)p,(const uint8_t*)p+n);}
bool take(const uint8_t*& p,size_t& n,std::vector<uint8_t>& out){if(n<4)return false;uint32_t len=0;for(int i=0;i<4;i++)len|=uint32_t(p[i])<<(8*i);p+=4;n-=4;if(len>n || len>64*1024*1024)return false;out.assign(p,p+len);p+=len;n-=len;return true;}
// Relocate only the saved Effects-relative identity. Never search by basename,
// or allow a state path to escape either resource tree through '..'.
bool relocatedEffect(const std::string& savedFile,const std::string& savedRoot,
                     const std::string& fallbackRoot,fs::path& candidate) {
 if(savedRoot.empty()||fallbackRoot.empty())return false;
 const auto file=fs::u8path(savedFile),root=fs::u8path(savedRoot),fallback=fs::u8path(fallbackRoot);
 if(!file.is_absolute()||!root.is_absolute()||!fallback.is_absolute())return false;
 for(const auto& path:{file,root,fallback})for(const auto& part:path)if(part==L"..")return false;
 const auto normalized=file.lexically_normal(),effects=(root/"Effects").lexically_normal();
 auto f=normalized.begin(),r=effects.begin();
 for(;r!=effects.end();++r,++f)if(f==normalized.end()||_wcsicmp(f->c_str(),r->c_str()))return false;
 fs::path relative;for(;f!=normalized.end();++f)relative/=*f;
 if(relative.empty())return false;
 candidate=fallback/"Effects"/relative;
 return true;
}
}
std::vector<uint8_t> ModernRuntime::save(){std::lock_guard<std::mutex> serialized(serializationMutex);std::vector<uint8_t> out={'M','J','S','1'};put(out,path_.data(),path_.size());put(out,root_.data(),root_.size());int n=0;const char* config=sx_saveState(inst_,&n);put(out,config,n>0?n:0);n=0;const char* state=sx_saveSerState(inst_,&n);put(out,state,n>0?n:0);return out;}
bool ModernRuntime::restore(const uint8_t* data,size_t size,const std::string& fallbackRoot){
 std::lock_guard<std::mutex> serialized(serializationMutex);
 if(!data || size<4 || memcmp(data,"MJS1",4)){error_="Invalid JSFX state header";return false;}
 data+=4;size-=4;std::vector<uint8_t> file,root,config,state;
 if(!take(data,size,file)||!take(data,size,root)||!take(data,size,config)||!take(data,size,state)||size){error_="Truncated or oversized JSFX state";return false;}
 std::string path(file.begin(),file.end()),resource(root.begin(),root.end());
 if(path.empty()){detach();bindParent(inst_,nullptr,nullptr);if(inst_)sx_destroyInstance(inst_);inst_=nullptr;path_.clear();root_.clear();return true;}
 std::error_code ec;
 if(!fs::exists(fs::u8path(path),ec)&&!ec){
  fs::path candidate;
  if(relocatedEffect(path,resource,fallbackRoot,candidate)){
   if(!fs::is_regular_file(candidate,ec)||ec){error_="File not found: "+path+"; relocated file not found: "+candidate.u8string();return false;}
   path=candidate.u8string();resource=fallbackRoot;
  }
 }
 if(!load(path,resource))return false;
 config.push_back(0);sx_loadState(inst_,(const char*)config.data());
 if(inst_->m_last_error[0]){error_=path_+": "+inst_->m_last_error;return false;}
 if(!state.empty())sx_loadSerState(inst_,(const char*)state.data(),(int)state.size());
 return true;
}
HWND ModernRuntime::attach(HWND parent){if(!inst_)return nullptr;HWND existing=sx_getUIwnd(inst_);if(existing){if(GetParent(existing)!=parent)SetParent(existing,parent);ShowWindow(existing,SW_SHOWNA);return existing;}HWND created=sx_createUI(inst_,g_hInst,parent,hostContext_);if(created)ShowWindow(created,SW_SHOWNA);return created;}
void ModernRuntime::detach(){if(inst_)sx_deleteUI(inst_);}
void ModernRuntime::resize(int w,int h){if(inst_){HWND hwnd=sx_getUIwnd(inst_);if(hwnd)SetWindowPos(hwnd,nullptr,0,0,w,h,SWP_NOZORDER|SWP_NOACTIVATE);}}
int ModernRuntime::latency()const{return sx_getCurrentLatency(inst_);}
double ModernRuntime::tail()const{return sx_getTailSize(inst_);}
double ModernRuntime::peek(const char* name,bool* exists)const{if(!inst_){if(exists)*exists=false;return 0;}return inst_->peek_effect_value(name,exists);}







double ModernRuntime::gainReduction() const {
  if(!inst_ || !inst_->m_ext_gr_meter) return 0;
  const double value=*inst_->m_ext_gr_meter;
  return std::isfinite(value) && value<=9999.0 ? value : 0;
}
int ModernRuntime::noteNameCount() const {
  if(!inst_) return 0;
  WDL_MutexLock lock(&inst_->m_string_sermutex);
  return inst_->m_note_names.GetSize();
}
bool ModernRuntime::noteName(int index,NoteName& result) const {
  if(!inst_) return false;
  WDL_MutexLock lock(&inst_->m_string_sermutex);
  int key=0; auto entry=inst_->m_note_names.EnumeratePtr(index,&key);
  if(!entry || !*entry) return false;
  result.mode=(key>>24)&255;result.note=key&255;
  result.channel=(key>>8)&65535;if(result.channel==65535)result.channel=-1;
  result.name=*entry;return true;
}
