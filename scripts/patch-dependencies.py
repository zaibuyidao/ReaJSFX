"""Narrow, idempotent compatibility patch for VST3 SDK 3.8.1's _iid macro argument."""
import runpy
from pathlib import Path
p=Path(__file__).resolve().parents[1]/'upstream/clap-wrapper/src/wrapasvst3.h'
s=p.read_text(encoding='utf-8')
s=s.replace('FUnknownPrivate::iidEqual(iid,', 'FUnknownPrivate::iidEqual(_iid,')
p.write_text(s,encoding='utf-8')

# Hosts may supply empty buffers for inactive buses. Validate the actual host
# array bounds before dereferencing instead of trusting the advertised layout.
p=Path(__file__).resolve().parents[1]/'upstream/clap-wrapper/src/detail/vst3/process.cpp'
s=p.read_text(encoding='utf-8')
s=s.replace('if (_vstdata->inputs[i].numChannels == (Steinberg::int32)_input_ports[i].channel_count)',
            'if (_vstdata->inputs && i < (uint32_t)_vstdata->numInputs &&\n          _vstdata->inputs[i].numChannels == (Steinberg::int32)_input_ports[i].channel_count &&\n          _vstdata->inputs[i].channelBuffers32)')
s=s.replace('if (_vstdata->outputs[i].numChannels == (Steinberg::int32)_output_ports[i].channel_count)',
            'if (_vstdata->outputs && i < (uint32_t)_vstdata->numOutputs &&\n          _vstdata->outputs[i].numChannels == (Steinberg::int32)_output_ports[i].channel_count &&\n          _vstdata->outputs[i].channelBuffers32)')
p.write_text(s,encoding='utf-8')

# Coalesce high-rate DSP output notifications instead of filling the UI gesture
# queue with meter values (small audio blocks can produce thousands per frame).
p=Path(__file__).resolve().parents[1]/'upstream/clap-wrapper/src/wrapasvst3.h'
s=p.read_text(encoding='utf-8')
needle='  ClapWrapper::detail::shared::fixedqueue<queueEvent, 8192> _queueToUI;'
if '_outputValuesChanged' not in s:
    if needle not in s: raise RuntimeError('Unknown clap-wrapper UI queue declaration')
    s=s.replace(needle,needle+'\n  std::atomic<bool> _outputValuesChanged{false}; // ReaJSFX DSP meter synchronization')
p.write_text(s,encoding='utf-8')
p=p.parent/'wrapasvst3.cpp'
s=p.read_text(encoding='utf-8')
needle='''  // receive a value change and pass it to the internal queue
  _queueToUI.push(valueEvent(value));'''
replacement='''  // ReaJSFX: DSP values need cache sync, not thousands of UI gestures.
  if (value->header.flags & CLAP_EVENT_DONT_RECORD)
  {
    _outputValuesChanged.store(true, std::memory_order_release);
    return;
  }
  // receive a value change and pass it to the internal queue
  _queueToUI.push(valueEvent(value));'''
if 'ReaJSFX: DSP values need cache sync' not in s:
    if needle not in s: raise RuntimeError('Unknown clap-wrapper value event queue')
    s=s.replace(needle,replacement)
needle='  // handling queued events\n  queueEvent n;'
replacement='''  // ReaJSFX: fetch the most recent atomic CLAP values once per UI tick.
  if (_outputValuesChanged.exchange(false, std::memory_order_acq_rel))
    syncParameterValuesFromClap();

  // handling queued events
  queueEvent n;'''
if 'ReaJSFX: fetch the most recent atomic CLAP values' not in s:
    if needle not in s: raise RuntimeError('Unknown clap-wrapper idle event queue')
    s=s.replace(needle,replacement)
p.write_text(s,encoding='utf-8')

# VST3's validator exercises the wrapper-owned controller text roundtrip.
p=Path(__file__).resolve().parents[1]/'upstream/clap-wrapper/src/wrapasvst3.cpp'
s=p.read_text(encoding='utf-8')
old='''  if (param->isWrapperOwned())
  {
    return Steinberg::kResultFalse;
  }'''
new='''  if (param->isWrapperOwned())
  {
    // ReaJSFX: invert the wrapper's MIDI/program display strings.
    const char *begin = inbuf;
    if (!strncmp(begin, "Program ", 8)) begin += 8;
    char *end = nullptr;
    const double plain = strtod(begin, &end);
    if (end == begin || *end || !std::isfinite(plain) || plain < param->min_value || plain > param->max_value)
      return Steinberg::kResultFalse;
    valueNormalized = param->asVst3Value(plain);
    return kResultOk;
  }'''
s=s.replace(old,new)
s=s.replace('auto param = (Vst3Parameter *)this->getParameterObject(id);\n  auto val =',
            'auto param = (Vst3Parameter *)this->getParameterObject(id);\n  if (!param) return kInvalidArgument;\n  auto val =')
