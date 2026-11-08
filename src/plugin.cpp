// SPDX-License-Identifier: LGPL-2.1-or-later
#include "runtime.h"
#include "config.h"
#include "diagnostics.h"
#include "reaper_host.h"
#include "library_menu.h"
#include "../upstream/reaper-sdk/sdk/reaper_plugin.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>
#include <memory>
#include <filesystem>
#include <fstream>
#include <clap/ext/draft/gain-adjustment-metering.h>
#include <clap/ext/note-name.h>
#include "../upstream/clap-wrapper/include/clapwrapper/vst3.h"

namespace {
HINSTANCE moduleHandle; std::mutex entryMutex; unsigned entryReferences=0;
HostConfig moduleConfig;
std::string pluginName="ReaJSFX",pluginId="org.modernreajs.host";
thread_local bool processingCallback=false; struct ProcessingScope { bool old=processingCallback; ProcessingScope(){processingCallback=true;} ~ProcessingScope(){processingCallback=old;} };
const char* features[]={CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,CLAP_PLUGIN_FEATURE_NOTE_EFFECT,CLAP_PLUGIN_FEATURE_STEREO,nullptr};
const char* instrumentFeatures[]={CLAP_PLUGIN_FEATURE_INSTRUMENT,CLAP_PLUGIN_FEATURE_SYNTHESIZER,nullptr};
clap_plugin_descriptor_t descriptor={CLAP_VERSION,"org.modernreajs.host","ReaJSFX","Cockos / zaibuyidao",
  "https://github.com/zaibuyidao/ReaJSFX","","",REAJSFX_VERSION,"JSFX host for Windows x64",features};
std::wstring wide(const std::string& s) { if(s.empty())return {}; int n=MultiByteToWideChar(CP_UTF8,0,s.c_str(),-1,nullptr,0);std::wstring w(n,0);MultiByteToWideChar(CP_UTF8,0,s.c_str(),-1,w.data(),n);w.resize(n-1);return w; }
std::string utf8(const wchar_t* w) { int n=WideCharToMultiByte(CP_UTF8,0,w,-1,nullptr,0,nullptr,nullptr);std::string s(n,0);WideCharToMultiByte(CP_UTF8,0,w,-1,s.data(),n,nullptr,nullptr);s.resize(n-1);return s; }
struct Packet {uint32_t time=0,offset=0,size=0;uint16_t port=0;};
struct Host {
  clap_plugin_t plugin{}; const clap_host_t* host;
  ModernRuntime rt; std::recursive_mutex mutex;
  bool active=false; double sampleRate=48000; uint32_t maxFrames=0,channels=2;
  int lastPlayState=-1;
  uint32_t inputChannels=2,outputChannels=2;
  std::vector<double> audio;
  HWND window=nullptr,pathEdit=nullptr,status=nullptr; uint32_t width=900,height=650;
  HFONT toolbarFont=nullptr; LOGFONTW toolbarFontSpec{};
  int (*themeColor)(int)=nullptr;
  std::array<COLORREF,6> toolbarColors{};
  static constexpr UINT_PTR themeTimer=0x524a;
  std::string pendingPath,lastError;
  std::array<std::atomic<double>,256> values{};
  std::array<std::atomic<bool>,256> dirty{};
  std::array<std::atomic<unsigned>,256> gestures{};
  std::array<bool,256> gestureActive{};
  std::atomic<double> gainMeter{0};
  std::atomic<bool> noteRefreshPending{false};
  uint64_t noteRefreshFrames=0;
  std::vector<clap_note_name_t> cachedNoteNames;
  std::vector<modern_midi_label> cachedLabels;
  const reaper_plugin_info_t* reaperInfo=nullptr;
  const modern_reaper_host* reaperBridge=nullptr;
  std::array<int,256> map{};
  // Keep ordinary blocks allocation-free, but retain grown capacity for bursts.
  static constexpr uint32_t maxMidiEvents=1024*1024,maxMidiBytes=64*1024*1024;
  std::vector<Packet> incoming=std::vector<Packet>(4096),outgoing=std::vector<Packet>(4096);
  std::vector<uint32_t> midiOrder=std::vector<uint32_t>(4096);
  std::atomic<bool> midiErrorPending{false};
  std::vector<uint8_t> inBytes,outBytes;
  uint32_t inCount=0,inRead=0,outCount=0,inUsed=0,outUsed=0,frames=0;
  bool midiRead=false; std::atomic<uint32_t> latencyValue{0}; std::atomic<bool> latencyPending{false};
  // Preserve the requested delay across a host's stop/reset/deactivate/activate.
  // The official runtime clears its last observed delay on reset, before @init
  // can restore pdc_delay. Querying that transient zero would restart forever.
  std::atomic<int> requestedLatency{-1};
  Host(const clap_host_t* h):host(h) {map.fill(-1);channels=moduleConfig.channels;inputChannels=moduleConfig.inputs;outputChannels=moduleConfig.outputs;inBytes.resize(1024*1024);outBytes.resize(1024*1024);rt.callbacks(midi,param,this);}
  ~Host(){destroyUI();}
  template<typename T> const T* extension(const char* name) const{return static_cast<const T*>(host->get_extension(host,name));}
  static void* getReaperApi(void* context,const char* name){auto& s=*static_cast<Host*>(context);if(s.reaperInfo&&s.reaperInfo->GetFunc)return s.reaperInfo->GetFunc(name);return s.reaperBridge?s.reaperBridge->get_func(s.host,name):nullptr;}
  static void* getReaperParent(void* context,int selector){auto& s=*static_cast<Host*>(context);if(s.reaperBridge)return s.reaperBridge->get_context(s.host,selector);auto getter=reinterpret_cast<void* (*)(const clap_host_t*,int)>(getReaperApi(&s,"clap_get_reaper_context"));return getter?getter(s.host,selector):nullptr;}
  void connectReaper(){
    reaperInfo=extension<reaper_plugin_info_t>("cockos.reaper_extension");
    if(reaperInfo&&reaperInfo->caller_version!=REAPER_PLUGIN_VERSION)reaperInfo=nullptr;
    reaperBridge=extension<modern_reaper_host>(MODERN_REAPER_HOST);
    if(!reaperInfo&&!reaperBridge)return;
    void* context=nullptr;
    if(reaperBridge)context=reaperBridge->get_context(host,4);
    else if(auto getter=reinterpret_cast<void* (*)(const clap_host_t*,int)>(getReaperApi(this,"clap_get_reaper_context")))context=getter(host,4);
    if(runtime_bind_reaper(getReaperApi,this))rt.hostContext(context,getReaperParent,this);
  }
  void mapping(){map.fill(-1);for(int i=0;i<rt.parameterCount();++i){int s=rt.sliderIndex(i);if(s>=0&&s<256)map[s]=i;}cache();}
  double normalized(int s){if(map[s]<0)return 0;double lo=0,hi=1,v=rt.parameter(map[s],&lo,&hi);return hi>lo?std::clamp((v-lo)/(hi-lo),0.,1.):0;}
  void cache(){for(int s=0;s<256;++s)values[s].store(normalized(s),std::memory_order_relaxed);}
  bool load(const char* file,const char* root="") {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    connectReaper();
    std::string resolvedRoot=root?root:"";
    if(resolvedRoot.empty()&&moduleConfig.rootConfigured)resolvedRoot=moduleConfig.resourceRoot;
    bool ok=rt.load(file,resolvedRoot);lastError=ok?"":rt.error();mapping();
    if(window){rt.attach(window);SetWindowTextW(pathEdit,wide(rt.path()).c_str());setStatus(lastError);}
    return ok;
  }
  void notify(){if(auto p=extension<clap_host_params_t>(CLAP_EXT_PARAMS))p->rescan(host,CLAP_PARAM_RESCAN_VALUES|CLAP_PARAM_RESCAN_TEXT);if(auto s=extension<clap_host_state_t>(CLAP_EXT_STATE))s->mark_dirty(host);}
  void refreshNoteNames(){
    std::lock_guard<std::recursive_mutex> guard(mutex);
    noteRefreshPending=false;
    std::vector<clap_note_name_t> next;
    std::vector<modern_midi_label> labels;
    for(int i=0;i<rt.noteNameCount();++i){ModernRuntime::NoteName n;if(!rt.noteName(i,n))continue;modern_midi_label label{};label.mode=n.mode;label.note=n.note;label.channel=n.channel;snprintf(label.name,sizeof(label.name),"%s",n.name.c_str());labels.push_back(label);if(n.mode!=0||n.note>127)continue;clap_note_name_t v{};v.port=-1;v.key=(int16_t)n.note;v.channel=(int16_t)n.channel;snprintf(v.name,sizeof(v.name),"%s",n.name.c_str());next.push_back(v);}
    bool different=next.size()!=cachedNoteNames.size();
    if(!different)for(size_t i=0;i<next.size();++i)if(next[i].key!=cachedNoteNames[i].key||next[i].channel!=cachedNoteNames[i].channel||strcmp(next[i].name,cachedNoteNames[i].name)){different=true;break;}
    if(labels.size()!=cachedLabels.size())different=true;
    else for(size_t i=0;i<labels.size();++i)if(labels[i].mode!=cachedLabels[i].mode||labels[i].note!=cachedLabels[i].note||labels[i].channel!=cachedLabels[i].channel||strcmp(labels[i].name,cachedLabels[i].name)){different=true;break;}
    if(different){cachedLabels=std::move(labels);cachedNoteNames=std::move(next);if(auto h=extension<clap_host_note_name_t>(CLAP_EXT_NOTE_NAME))h->changed(host);}
  }
  static Host& self(const clap_plugin_t* p){return *static_cast<Host*>(p->plugin_data);}
  static void param(void* ctx,int index,bool done){auto& s=*static_cast<Host*>(ctx);int physical=s.rt.sliderIndex(index);if(physical<0||physical>=moduleConfig.parameterCount)return;s.values[physical].store(s.normalized(physical));s.dirty[physical].store(true);if(!processingCallback){s.gestures[physical].fetch_or(done?2u:1u);if(auto h=s.extension<clap_host_params_t>(CLAP_EXT_PARAMS))h->request_flush(s.host);}}
  void apply(const clap_input_events_t* events) {
    if(!events)return;
    std::array<bool,256> seen{};
    for(uint32_t i=0;i<events->size(events);++i) {
      auto e=events->get(events,i);
      if(!e||e->space_id!=CLAP_CORE_EVENT_SPACE_ID||e->type!=CLAP_EVENT_PARAM_VALUE||e->size<sizeof(clap_event_param_value_t))continue;
      auto p=reinterpret_cast<const clap_event_param_value_t*>(e);
      if(p->param_id>=(uint32_t)moduleConfig.parameterCount||!std::isfinite(p->value)||map[p->param_id]<0)continue;
      const auto id=p->param_id;
      const double incoming=std::clamp(p->value,0.,1.);
      const double current=normalized(id);
      // A host can return a script's own slider_automate value, including a
      // float32-normalized copy. Reapplying it would snap an arbitrary script
      // output to the UI step in sx_setParmVal. Only suppress the first t=0
      // echo: later points and sample-offset automation remain explicit input.
      const bool echo=!seen[id]&&e->time==0&&
        (incoming==current||incoming==double(float(current)));
      seen[id]=true;
      if(echo){values[id].store(current);continue;}
      double lo=0,hi=1;rt.parameter(map[id],&lo,&hi);
      rt.setParameter(map[id],lo+incoming*(hi-lo),std::min(e->time,frames?frames-1:0));
      values[id].store(incoming);
    }
  }

