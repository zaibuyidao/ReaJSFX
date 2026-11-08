"""Idempotent ReaJSFX VST3 host-services and MIDI bus adaptation."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]/'upstream/clap-wrapper/src'
def change(file, callback):
    p=root/file
    before=p.read_text(encoding='utf-8')
    after=callback(before)
    p.write_text(after,encoding='utf-8')
def once(s, old, new, marker):
    if marker in s: return s
    if old not in s: raise RuntimeError('Unknown wrapper source for '+marker)
    return s.replace(old,new,1)

def proxy_header(s):
    s=once(s,'  virtual ~IHost() = default;','''  virtual ~IHost() = default;
  // ReaJSFX host services stay per-instance, never a process-global host.
  virtual bool has_reaper_host() const { return false; }
  virtual void *reaper_function(const char *) { return nullptr; }
  virtual void *reaper_context(uint32_t) { return nullptr; }
  virtual void *reaper_extended(uint32_t, void *, void *, void *) { return nullptr; }
  virtual void note_names_changed() {}
''','ReaJSFX host services')
    s=once(s,'  void schnick();','''  // ReaJSFX: called by the static CLAP host extension trampolines.
  bool has_reaper_host() const { return _parentHost->has_reaper_host(); }
  void *reaper_function(const char *name) { return _parentHost->reaper_function(name); }
  void *reaper_context(uint32_t which) { return _parentHost->reaper_context(which); }
  void *reaper_extended(uint32_t call,void *a,void *b,void *c) { return _parentHost->reaper_extended(call,a,b,c); }
  void note_names_changed() { _parentHost->note_names_changed(); }
  void schnick();''','ReaJSFX: called by the static')
    return s
change('clap_proxy.h',proxy_header)

def proxy_source(s):
    s=once(s,'#include "clap_proxy.h"','#include "clap_proxy.h"\n#include "reaper_host.h"\n#include <clap/ext/note-name.h>','"reaper_host.h"')
    s=once(s,'  // TODO: add \'audio-ports\' host-side extension','''  // ReaJSFX: normalize VST3 REAPER access to the inner CLAP host ABI.
  static const modern_reaper_host reaper = {
    [](const clap_host_t *h,const char *name)->void* { return static_cast<Plugin*>(h->host_data)->reaper_function(name); },
    [](const clap_host_t *h,uint32_t which)->void* { return static_cast<Plugin*>(h->host_data)->reaper_context(which); },
    [](const clap_host_t *h,uint32_t call,void *a,void *b,void *c)->void* { return static_cast<Plugin*>(h->host_data)->reaper_extended(call,a,b,c); }
  };
  static const clap_host_note_name_t noteNames = {
    [](const clap_host_t *h) { static_cast<Plugin*>(h->host_data)->note_names_changed(); }
  };
  if (!strcmp(extension,"org.modernreajs.reaper-host/1"))
    return static_cast<Plugin*>(host->host_data)->has_reaper_host() ? &reaper : nullptr;
  if (!strcmp(extension,CLAP_EXT_NOTE_NAME)) return &noteNames;
  // TODO: add 'audio-ports' host-side extension''','ReaJSFX: normalize VST3')
    return s
change('clap_proxy.cpp',proxy_source)

def wrapper_header(s):
    s=once(s,'using namespace Steinberg;','''using namespace Steinberg;
namespace ModernReaper {
using namespace Steinberg;
#include "../../reaper-sdk/sdk/reaper_vst3_interfaces.h"
}
''','namespace ModernReaper')
    s=s.replace('_IMidiMappingIDs[16][Vst::ControllerNumbers::kCountCtrlNumber]','_IMidiMappingIDs[16][16][Vst::ControllerNumbers::kCountCtrlNumber]')
    s=once(s,'  bool _useIMidiMapping = false;','''  // ReaJSFX MIDI program lists identify a concrete bus/channel.
  std::map<Vst::ProgramListID,std::pair<int,int>> _midiProgramLists;
  bool _useIMidiMapping = false;''','ReaJSFX MIDI program lists')
    s=once(s,'  //---IUnitInfo--------------------------------------------------------------------------','''  // ReaJSFX REAPER host bridge and dynamic note names.
  Steinberg::IPtr<ModernReaper::IReaperHostApplication> _reaperHost;
  bool has_reaper_host() const override { return _reaperHost != nullptr; }
  void *reaper_function(const char *name) override { return _reaperHost ? _reaperHost->getReaperApi(name) : nullptr; }
  void *reaper_context(uint32_t which) override { return _reaperHost ? _reaperHost->getReaperParent(which) : nullptr; }
  void *reaper_extended(uint32_t call,void *a,void *b,void *c) override { return _reaperHost ? _reaperHost->reaperExtended(call,a,b,c) : nullptr; }
  void note_names_changed() override;
  tresult PLUGIN_API hasProgramPitchNames(Vst::ProgramListID listId,int32 programIndex) override;
  tresult PLUGIN_API getProgramPitchName(Vst::ProgramListID listId,int32 programIndex,int16 midiPitch,Vst::String128 name) override;
  //---IUnitInfo--------------------------------------------------------------------------''','ReaJSFX REAPER host bridge')
    return s
change('wrapasvst3.h',wrapper_header)

def wrapper_source(s):
    s=once(s,'DEF_CLASS_IID(Presonus::IGainReductionInfo)','DEF_CLASS_IID(Presonus::IGainReductionInfo)\nDEF_CLASS_IID(ModernReaper::IReaperHostApplication)','DEF_CLASS_IID(ModernReaper')
    s=once(s,'  context->queryInterface(Vst::IHostApplication::iid, (void **)&vst3HostApplication);','''  context->queryInterface(Vst::IHostApplication::iid, (void **)&vst3HostApplication);
  // ReaJSFX: acquire before creating the CLAP instance; release after it.
  if (vst3HostApplication)
    vst3HostApplication->queryInterface(ModernReaper::IReaperHostApplication::iid,(void **)&_reaperHost);
  if (!_reaperHost)
    context->queryInterface(ModernReaper::IReaperHostApplication::iid,(void **)&_reaperHost);''','ReaJSFX: acquire before')
    s=once(s,'    _plugin.reset();\n  }\n\n  return super::terminate();','''    _plugin.reset();
  }
  _reaperHost.reset(); // ReaJSFX host must outlive inner plugin teardown.
  return super::terminate();''','ReaJSFX host must outlive')
    s=s.replace('busIndex == 0 && channel >= 0 && channel < 16 && midiControllerNumber >= 0','busIndex >= 0 && busIndex < 16 && busIndex < (int32)eventInputs.size() && channel >= 0 && channel < 16 && midiControllerNumber >= 0')
    s=s.replace('_IMidiMappingIDs[channel][midiControllerNumber]','_IMidiMappingIDs[busIndex][channel][midiControllerNumber]')
    s=once(s,'    _MIDIUnits.clear();','''    _MIDIUnits.clear();
    _midiProgramLists.clear(); // ReaJSFX multi-bus units, bus zero IDs unchanged.
    for (uint8_t bus = 0; bus < std::min<size_t>(16,eventInputs.size()); ++bus)
    {''','ReaJSFX multi-bus units')
    s=s.replace('auto name = fmt::format("MIDI Channel {}", channel + 1);','auto name = fmt::format("MIDI Bus {} Channel {}", bus + 1, channel + 1);')
    s=s.replace('Vst3Parameter::create(0, channel, i, x)','Vst3Parameter::create(bus, channel, i, x)')
    s=s.replace('_IMidiMappingIDs[channel][i] = x++;','_IMidiMappingIDs[bus][channel][i] = x++;')
    s=s.replace('Vst3Parameter::create(0, channel, Vst::ControllerNumbers::kCtrlProgramChange, x)','Vst3Parameter::create(bus, channel, Vst::ControllerNumbers::kCtrlProgramChange, x)')
    s=once(s,'        newUnit->setProgramListID(x);','''        newUnit->setProgramListID(x);
        _midiProgramLists[x] = {bus,channel}; // ReaJSFX pitch-name routing.''','ReaJSFX pitch-name routing')
    s=once(s,'\n  // setting up noteexpression','\n    } // ReaJSFX multi-bus loop\n\n  // setting up noteexpression','ReaJSFX multi-bus loop')
    s=s.replace('    if (busIndex == 0)\n    {\n      if ((channel >= 0) && (channel < (Steinberg::int32)_MIDIUnits.size()))','    if (busIndex >= 0 && busIndex < 16)\n    {\n      if (channel >= 0 && channel < 16 && busIndex*16+channel < (int32)_MIDIUnits.size())')
    s=s.replace('unitId = _MIDIUnits[channel];','unitId = _MIDIUnits[busIndex*16+channel];')
    if '// ReaJSFX dynamic pitch-name bridge' not in s:
        s+='''
// ReaJSFX dynamic pitch-name bridge: CLAP note labels are main-thread data.
void ClapAsVst3::note_names_changed()
{
  if (!componentHandler) return;
  Vst::IUnitHandler *handler=nullptr;
  if (componentHandler->queryInterface(Vst::IUnitHandler::iid,(void**)&handler)==kResultOk)
  {
    for (const auto &entry : _midiProgramLists) handler->notifyProgramListChange(entry.first,-1);
    handler->release();
  }
}
Steinberg::tresult PLUGIN_API ClapAsVst3::hasProgramPitchNames(Vst::ProgramListID listId,int32 programIndex)
{
  if (programIndex<0 || programIndex>=128 || !_midiProgramLists.count(listId)) return kResultFalse;
  Vst::String128 name{};
  for (int16 key=0;key<128;++key) if (getProgramPitchName(listId,programIndex,key,name)==kResultOk) return kResultTrue;
  return kResultFalse;
}
Steinberg::tresult PLUGIN_API ClapAsVst3::getProgramPitchName(Vst::ProgramListID listId,int32 programIndex,int16 midiPitch,Vst::String128 name)
{
  auto found=_midiProgramLists.find(listId);
  if (!_plugin || !_plugin->_plugin || found==_midiProgramLists.end() || programIndex<0 || programIndex>=128 || midiPitch<0 || midiPitch>=128) return kResultFalse;
  const auto *ext=(const clap_plugin_note_name_t*)_plugin->_plugin->get_extension(_plugin->_plugin,CLAP_EXT_NOTE_NAME);
  if (!ext) return kResultFalse;
  int best=-1;std::string chosen;
  for (uint32_t i=0;i<ext->count(_plugin->_plugin);++i)
  {
    clap_note_name_t n{};
    if (!ext->get(_plugin->_plugin,i,&n) || (n.port!=-1 && n.port!=found->second.first) || (n.channel!=-1 && n.channel!=found->second.second) || (n.key!=-1 && n.key!=midiPitch)) continue;
    int score=(n.port!=-1?4:0)+(n.channel!=-1?2:0)+(n.key!=-1?1:0);
    if (score>=best) { best=score;chosen=n.name; }
  }
  if (best<0 || chosen.empty()) return kResultFalse;
  stringconv::convert(chosen,name);
  return kResultOk;
}
'''
    s=once(s,'#include "wrapasvst3.h"','#include "wrapasvst3.h"\n#include <clap/ext/note-name.h>','include <clap/ext/note-name.h>')
    return s
change('wrapasvst3.cpp',wrapper_source)
change('detail/vst3/parameter.h',lambda s: once(s,'  uint8_t channel = 0;','  uint8_t bus = 0; // ReaJSFX preserves every MIDI input port\n  uint8_t channel = 0;','ReaJSFX preserves every MIDI'))
change('detail/vst3/parameter.cpp',lambda s:s.replace('uint8_t /*bus*/,','uint8_t bus,').replace('  , isMidi(true)\n  , channel(channel)','  , isMidi(true)\n  , bus(bus)\n  , channel(channel)'))
change('detail/vst3/process.cpp',lambda s:s.replace('n.midi.port_index = 0;','n.midi.port_index = param->bus; // ReaJSFX multi-bus controller input',1))

def labels_and_float64(s):
    s=once(s,'#include <clap/ext/note-name.h>','#include <clap/ext/note-name.h>\n#include "reaper_host.h"','include "reaper_host.h"')
    s=once(s,'  if (!isPresetProgramList(listId)) return super::getProgramName(listId, programIndex, name);','''  // ReaJSFX mode=2 is a program label, independent of MIDI channel.
  if (_midiProgramLists.count(listId) && _plugin && programIndex>=0 && programIndex<128)
  {
    auto labels=(const modern_midi_labels*)_plugin->_plugin->get_extension(_plugin->_plugin,MODERN_MIDI_LABELS);
    if (labels) for(uint32_t i=0;i<labels->count(_plugin->_plugin);++i)
    {
      modern_midi_label label{};
      if(labels->get(_plugin->_plugin,i,&label) && label.mode==2 && label.note==programIndex && label.name[0])
      { stringconv::convert(std::string(label.name),name);return kResultOk; }
    }
  }
  if (!isPresetProgramList(listId)) return super::getProgramName(listId, programIndex, name);''','ReaJSFX mode=2')
    s=once(s,'void ClapAsVst3::note_names_changed()\n{\n  if (!componentHandler) return;','''void ClapAsVst3::note_names_changed()
{
  // ReaJSFX CC labels use the real wrapper controller parameters.
  if (_plugin && _plugin->_plugin)
  {
    auto labels=(const modern_midi_labels*)_plugin->_plugin->get_extension(_plugin->_plugin,MODERN_MIDI_LABELS);
    std::vector<modern_midi_label> names;
    if(labels) for(uint32_t i=0;i<labels->count(_plugin->_plugin);++i)
    { modern_midi_label v{};if(labels->get(_plugin->_plugin,i,&v)&&v.mode==1)names.push_back(v); }
    for(int32 i=0;i<parameters.getParameterCount();++i)
    {
      auto p=static_cast<Vst3Parameter*>(parameters.getParameterByIndex(i));
      if(!p || !p->isMidi) continue;
      std::string title=fmt::format("MIDI/Controller {}",p->controller);int score=-1;
      for(const auto& v:names) if(v.note==p->controller && (v.channel==-1 || v.channel==p->channel))
      {int candidate=v.channel==-1?0:1;if(candidate>=score){title=v.name;score=candidate;}}
      stringconv::convert(title,p->getInfo().title);
    }
  }
  if (!componentHandler) return;
  componentHandler->restartComponent(Vst::kParamTitlesChanged);''','ReaJSFX CC labels')
    old='''  if (symbolicSampleSize != Steinberg::Vst::kSample32)
  {
    return kResultFalse;
  }
  return kResultOk;'''
    new='''  // ReaJSFX forwards float64 only when every CLAP audio port supports it.
  if (symbolicSampleSize==Vst::kSample32) return kResultOk;
  if (symbolicSampleSize!=Vst::kSample64 || !_plugin) return kResultFalse;
  auto ports=_plugin->_ext._audioports;
  if (!ports) return kResultOk;
  for(bool input:{true,false}) for(uint32_t i=0;i<ports->count(_plugin->_plugin,input);++i)
  { clap_audio_port_info_t info{};if(!ports->get(_plugin->_plugin,i,input,&info)||!(info.flags&CLAP_AUDIO_PORT_SUPPORTS_64BITS))return kResultFalse; }
  return kResultOk;'''
    s=once(s,old,new,'ReaJSFX forwards float64')
    s=s.replace('if (newSetup.symbolicSampleSize != Vst::kSample32)','if (canProcessSampleSize(newSetup.symbolicSampleSize) != kResultOk)')
    s=s.replace('  if (data.symbolicSampleSize != Steinberg::Vst::kSample32) return;','  if (data.symbolicSampleSize != Steinberg::Vst::kSample32 && data.symbolicSampleSize != Steinberg::Vst::kSample64) return;')
    s=once(s,'''      if (out.channelBuffers32[ch])
        memset(out.channelBuffers32[ch], 0, sizeof(float) * (size_t)data.numSamples);''','''      // ReaJSFX silence both supported host sample formats.
      if (data.symbolicSampleSize==Steinberg::Vst::kSample64)
      { if(out.channelBuffers64[ch]) memset(out.channelBuffers64[ch],0,sizeof(double)*(size_t)data.numSamples); }
      else if (out.channelBuffers32[ch])
        memset(out.channelBuffers32[ch], 0, sizeof(float) * (size_t)data.numSamples);''','ReaJSFX silence both')
    return s
change('wrapasvst3.cpp',labels_and_float64)
def double_buffers(s):
    for direction in ('input','output'):
        old=f'        _{direction}_ports[i].data32 = _vstdata->{direction}s[i].channelBuffers32;'
        new=f'''        // ReaJSFX float64 uses the matching union member, clearing the other.
        _{direction}_ports[i].data32 = _vstdata->symbolicSampleSize==Vst::kSample32 ? _vstdata->{direction}s[i].channelBuffers32 : nullptr;
        _{direction}_ports[i].data64 = _vstdata->symbolicSampleSize==Vst::kSample64 ? _vstdata->{direction}s[i].channelBuffers64 : nullptr;'''
        s=s.replace(old,new)
    return s
change('detail/vst3/process.cpp',double_buffers)