s=s.replace('auto param = (Vst3Parameter *)this->getParameterObject(id);\n  Steinberg::String m(string);',
            'auto param = (Vst3Parameter *)this->getParameterObject(id);\n  if (!param || !string) return kInvalidArgument;\n  Steinberg::String m(string);')
p.write_text(s,encoding='utf-8')
p=p.parent/'detail/vst3/parameter.cpp'
s=p.read_text(encoding='utf-8')
s=s.replace('fullname.append("controller");','fullname.append("Controller ");\n  fullname.append(std::to_string(cc));')
s=s.replace('v.flags |= Vst::ParameterInfo::kIsProgramChange | Vst::ParameterInfo::kCanAutomate;',
            'v.flags |= Vst::ParameterInfo::kIsProgramChange | Vst::ParameterInfo::kIsList | Vst::ParameterInfo::kCanAutomate;')
p.write_text(s,encoding='utf-8')

# clap-wrapper 0.17.0 acknowledges MIDI output but drops it. Translate the MIDI
# dialect actually emitted by JSFX into the VST3 event types, keeping timestamps
# and buses. MIDI 2.0/unsupported system realtime messages fail explicitly.
p=Path(__file__).resolve().parents[1]/'upstream/clap-wrapper/src/detail/vst3/process.cpp'
s=p.read_text(encoding='utf-8')
old='''    case CLAP_EVENT_MIDI:
    case CLAP_EVENT_MIDI_SYSEX:
    case CLAP_EVENT_MIDI2:
      return true;
      break;'''
new='''    case CLAP_EVENT_MIDI: // ReaJSFX: preserve JSFX MIDI output
    {
      if (event->size < sizeof(clap_event_midi_t) || !_vstdata || !_vstdata->outputEvents)
        return false;
      const auto *m = reinterpret_cast<const clap_event_midi_t *>(event);
      Vst::Event oe{};
      oe.busIndex = m->port_index;
      oe.sampleOffset = event->time;
      oe.flags = (event->flags & CLAP_EVENT_IS_LIVE) ? Vst::Event::kIsLive : 0;
      const int status = m->data[0], channel = status & 15;
      const auto d1 = m->data[1] & 127, d2 = m->data[2] & 127;
      if ((status & 0xf0) == 0x90 && d2)
      {
        oe.type = Vst::Event::kNoteOnEvent;
        oe.noteOn.channel = channel; oe.noteOn.pitch = d1;
        oe.noteOn.velocity = d2 / 127.f; oe.noteOn.noteId = -1;
      }
      else if ((status & 0xf0) == 0x80 || (status & 0xf0) == 0x90)
      {
        oe.type = Vst::Event::kNoteOffEvent;
        oe.noteOff.channel = channel; oe.noteOff.pitch = d1;
        oe.noteOff.velocity = d2 / 127.f; oe.noteOff.noteId = -1;
      }
      else
      {
        oe.type = Vst::Event::kLegacyMIDICCOutEvent;
        oe.midiCCOut.channel = channel;
        oe.midiCCOut.value = d1; oe.midiCCOut.value2 = d2;
        switch (status & 0xf0)
        {
          case 0xa0: oe.midiCCOut.controlNumber = Vst::kCtrlPolyPressure; break;
          case 0xb0: oe.midiCCOut.controlNumber = d1; oe.midiCCOut.value = d2; oe.midiCCOut.value2 = 0; break;
          case 0xc0: oe.midiCCOut.controlNumber = Vst::kCtrlProgramChange; break;
          case 0xd0: oe.midiCCOut.controlNumber = Vst::kAfterTouch; break;
          case 0xe0: oe.midiCCOut.controlNumber = Vst::kPitchBend; break;
          case 0xf0:
            oe.midiCCOut.channel = 0;
            switch (status)
            {
              case 0xf1: oe.midiCCOut.controlNumber = Vst::kCtrlQuarterFrame; break;
              case 0xf2: oe.midiCCOut.controlNumber = Vst::kSystemSongPointer; break;
              case 0xf3: oe.midiCCOut.controlNumber = Vst::kSystemSongSelect; break;
              case 0xf6: oe.midiCCOut.controlNumber = Vst::kSystemTuneRequest; break;
              case 0xfa: oe.midiCCOut.controlNumber = Vst::kSystemMidiClockStart; break;
              case 0xfb: oe.midiCCOut.controlNumber = Vst::kSystemMidiClockContinue; break;
              case 0xfc: oe.midiCCOut.controlNumber = Vst::kSystemMidiClockStop; break;
              case 0xfe: oe.midiCCOut.controlNumber = Vst::kSystemActiveSensing; break;
              default: return false;
            }
            break;
          default: return false;
        }
      }
      return _vstdata->outputEvents->addEvent(oe) == kResultOk;
    }
    case CLAP_EVENT_MIDI_SYSEX:
    {
      if (event->size < sizeof(clap_event_midi_sysex_t) || !_vstdata || !_vstdata->outputEvents)
        return false;
      const auto *m = reinterpret_cast<const clap_event_midi_sysex_t *>(event);
      Vst::Event oe{};
      oe.type = Vst::Event::kDataEvent; oe.busIndex = m->port_index;
      oe.sampleOffset = event->time; oe.data.type = Vst::DataEvent::kMidiSysEx;
      oe.data.size = m->size; oe.data.bytes = m->buffer;
      return _vstdata->outputEvents->addEvent(oe) == kResultOk;
    }
    case CLAP_EVENT_MIDI2:
      return false;'''