  void emitParams(const clap_output_events_t* out) {
    if(!out)return;
    for(int i=0;i<moduleConfig.parameterCount;++i) if(dirty[i].exchange(false)) {
      unsigned gesture=gestures[i].exchange(0);
      if(gesture && !gestureActive[i]) {clap_event_param_gesture_t e{};e.header={sizeof(e),0,0,CLAP_EVENT_PARAM_GESTURE_BEGIN,CLAP_EVENT_IS_LIVE};e.param_id=i;if(!out->try_push(out,&e.header)){dirty[i].store(true);gestures[i].fetch_or(gesture);continue;}gestureActive[i]=true;}
      clap_event_param_value_t e{};e.header={sizeof(e),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_PARAM_VALUE,CLAP_EVENT_IS_LIVE};e.param_id=i;e.note_id=-1;e.port_index=e.channel=e.key=-1;e.value=values[i].load();
      if(!out->try_push(out,&e.header)){dirty[i].store(true);gestures[i].fetch_or(gesture);continue;}
      if((gesture&2u) && gestureActive[i]) {clap_event_param_gesture_t e{};e.header={sizeof(e),0,0,CLAP_EVENT_PARAM_GESTURE_END,CLAP_EVENT_IS_LIVE};e.param_id=i;if(out->try_push(out,&e.header))gestureActive[i]=false;else{dirty[i].store(true);gestures[i].fetch_or(2u);}}
    }
  }
  bool midiError(const char* message){
    if(!midiErrorPending.exchange(true)){lastError=rt.path()+": MIDI "+message;host->request_callback(host);}
    return false;
  }
  bool reserveMidi(bool input,uint32_t size){
    auto count=input?inCount:outCount;auto used=input?inUsed:outUsed;
    auto& storage=input?inBytes:outBytes;auto& list=input?incoming:outgoing;
    if(count>=maxMidiEvents)return midiError(input?"input exceeds 1048576 events per block":"output exceeds 1048576 events per block");
    if(size>maxMidiBytes-used)return midiError(input?"input exceeds 64 MiB per block":"output exceeds 64 MiB per block");
    try{
      if(count>=list.size())list.resize(std::min<size_t>(maxMidiEvents,std::max<size_t>(count+1,list.size()*2)));
      if(!input&&midiOrder.size()<list.size())midiOrder.resize(list.size());
      if(size>storage.size()-used)storage.resize(std::min<size_t>(maxMidiBytes,std::max<size_t>(size_t(used)+size,storage.size()*2)));
    }catch(const std::bad_alloc&){return midiError(input?"input buffer allocation failed":"output buffer allocation failed");}
    return true;
  }
  bool packet(bool input,uint32_t time,uint16_t port,const uint8_t* bytes,uint32_t size){
    if(!size)return false;if(!reserveMidi(input,size))return false;
    auto& count=input?inCount:outCount;auto& used=input?inUsed:outUsed;
    auto& storage=input?inBytes:outBytes;auto& list=input?incoming:outgoing;
    list[count++]={std::min(time,frames?frames-1:0),used,size,port};
    memcpy(storage.data()+used,bytes,size);used+=size;return true;
  }
  void collect(const clap_input_events_t* list){inCount=inRead=outCount=inUsed=outUsed=0;midiRead=false;if(!list||!(moduleConfig.midiFlags&1))return;for(uint32_t i=0;i<list->size(list);++i){auto e=list->get(list,i);if(!e||e->space_id)continue;if(e->type==CLAP_EVENT_MIDI&&e->size>=sizeof(clap_event_midi_t)){auto m=(const clap_event_midi_t*)e;packet(true,e->time,m->port_index,m->data,3);}else if(e->type==CLAP_EVENT_MIDI_SYSEX&&e->size>=sizeof(clap_event_midi_sysex_t)){auto m=(const clap_event_midi_sysex_t*)e;if(m->buffer)packet(true,e->time,m->port_index,m->buffer,m->size);}else if((e->type==CLAP_EVENT_NOTE_ON||e->type==CLAP_EVENT_NOTE_OFF||e->type==CLAP_EVENT_NOTE_CHOKE)&&e->size>=sizeof(clap_event_note_t)){auto n=(const clap_event_note_t*)e;uint8_t b[3]={(uint8_t)((e->type==CLAP_EVENT_NOTE_ON?0x90:0x80)|(n->channel&15)),(uint8_t)(n->key&127),(uint8_t)std::clamp(int(n->velocity*127+.5),0,127)};packet(true,e->time,(uint16_t)std::max(0,int(n->port_index)),b,3);}}}
  static double midi(void* context,int action,double* ts,double* a,double* b,double* bus){auto& s=*static_cast<Host*>(context);if(action==0){return 0;}if(action<0||action==0x101){s.midiRead=true;while(s.inRead<s.inCount){auto& p=s.incoming[s.inRead++];auto bytes=s.inBytes.data()+p.offset;
      // A null bus pointer is the official runtime's ext_midi_bus=0 contract:
      // expose bus 0 only; pass other buses through without playing them again.
      if(!bus&&p.port!=0){s.packet(false,p.time,p.port,bytes,p.size);continue;}
      if(action<0&&p.size>3){s.packet(false,p.time,p.port,bytes,p.size);continue;}if(action==0x101){int capacity=*(int*)a;if(p.size>(uint32_t)std::max(0,capacity)){s.packet(false,p.time,p.port,bytes,p.size);continue;}*(int*)a=p.size;*(uint8_t**)b=bytes;}else{*a=bytes[0];*b=(p.size>1?bytes[1]:0)+(p.size>2?bytes[2]*256:0);}*ts=p.time;if(bus)*bus=p.port;return 1;}return 0;}
    if(!(moduleConfig.midiFlags&2))return 0;uint32_t time=ts?(uint32_t)std::max(0.,*ts):0;uint16_t port=bus?(uint16_t)std::clamp(*bus,0.,15.):0;
    if(action==1){uint8_t bytes[3]={(uint8_t)(int)*a,(uint8_t)(int)*b,(uint8_t)((int)*b>>8)};return s.packet(false,time,port,bytes,3)?1:0;}
    if(action==0x100){int size=*(int*)a;if(size<=0||!s.reserveMidi(false,(uint32_t)size))return 0;*(uint8_t**)b=s.outBytes.data()+s.outUsed;s.outgoing[s.outCount++]={std::min(time,s.frames?s.frames-1:0),s.outUsed,(uint32_t)size,port};s.outUsed+=size;return 1;}
    if(action==2){
      if(!std::isfinite(*b)||*b<0||*b>maxMidiBytes-2){s.midiError("invalid legacy SysEx length (maximum 67108862 payload bytes)");return 0;}
      int n=(int)*b;bool framed=n>=2&&(int)a[0]==240&&(int)a[n-1]==247;
      uint32_t size=n+(framed?0:2);if(!s.reserveMidi(false,size))return 0;
      auto bytes=s.outBytes.data()+s.outUsed;int k=0;if(!framed)bytes[k++]=240;
      for(int i=0;i<n;++i)bytes[k++]=(uint8_t)(int)a[i];if(!framed)bytes[k++]=247;
      s.outgoing[s.outCount++]={std::min(time,s.frames?s.frames-1:0),s.outUsed,size,port};s.outUsed+=size;return 1;
    }return 0;
  }
  void emitMidi(const clap_output_events_t* out){if(!(moduleConfig.midiFlags&2))return;for(uint32_t i=inRead;i<inCount;++i){auto& p=incoming[i];packet(false,p.time,p.port,inBytes.data()+p.offset,p.size);}// Stable ordering matters for same-sample program/CC/note messages. Sort
    // retained indices with the enqueue position as a tie breaker, keeping
    // the audio callback free of stable_sort's temporary heap allocation.
    auto& order=midiOrder;
    for(uint32_t i=0;i<outCount;++i)order[i]=i;
    std::sort(order.begin(),order.begin()+outCount,[&](uint32_t a,uint32_t b){return outgoing[a].time<outgoing[b].time||(outgoing[a].time==outgoing[b].time&&a<b);});
    if(!out)return;for(uint32_t i=0;i<outCount;++i){auto& p=outgoing[order[i]];auto bytes=outBytes.data()+p.offset;if(p.size<=3&&bytes[0]!=240){clap_event_midi_t e{};e.header={sizeof(e),p.time,0,CLAP_EVENT_MIDI,0};e.port_index=p.port;memcpy(e.data,bytes,p.size);if(!out->try_push(out,&e.header))midiError("host rejected an output event (host event queue full or event unsupported)");}else{clap_event_midi_sysex_t e{};e.header={sizeof(e),p.time,0,CLAP_EVENT_MIDI_SYSEX,0};e.port_index=p.port;e.buffer=bytes;e.size=p.size;if(!out->try_push(out,&e.header))midiError("host rejected an output event (host event queue full or event unsupported)");}}}
  clap_process_status process(const clap_process_t* p){ProcessingScope scope;std::lock_guard<std::recursive_mutex> guard(mutex);frames=p->frames_count;if(frames>maxFrames)return CLAP_PROCESS_ERROR;apply(p->in_events);collect(p->in_events);for(uint32_t f=0;f<frames;++f)for(uint32_t c=0;c<channels;++c){double v=0;if(p->audio_inputs_count&&c<p->audio_inputs[0].channel_count){auto& b=p->audio_inputs[0];if(b.data64)v=b.data64[c][f];else if(b.data32)v=b.data32[c][f];}audio[f*channels+c]=v;}
    double tempo=120,sec=0,beats=0,state=0;int num=4,den=4;if(auto t=p->transport){if(t->flags&CLAP_TRANSPORT_HAS_TEMPO)tempo=t->tempo;if(t->flags&CLAP_TRANSPORT_HAS_SECONDS_TIMELINE)sec=double(t->song_pos_seconds)/CLAP_SECTIME_FACTOR;if(t->flags&CLAP_TRANSPORT_HAS_BEATS_TIMELINE)beats=double(t->song_pos_beats)/CLAP_BEATTIME_FACTOR;if(t->flags&CLAP_TRANSPORT_HAS_TIME_SIGNATURE){num=t->tsig_num;den=t->tsig_denom;}if(t->flags&CLAP_TRANSPORT_IS_PLAYING)state=1;if(t->flags&CLAP_TRANSPORT_IS_RECORDING)state+=4;}
    // JSFX runs @init on a transport start (unless ext_noinit opts out).
    // CLAP hosts need not call reset() when the transport starts, so bridge the
    // transition explicitly. Upstream reset preserves ext_noinit semantics.
    if(p->transport&&lastPlayState>=0&&!(lastPlayState&1)&&((int)state&1))rt.reset();
    if(p->transport)lastPlayState=(int)state;
    rt.process(audio.data(),frames,channels,(int)sampleRate,tempo,num,den,state,sec,beats);gainMeter.store(rt.gainReduction());noteRefreshFrames+=frames;if(noteRefreshFrames>=(uint64_t)std::max(1.,sampleRate/4)){noteRefreshFrames=0;if(!noteRefreshPending.exchange(true))host->request_callback(host);}cache();emitParams(p->out_events);if(p->audio_outputs_count){auto& out=p->audio_outputs[0];for(uint32_t c=0;c<out.channel_count;++c)for(uint32_t f=0;f<frames;++f){double v=c<channels?audio[f*channels+c]:0;if(out.data64)out.data64[c][f]=v;else if(out.data32)out.data32[c][f]=(float)v;}out.constant_mask=0;}emitMidi(p->out_events);auto l=rt.latency();if(l!=(int)latencyValue.load()){requestedLatency.store(l);if(!latencyPending.exchange(true))host->request_restart(host);}return CLAP_PROCESS_CONTINUE;}
  void setStatus(const std::string& message){
    if(!status)return;
    SetWindowTextW(status,wide(message).c_str());
    layout();
  }
  // The native VST2 editor is a dialog which REAPER can theme. This custom
  // VST3/CLAP container must explicitly use the host's overridden system colors.
  // Resolve per instance, leaving other hosts and older REAPER versions native.
  void refreshToolbarTheme(bool force=false){
    if(!window||!themeColor)return;
    const int indices[]={COLOR_BTNFACE,COLOR_BTNTEXT,COLOR_WINDOW,COLOR_WINDOWTEXT,
      COLOR_GRAYTEXT,COLOR_BTNSHADOW};
    auto next=toolbarColors;
    for(size_t i=0;i<next.size();++i)next[i]=(COLORREF)themeColor(indices[i]);
    if(!force&&next==toolbarColors)return;
    toolbarColors=next;
    InvalidateRect(window,nullptr,TRUE);
    for(int id:{100,102,103})if(auto child=GetDlgItem(window,id))RedrawWindow(child,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_FRAME);
  }
  LRESULT toolbarControlColor(HDC dc,HWND control){
    const bool edit=control==pathEdit;
    const COLORREF bg=toolbarColors[edit?2:0];
    SetTextColor(dc,toolbarColors[IsWindowEnabled(control)?(edit?3:1):4]);
    SetBkColor(dc,bg);SetDCBrushColor(dc,bg);
    return (LRESULT)GetStockObject(DC_BRUSH);
  }
  void drawLoadButton(const DRAWITEMSTRUCT& item){
    const bool pressed=(item.itemState&ODS_SELECTED)!=0;
    RECT bounds=item.rcItem;
    SetDCBrushColor(item.hDC,toolbarColors[0]);
    FillRect(item.hDC,&bounds,(HBRUSH)GetStockObject(DC_BRUSH));
    SetDCBrushColor(item.hDC,toolbarColors[5]);
    FrameRect(item.hDC,&bounds,(HBRUSH)GetStockObject(DC_BRUSH));
    if(pressed)OffsetRect(&bounds,1,1);
    SetBkMode(item.hDC,TRANSPARENT);
    SetTextColor(item.hDC,toolbarColors[(item.itemState&ODS_DISABLED)?4:1]);
    auto previous=SelectObject(item.hDC,toolbarFont?toolbarFont:GetStockObject(DEFAULT_GUI_FONT));
    DrawTextW(item.hDC,L"Load",-1,&bounds,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    SelectObject(item.hDC,previous);
    if((item.itemState&ODS_FOCUS)&&!(item.itemState&ODS_NOFOCUSRECT)){
      bounds=item.rcItem;InflateRect(&bounds,-3,-3);DrawFocusRect(item.hDC,&bounds);
    }
  }
  void layout(){
    if(!window)return;
    // Use the upstream dialog's exact font when loaded. Before loading, use
    // its resource definition (8-point MS Shell Dlg), not a stock system font.
    LOGFONTW font{};
    auto nativeDialog=FindWindowExW(window,nullptr,L"#32770",nullptr);
    auto nativeFont=nativeDialog?(HFONT)SendMessageW(GetDlgItem(nativeDialog,1062),WM_GETFONT,0,0):nullptr;
    if(!nativeFont||GetObjectW(nativeFont,sizeof(font),&font)!=sizeof(font)){
      HDC dc=GetDC(window);const int dpi=dc?GetDeviceCaps(dc,LOGPIXELSY):96;if(dc)ReleaseDC(window,dc);
      font.lfHeight=-MulDiv(8,dpi,72);font.lfWeight=FW_NORMAL;font.lfCharSet=DEFAULT_CHARSET;
      wcscpy_s(font.lfFaceName,L"MS Shell Dlg");
    }
    if(!toolbarFont||memcmp(&font,&toolbarFontSpec,sizeof(font))){
      if(auto replacement=CreateFontIndirectW(&font)){
        auto previous=toolbarFont;toolbarFont=replacement;toolbarFontSpec=font;
        for(int id:{102,103})SendMessageW(GetDlgItem(window,id),WM_SETFONT,(WPARAM)toolbarFont,TRUE);
        if(previous)DeleteObject(previous);
      }
    }
    MoveWindow(pathEdit,8,6,width-82,24,TRUE);
    MoveWindow(GetDlgItem(window,102),width-68,6,60,24,TRUE);
    const bool hasStatus=status&&GetWindowTextLengthW(status)>0;
    const int top=hasStatus?72:36;
    MoveWindow(status,8,34,width-16,34,TRUE);
    ShowWindow(status,hasStatus?SW_SHOWNA:SW_HIDE);
    const int bodyHeight=std::max(1,int(height)-top);
    rt.resize(width,bodyHeight);
    if(auto child=FindWindowExW(window,nullptr,L"#32770",nullptr)){
      SetWindowPos(child,nullptr,0,top,width,bodyHeight,SWP_NOZORDER|SWP_NOACTIVATE);
      if(!moduleConfig.allowEdit)ShowWindow(GetDlgItem(child,1062),SW_HIDE);
    }
  }
  void library(){
    if(moduleConfig.lockEffect)return;
    std::vector<std::string> files;
    auto root=std::filesystem::u8path(moduleConfig.resourceRoot)/"Effects";
    HMENU menu=modern::makeLibraryMenu(root,files);
    if(!menu){setStatus("No JSFX scripts found in "+root.u8string()+". Place scripts in this directory or configure rootpath in ReaJSFX.ini.");return;}
    RECT r{};GetWindowRect(GetDlgItem(window,102),&r);
    auto choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,r.left,r.bottom,0,window,nullptr);
    DestroyMenu(menu);
    if(choice>1000&&choice<=1000+files.size())requestLoad(files[choice-1001]);
  }
  void requestLoad(std::string file){
    if(moduleConfig.lockEffect)return;
    if(active){pendingPath=std::move(file);setStatus("Waiting for host to restart processing and load the script...");host->request_restart(host);}
    else{load(file.c_str());notify();}
  }
  static LRESULT CALLBACK wndproc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
    auto s=(Host*)GetWindowLongPtrW(w,GWLP_USERDATA);
    if(msg==WM_NCCREATE){s=(Host*)((CREATESTRUCTW*)lp)->lpCreateParams;SetWindowLongPtrW(w,GWLP_USERDATA,(LONG_PTR)s);}
    if(!s)return DefWindowProcW(w,msg,wp,lp);
    if(msg==WM_TIMER&&wp==themeTimer){s->refreshToolbarTheme();return 0;}
    if(msg==WM_THEMECHANGED||msg==WM_SYSCOLORCHANGE||msg==WM_SETTINGCHANGE)s->refreshToolbarTheme(true);
    if(s->themeColor){
      if(msg==WM_ERASEBKGND){RECT r;GetClientRect(w,&r);SetDCBrushColor((HDC)wp,s->toolbarColors[0]);FillRect((HDC)wp,&r,(HBRUSH)GetStockObject(DC_BRUSH));return 1;}
      if(msg==WM_CTLCOLOREDIT||msg==WM_CTLCOLORSTATIC)return s->toolbarControlColor((HDC)wp,(HWND)lp);
      if(msg==WM_DRAWITEM&&wp==102&&lp){s->drawLoadButton(*(const DRAWITEMSTRUCT*)lp);return TRUE;}
    }
    if(msg==WM_SIZE){s->width=LOWORD(lp);s->height=HIWORD(lp);s->layout();return 0;}
    if(msg==WM_COMMAND&&LOWORD(wp)==102&&HIWORD(wp)==BN_CLICKED){s->library();return 0;}
    return DefWindowProcW(w,msg,wp,lp);
  }
  bool createUI(){
    if(window)return true;
    connectReaper();
    themeColor=reinterpret_cast<decltype(themeColor)>(getReaperApi(this,"GSC_mainwnd"));
    WNDCLASSW wc{};wc.hInstance=moduleHandle;wc.lpfnWndProc=wndproc;wc.lpszClassName=L"ReaJSFXHost";
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);RegisterClassW(&wc);
    window=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"ReaJSFX",WS_POPUP|WS_CLIPCHILDREN,0,0,width,height,nullptr,nullptr,moduleHandle,this);
    if(!window)return false;
    refreshToolbarTheme(true);
    pathEdit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",wide(rt.path()).c_str(),WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL|ES_READONLY,0,0,100,24,window,(HMENU)100,moduleHandle,nullptr);
    CreateWindowW(L"BUTTON",L"Load",WS_CHILD|WS_VISIBLE|WS_TABSTOP|(themeColor?BS_OWNERDRAW:0),0,0,60,24,window,(HMENU)102,moduleHandle,nullptr);
    status=CreateWindowW(L"STATIC",wide(lastError).c_str(),WS_CHILD,0,0,100,34,window,(HMENU)103,moduleHandle,nullptr);
    for(int id:{100,102,103})SendMessageW(GetDlgItem(window,id),WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),FALSE);
    rt.attach(window);layout();
    if(moduleConfig.lockEffect)EnableWindow(GetDlgItem(window,102),FALSE);
    // Hosts need not broadcast Windows theme messages to plug-in children.
    // Poll only while the editor exists and repaint only on a palette change.
    if(themeColor)SetTimer(window,themeTimer,250,nullptr);
    return true;
  }
  void destroyUI(){rt.detach();if(window){KillTimer(window,themeTimer);DestroyWindow(window);}window=pathEdit=status=nullptr;if(toolbarFont)DeleteObject(toolbarFont);toolbarFont=nullptr;themeColor=nullptr;}
};

const clap_plugin_params_t params={
 [](const clap_plugin_t*)->uint32_t{return moduleConfig.parameterCount;},
 [](const clap_plugin_t* p,uint32_t i,clap_param_info_t* info)->bool{if(i>=(uint32_t)moduleConfig.parameterCount)return false;*info={};info->id=i;info->flags=CLAP_PARAM_IS_AUTOMATABLE|CLAP_PARAM_REQUIRES_PROCESS;snprintf(info->name,sizeof(info->name),"Slider %u",i+1);info->min_value=0;info->max_value=1;info->default_value=0;return true;},
 [](const clap_plugin_t* p,clap_id i,double* v)->bool{if(i>=(uint32_t)moduleConfig.parameterCount)return false;*v=Host::self(p).values[i].load();return true;},
 [](const clap_plugin_t* p,clap_id i,double v,char* text,uint32_t size)->bool{if(i>=(uint32_t)moduleConfig.parameterCount||!size)return false;auto& s=Host::self(p);std::lock_guard<std::recursive_mutex> g(s.mutex);if(s.map[i]>=0){double lo=0,hi=1;s.rt.parameter(s.map[i],&lo,&hi);double raw=lo+v*(hi-lo);auto name=s.rt.parameterName(s.map[i]);auto value=s.rt.parameterText(s.map[i],&raw);snprintf(text,size,"%s",value.c_str());}else snprintf(text,size,"%.4f",v);return true;},
 [](const clap_plugin_t* p,clap_id i,const char* text,double* v)->bool{if(i>=(uint32_t)moduleConfig.parameterCount)return false;auto& s=Host::self(p);std::lock_guard<std::recursive_mutex> g(s.mutex);if(s.map[i]<0){char* end=nullptr;double raw=strtod(text,&end);if(end==text||*end||!std::isfinite(raw))return false;*v=std::clamp(raw,0.,1.);return true;}double raw;if(!s.rt.parameterFromText(s.map[i],text,&raw))return false;double lo=0,hi=1;s.rt.parameter(s.map[i],&lo,&hi);*v=hi>lo?std::clamp((raw-lo)/(hi-lo),0.,1.):0;return true;},
 [](const clap_plugin_t* p,const clap_input_events_t* in,const clap_output_events_t* out){ProcessingScope scope;auto& s=Host::self(p);std::lock_guard<std::recursive_mutex> g(s.mutex);s.frames=0;s.apply(in);s.emitParams(out);}
};
const clap_plugin_audio_ports_t ports={[](const clap_plugin_t* p,bool input)->uint32_t{auto& s=Host::self(p);return (input?s.inputChannels:s.outputChannels)>0?1:0;},[](const clap_plugin_t* p,uint32_t i,bool input,clap_audio_port_info_t* info)->bool{if(i)return false;*info={};info->id=input?0:1;strcpy(info->name,input?"JSFX input":"JSFX output");info->flags=CLAP_AUDIO_PORT_IS_MAIN|CLAP_AUDIO_PORT_SUPPORTS_64BITS|CLAP_AUDIO_PORT_REQUIRES_COMMON_SAMPLE_SIZE;info->channel_count=input?Host::self(p).inputChannels:Host::self(p).outputChannels;if(!info->channel_count)return false;info->port_type=info->channel_count==1?CLAP_PORT_MONO:info->channel_count==2?CLAP_PORT_STEREO:nullptr;info->in_place_pair=Host::self(p).inputChannels==Host::self(p).outputChannels?(input?1:0):CLAP_INVALID_ID;return true;}};
const uint32_t layouts[]={1,2,4,8,16,32,64};
// Explicit INI pin counts are fixed, like official ReaJS. Otherwise expose every
// supported track width: a powers-of-two-only list made REAPER choose 8 for a
// 12-channel track, silently losing DrNine channels 9-11.
static constexpr clap_id configuredLayout=0x10000;
const clap_plugin_audio_ports_config_t portConfigs={
 [](const clap_plugin_t*)->uint32_t{return moduleConfig.channelsConfigured?1:64;},
 [](const clap_plugin_t*,uint32_t i,clap_audio_ports_config_t* c)->bool{
  if(i>=(moduleConfig.channelsConfigured?1u:64u))return false;
  *c={};c->id=moduleConfig.channelsConfigured?configuredLayout:i+1;
  unsigned ni=moduleConfig.channelsConfigured?moduleConfig.inputs:i+1;
  unsigned no=moduleConfig.channelsConfigured?moduleConfig.outputs:i+1;
  snprintf(c->name,sizeof(c->name),"%u in / %u out",ni,no);
  c->input_port_count=ni?1:0;c->output_port_count=no?1:0;
  c->has_main_input=ni>0;c->has_main_output=no>0;
  c->main_input_channel_count=ni;c->main_output_channel_count=no;
  c->main_input_port_type=ni==1?CLAP_PORT_MONO:ni==2?CLAP_PORT_STEREO:nullptr;
  c->main_output_port_type=no==1?CLAP_PORT_MONO:no==2?CLAP_PORT_STEREO:nullptr;return true;
 },
 [](const clap_plugin_t* p,clap_id id)->bool{
  auto& s=Host::self(p);if(s.active)return false;
  if(moduleConfig.channelsConfigured){if(id!=configuredLayout)return false;s.inputChannels=moduleConfig.inputs;s.outputChannels=moduleConfig.outputs;s.channels=moduleConfig.channels;}
  else {if(id<1||id>64)return false;s.channels=s.inputChannels=s.outputChannels=id;}return true;
 }};