if old in s:
    s=s.replace(old,new)
elif 'ReaJSFX: preserve JSFX MIDI output' not in s:
    raise RuntimeError('Unknown clap-wrapper MIDI output implementation')
# Note, poly-pressure and SysEx input buses are carried by VST3's event busIndex.
start=s.index('void ProcessAdapter::processInputEvents(')
end=s.index('bool ProcessAdapter::enqueueOutputEvent(',start)
part=s[start:end].replace('.port_index = 0;', '.port_index = vstevent.busIndex;')
s=s[:start]+part+s[end:]
s=s.replace('oe.busIndex = 0;  // FIXME - multi-out midi still needs work','oe.busIndex = nevt->port_index;')
# Ordinary automation and MIDI mapping must preserve every point in the block.
# The preset-selector branch deliberately retains its last-point-only behavior.
s=s.replace('if (k->getPoint(nums - 1, offset, value) == kResultOk)',
            'for (int32 point = 0; point < nums; ++point)\n          if (k->getPoint(point, offset, value) == kResultOk)')
p.write_text(s,encoding='utf-8')

p=p.parents[2]/'wrapasvst3.cpp'
s=p.read_text(encoding='utf-8')
s=s.replace('if (busIndex == 0)  // && channel == 0) // && midiControllerNumber == Vst::kCtrlVolume)',
            'if (busIndex == 0 && channel >= 0 && channel < 16 && midiControllerNumber >= 0)')
p.write_text(s,encoding='utf-8')

# Output parameters must also update the edit controller, on its main thread.
# performEdit() only informs IComponentHandler; it does not update Parameter.
# Reuse the lock-free UI queue. Non-gestured DSP outputs synchronize values but
# do not fabricate UI automation gestures (the host receives their output queue).
p=Path(__file__).resolve().parents[1]/'upstream/clap-wrapper/src/detail/vst3/process.cpp'
s=p.read_text(encoding='utf-8')
old='''        if (std::find(_gesturedParameters->begin(), _gesturedParameters->end(), param_id) !=
            _gesturedParameters->end())
        {
          if (_automation) _automation->onPerformEdit(ev);
        }'''
new='''        // ReaJSFX: marshal every output value to the edit controller.
        if (_automation)
        {
          clap_event_param_value_t uiValue = *ev;
          if (std::find(_gesturedParameters->begin(), _gesturedParameters->end(), param_id) ==
              _gesturedParameters->end())
            uiValue.header.flags |= CLAP_EVENT_DONT_RECORD;
          _automation->onPerformEdit(&uiValue);
        }'''
if old in s:
    s=s.replace(old,new)
elif 'ReaJSFX: marshal every output value' not in s:
    raise RuntimeError('Unknown clap-wrapper output parameter implementation')
p.write_text(s,encoding='utf-8')
p=p.parents[2]/'wrapasvst3.cpp'
s=p.read_text(encoding='utf-8')
old='''        auto v = n._data._value.value;
        performEdit(param->getInfo().id, param->asVst3Value(v));'''
new='''        if (!param) break;
        const auto normalized = param->asVst3Value(n._data._value.value);
        // ReaJSFX: the controller cache and host notification must agree.
        param->setNormalized(normalized);
        if (!(n._data._value.header.flags & CLAP_EVENT_DONT_RECORD))
          performEdit(param->getInfo().id, normalized);'''
if old in s:
    s=s.replace(old,new)
elif 'ReaJSFX: the controller cache' not in s:
    raise RuntimeError('Unknown clap-wrapper UI parameter queue implementation')
old='''        const auto normalized = param->asVst3Value(n._data._value.value);'''
new='''        // The audio thread may have published a newer value while this UI
        // notification was queued. Never overwrite that value with stale UI
        // history: hosts may feed performEdit() straight back into processing.
        double latest = n._data._value.value;
        if (_plugin->_ext._params)
          _plugin->_ext._params->get_value(_plugin->_plugin, param->id, &latest);
        const auto normalized = param->asVst3Value(latest);'''
if old in s:
    s=s.replace(old,new)
elif 'double latest = n._data._value.value;' not in s:
    raise RuntimeError('Unknown clap-wrapper stale UI queue implementation')
p.write_text(s,encoding='utf-8')


runpy.run_path(str(Path(__file__).with_name('patch-wrapper-host.py')))