const clap_plugin_note_ports_t notes={[](const clap_plugin_t*,bool input)->uint32_t{return (moduleConfig.midiFlags&(input?1:2))?16:0;},[](const clap_plugin_t*,uint32_t i,bool,clap_note_port_info_t* info)->bool{if(i>=16)return false;*info={};info->id=i;info->supported_dialects=CLAP_NOTE_DIALECT_MIDI;info->preferred_dialect=CLAP_NOTE_DIALECT_MIDI;snprintf(info->name,sizeof(info->name),"MIDI bus %u",i+1);return true;}};
bool writeAll(const clap_ostream_t* stream,const uint8_t* data,size_t size){while(size){auto n=stream->write(stream,data,size);if(n<=0||(uint64_t)n>size)return false;data+=n;size-=n;}return true;}
// MJS2 adds host-window geometry around the unchanged MJS1 runtime payload.
// Existing native MJS1 project states remain readable; this is not VST2 import.
const clap_plugin_state_t state={
 [](const clap_plugin_t* p,const clap_ostream_t* stream)->bool{
  auto& s=Host::self(p);std::lock_guard<std::recursive_mutex> g(s.mutex);
  auto payload=s.rt.save();if(payload.empty())return false;
  std::vector<uint8_t> bytes={'M','J','S','2'};
  for(uint32_t value:{std::clamp(s.width,420u,65535u),std::clamp(s.height,300u,65535u)})
   for(int i=0;i<4;++i)bytes.push_back(uint8_t(value>>(i*8)));
  bytes.insert(bytes.end(),payload.begin(),payload.end());
  return writeAll(stream,bytes.data(),bytes.size());
 },
 [](const clap_plugin_t* p,const clap_istream_t* stream)->bool{
  auto& s=Host::self(p);std::vector<uint8_t> bytes;uint8_t b[8192];
  for(;;){auto n=stream->read(stream,b,sizeof(b));if(n<0||n>sizeof(b)||bytes.size()+n>64*1024*1024)return false;if(!n)break;bytes.insert(bytes.end(),b,b+n);}
  std::lock_guard<std::recursive_mutex> g(s.mutex);
  size_t offset=0;uint32_t width=s.width,height=s.height;
  if(bytes.size()>=4&&!memcmp(bytes.data(),"MJS2",4)){
   if(bytes.size()<16){s.lastError="Truncated JSFX window state";s.setStatus(s.lastError);return false;}
   auto u32=[&](size_t n){uint32_t v=0;for(int i=0;i<4;++i)v|=uint32_t(bytes[n+i])<<(i*8);return v;};
   width=u32(4);height=u32(8);offset=12;
   if(width<420||width>65535||height<300||height>65535){s.lastError="Invalid JSFX window dimensions";s.setStatus(s.lastError);return false;}
  }
  bool ok=s.rt.restore(offset?bytes.data()+offset:bytes.data(),bytes.size()-offset,moduleConfig.resourceRoot);
  s.lastError=ok?"":s.rt.error();s.mapping();
  if(ok){s.width=width;s.height=height;}
  if(s.window){s.rt.attach(s.window);SetWindowTextW(s.pathEdit,wide(s.rt.path()).c_str());s.setStatus(s.lastError);
   if(ok){SetWindowPos(s.window,nullptr,0,0,width,height,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);if(auto h=s.extension<clap_host_gui_t>(CLAP_EXT_GUI))h->request_resize(s.host,width,height);}
  }
  if(auto h=s.extension<clap_host_params_t>(CLAP_EXT_PARAMS))h->rescan(s.host,CLAP_PARAM_RESCAN_VALUES|CLAP_PARAM_RESCAN_TEXT);
  return ok;
 }
};
const clap_plugin_gui_t gui={
 [](const clap_plugin_t*,const char* api,bool floating)->bool{return api&&!strcmp(api,CLAP_WINDOW_API_WIN32)&&!floating;},
 [](const clap_plugin_t*,const char** api,bool* floating)->bool{*api=CLAP_WINDOW_API_WIN32;*floating=false;return true;},
 [](const clap_plugin_t* p,const char* api,bool floating)->bool{return !floating&&api&&!strcmp(api,CLAP_WINDOW_API_WIN32)&&Host::self(p).createUI();},
 [](const clap_plugin_t* p){Host::self(p).destroyUI();},[](const clap_plugin_t*,double)->bool{return false;},
 [](const clap_plugin_t* p,uint32_t* w,uint32_t* h)->bool{auto& s=Host::self(p);*w=s.width;*h=s.height;return true;},
 [](const clap_plugin_t*)->bool{return true;},[](const clap_plugin_t*,clap_gui_resize_hints_t* h)->bool{*h={true,true,false,0,0};return true;},
 [](const clap_plugin_t*,uint32_t* w,uint32_t* h)->bool{*w=std::max(420u,*w);*h=std::max(300u,*h);return true;},
 [](const clap_plugin_t* p,uint32_t w,uint32_t h)->bool{auto& s=Host::self(p);if(w<420||h<300)return false;s.width=w;s.height=h;if(s.window)SetWindowPos(s.window,nullptr,0,0,w,h,SWP_NOMOVE|SWP_NOZORDER);return true;},
 [](const clap_plugin_t* p,const clap_window_t* w)->bool{auto& s=Host::self(p);if(!s.window||strcmp(w->api,CLAP_WINDOW_API_WIN32))return false;SetWindowLongPtrW(s.window,GWL_STYLE,WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS);SetParent(s.window,(HWND)w->win32);return true;},
 [](const clap_plugin_t*,const clap_window_t*)->bool{return false;},[](const clap_plugin_t*,const char*){},
 [](const clap_plugin_t* p)->bool{auto w=Host::self(p).window;if(!w)return false;ShowWindow(w,SW_SHOW);return true;},
 [](const clap_plugin_t* p)->bool{auto w=Host::self(p).window;if(!w)return false;ShowWindow(w,SW_HIDE);return true;}
};
const clap_plugin_latency_t latency={[](const clap_plugin_t* p)->uint32_t{return Host::self(p).latencyValue.load();}};
const clap_plugin_tail_t tail={[](const clap_plugin_t* p)->uint32_t{double n=Host::self(p).rt.tail();return n<0?UINT32_MAX:(uint32_t)std::min(n,double(UINT32_MAX));}};
const clap_plugin_gain_adjustment_metering_t gainMeterExtension={[](const clap_plugin_t* p)->double{return Host::self(p).gainMeter.load();}};
const clap_plugin_note_name_t noteNames={[](const clap_plugin_t* p)->uint32_t{return (uint32_t)Host::self(p).cachedNoteNames.size();},[](const clap_plugin_t* p,uint32_t i,clap_note_name_t* n)->bool{auto& names=Host::self(p).cachedNoteNames;if(i>=names.size()||!n)return false;*n=names[i];return true;}};
const modern_midi_labels midiLabels={[](const clap_plugin_t* p)->uint32_t{return (uint32_t)Host::self(p).cachedLabels.size();},[](const clap_plugin_t* p,uint32_t i,modern_midi_label* n)->bool{auto& labels=Host::self(p).cachedLabels;if(i>=labels.size()||!n)return false;*n=labels[i];return true;}};
const modern_diagnostics diagnostics={[](const clap_plugin_t* p,const char* path,const char* root)->bool{auto& s=Host::self(p);if(s.active||!path)return false;return s.load(path,root);},[](const clap_plugin_t* p)->const char*{return Host::self(p).lastError.c_str();},[](const clap_plugin_t* p,const char* v)->double{return Host::self(p).rt.peek(v);}};
const void* CLAP_ABI extensions(const clap_plugin_t*,const char* id){if(!strcmp(id,MODERN_MIDI_LABELS))return &midiLabels;if(!strcmp(id,CLAP_EXT_GAIN_ADJUSTMENT_METERING))return &gainMeterExtension;if(!strcmp(id,CLAP_EXT_NOTE_NAME))return &noteNames;if(!strcmp(id,CLAP_EXT_PARAMS))return &params;if(!strcmp(id,CLAP_EXT_AUDIO_PORTS))return &ports;if(!strcmp(id,CLAP_EXT_AUDIO_PORTS_CONFIG))return &portConfigs;if(!strcmp(id,CLAP_EXT_NOTE_PORTS))return &notes;if(!strcmp(id,CLAP_EXT_STATE))return &state;if(!strcmp(id,CLAP_EXT_GUI))return &gui;if(!strcmp(id,CLAP_EXT_LATENCY))return &latency;if(!strcmp(id,CLAP_EXT_TAIL))return &tail;if(!strcmp(id,MODERN_DIAGNOSTICS))return &diagnostics;return nullptr;}
const clap_plugin_t* CLAP_ABI create(const clap_plugin_factory_t*,const clap_host_t* host,const char* id){if(!host||!clap_version_is_compatible(host->clap_version)||strcmp(id,descriptor.id))return nullptr;try{auto s=new Host(host);s->plugin={&descriptor,s,
 [](const clap_plugin_t* p)->bool{auto& s=Host::self(p);s.connectReaper();if(!moduleConfig.defaultEffect.empty())s.load(moduleConfig.defaultEffect.c_str());return true;},[](const clap_plugin_t* p){delete &Host::self(p);},
 [](const clap_plugin_t* p,double sr,uint32_t,uint32_t max)->bool{auto& s=Host::self(p);if(!std::isfinite(sr)||sr<1||sr>2147483647||!max||max>1048576)return false;s.connectReaper();try{s.audio.resize(size_t(max)*s.channels);}catch(...){return false;}s.sampleRate=sr;s.maxFrames=max;if(!s.rt.prepare((int)sr,s.channels))return false;s.mapping();s.active=true;const int requested=s.requestedLatency.exchange(-1);auto oldLatency=s.latencyValue.load();if(requested>=0)s.latencyValue.store((uint32_t)requested);s.latencyPending=false;if(oldLatency!=s.latencyValue.load())if(auto h=s.extension<clap_host_latency_t>(CLAP_EXT_LATENCY))h->changed(s.host);return true;},
 [](const clap_plugin_t* p){auto& s=Host::self(p);s.active=false;s.gainMeter=0;if(!s.pendingPath.empty()){auto path=std::move(s.pendingPath);s.pendingPath.clear();s.load(path.c_str());s.notify();}},
 [](const clap_plugin_t*)->bool{return true;},[](const clap_plugin_t*){},[](const clap_plugin_t* p){auto& s=Host::self(p);std::lock_guard<std::recursive_mutex> g(s.mutex);s.rt.reset();},
 [](const clap_plugin_t* p,const clap_process_t* process)->clap_process_status{try{return Host::self(p).process(process);}catch(...){return CLAP_PROCESS_ERROR;}},extensions,
 [](const clap_plugin_t* p){auto& s=Host::self(p);s.refreshNoteNames();if(s.midiErrorPending.exchange(false)){std::lock_guard<std::recursive_mutex> guard(s.mutex);s.setStatus(s.lastError);}}
 };return &s->plugin;}catch(...){return nullptr;}}
const clap_plugin_factory_t factory={[](const clap_plugin_factory_t*)->uint32_t{return 1;},[](const clap_plugin_factory_t*,uint32_t i)->const clap_plugin_descriptor_t*{return i?nullptr:&descriptor;},create};
// A MIDI-capable effect is not necessarily a synthesizer. Override only VST3
// classification; leave CLAP note-effect support and generated component IDs intact.
const clap_plugin_info_as_vst3_t effectVst3Info={nullptr,nullptr,"Fx|Tools"};
const clap_plugin_info_as_vst3_t instrumentVst3Info={nullptr,nullptr,"Instrument|Synth"};
const clap_plugin_factory_as_vst3_t vst3FactoryInfo={nullptr,nullptr,nullptr,
 [](const clap_plugin_factory_as_vst3_t*,uint32_t index)->const clap_plugin_info_as_vst3_t*{return index?nullptr:(moduleConfig.instrument?&instrumentVst3Info:&effectVst3Info);},nullptr};
}
bool CLAP_ABI modern_init(const char*){std::lock_guard<std::mutex> guard(entryMutex);if(entryReferences++)return true;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)&modern_init,&moduleHandle);moduleConfig=loadHostConfig(moduleHandle);pluginName="ReaJSFX"+moduleConfig.appendName;pluginId="org.modernreajs.host";if(moduleConfig.variantId)pluginId+=".variant."+std::to_string((uint32_t)moduleConfig.variantId);descriptor.name=pluginName.c_str();descriptor.id=pluginId.c_str();descriptor.features=moduleConfig.instrument?instrumentFeatures:features;runtime_init(moduleHandle);return true;}
void CLAP_ABI modern_deinit(){std::lock_guard<std::mutex> guard(entryMutex);if(entryReferences&&!--entryReferences)runtime_shutdown();}
const void* CLAP_ABI modern_factory(const char* id){if(!strcmp(id,CLAP_PLUGIN_FACTORY_ID))return &factory;if(!strcmp(id,CLAP_PLUGIN_FACTORY_INFO_VST3)||!strcmp(id,CLAP_PLUGIN_FACTORY_INFO_VST3_V1))return &vst3FactoryInfo;return nullptr;}








