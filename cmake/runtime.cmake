# SPDX-License-Identifier: LGPL-2.1-or-later
set(JSFX_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../upstream/jsfx")
get_filename_component(JSFX_SOURCE_ROOT "${JSFX_SOURCE_ROOT}" ABSOLUTE)
set(JSFX_RUNTIME_PROJECT "${JSFX_SOURCE_ROOT}/jsfx/reajs-vst2/reajs-vst2.vcxproj")
# Reuse the official standalone build's explicit source list; the VST2 frame is deliberately excluded.
file(STRINGS "${JSFX_RUNTIME_PROJECT}" _source_lines REGEX "<ClCompile Include=")
set(_runtime_sources)
foreach(_line IN LISTS _source_lines)
  string(REGEX MATCH "Include=\"([^\"]+)\"" _match "${_line}")
  set(_relative "${CMAKE_MATCH_1}")
  if(NOT _relative STREQUAL "vstframe.cpp")
    string(REPLACE "\\" "/" _relative "${_relative}")
    list(APPEND _runtime_sources "${JSFX_SOURCE_ROOT}/jsfx/reajs-vst2/${_relative}")
  endif()
endforeach()
add_library(jsfx_runtime STATIC ${_runtime_sources}
 "${CMAKE_CURRENT_LIST_DIR}/../src/runtime.cpp"
 "${CMAKE_CURRENT_LIST_DIR}/../src/runtime_support.cpp"
 "${CMAKE_CURRENT_LIST_DIR}/../src/flac_source.cpp"
 "${JSFX_SOURCE_ROOT}/WDL/eel2/asm-nseel-x64.obj")
target_compile_features(jsfx_runtime PUBLIC cxx_std_17)
target_compile_definitions(jsfx_runtime PRIVATE WIN32 _WINDOWS _USRDLL BUILD_WINDOWS _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS WDL_FFT_REALSIZE=8 _SUPPORT_GFX_VIS_ JS_NO_TEXTUI)
target_compile_options(jsfx_runtime PRIVATE /wd4244 /wd4267 /wd4018 /wd4800 /wd4996 /utf-8)
target_include_directories(jsfx_runtime PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../src")
target_link_libraries(jsfx_runtime PUBLIC comctl32 user32 gdi32 comdlg32 shell32 winmm ole32 msimg32 advapi32)
# Resource objects in static archives are not extracted by LINK. Propagate the resource as an interface source.
# Replace MFC's unused afxres include with the standard Windows resource header; no MFC dependency.
file(READ "${JSFX_SOURCE_ROOT}/jsfx/reajs-vst2/res.rc" _resources)
string(REPLACE "\"ReaJS\"" "\"ReaJSFX\"" _resources "${_resources}")
string(REPLACE "\"afxres.h\"" "<winres.h>" _resources "${_resources}")
string(REPLACE "\"resource.h\"" "\"${JSFX_SOURCE_ROOT}/jsfx/reajs-vst2/resource.h\"" _resources "${_resources}")
string(REPLACE "..\\\\standalone-helpers\\\\cockos_hslider.bmp" "${JSFX_SOURCE_ROOT}/jsfx/standalone-helpers/cockos_hslider.bmp" _resources "${_resources}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/jsfx_runtime.rc" "${_resources}")
target_sources(jsfx_runtime INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/jsfx_runtime.rc")
# Upstream jsfx embeds an older SDK header than sfxui.cpp requires. Values from official reaper-sdk/sdk/reaper_plugin_fx_embed.h.
target_compile_definitions(jsfx_runtime PRIVATE REAPER_FXEMBED_WM_NOBORDER=0x100001 REAPER_FXEMBED_WM_HITTEST=0x100002 JSFX_SUPPORT_WAV_DECODING)
# The disabled upstream fallback had a stale local variable and signed 8-bit PCM conversion.
# Apply narrowly scoped fixes to a build-directory copy, retaining vendored source verbatim.
file(READ "${JSFX_SOURCE_ROOT}/jsfx/miscfunc.cpp" _miscfunc)
# Upstream _midi_syx returns EEL_F, not EEL_F*. Retptr makes evaluated
# midisyx() results dereference a bogus pointer on x64. Keep the baseline
# source untouched; fix the registration in this production build copy.
string(REPLACE "NSEEL_addfunc_retptr(\"midisyx\",3,NSEEL_PProc_THIS,&_midi_syx)"
 "NSEEL_addfunc_retval(\"midisyx\",3,NSEEL_PProc_THIS,&_midi_syx)" _miscfunc "${_miscfunc}")
string(REPLACE "int fileHandleRec::Open(const char *fn)" "extern PCM_source* modern_open_flac(const char*);\nint fileHandleRec::Open(const char *fn)" _miscfunc "${_miscfunc}")
# Prefer the DAW's decoder; our streaming fallback also works in non-REAPER hosts.
string(REPLACE "    if (m_pcm_src && m_pcm_src->GetNumChannels()>0" "    if (!m_pcm_src && !stricmp(ext,\".flac\")) m_pcm_src=modern_open_flac(fn);\n    if (m_pcm_src && m_pcm_src->GetNumChannels()>0" _miscfunc "${_miscfunc}")
# A FLAC decoding failure must not fall through to interpreting compressed bytes
# as raw floats, which previously obscured missing-decoder failures.
string(REPLACE "  m_fp=fopenUTF8(fn,\"rb\");" "  if (!stricmp(ext,\".flac\")) return 0;\n  m_fp=fopenUTF8(fn,\"rb\");" _miscfunc "${_miscfunc}")
string(REPLACE "else if (l > 4 && !stricmp(fn+l-4,\".wav\"))" "else if (!stricmp(ext,\".wav\"))" _miscfunc "${_miscfunc}")
string(REPLACE "(((signed char)(s))/128.0)" "(((int)(unsigned char)(s)-128)/128.0)" _miscfunc "${_miscfunc}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/jsfx_miscfunc.cpp" "${_miscfunc}")
# Keep the actual REAPER FXDSP separate from our parameter/MIDI callback context.
string(REPLACE "pthis->m_slider_automate(pthis->m_hostctx," "pthis->m_slider_automate(pthis->midi_ctxdata," _miscfunc "${_miscfunc}")
get_target_property(_all_sources jsfx_runtime SOURCES)
list(FILTER _all_sources EXCLUDE REGEX "/miscfunc\\.cpp$")
set_property(TARGET jsfx_runtime PROPERTY SOURCES "${_all_sources};${CMAKE_CURRENT_BINARY_DIR}/jsfx_miscfunc.cpp")
target_include_directories(jsfx_runtime PRIVATE "${JSFX_SOURCE_ROOT}/jsfx")
# Extend the standalone RIFF fallback with PCM32 and IEEE float32/float64.
# m_riff_adpcm remains 1 for ADPCM; 2 identifies IEEE floating point.
string(REPLACE "if (m_riff_adpcm)" "if (m_riff_adpcm == 1)" _miscfunc "${_miscfunc}")
string(REPLACE "else if (ebuf[0] != 1 || ebuf[1] != 0)" "else if (ebuf[0] == 3 && ebuf[1] == 0) { m_riff_adpcm=2; }\n          else if (ebuf[0] != 1 || ebuf[1] != 0)" _miscfunc "${_miscfunc}")
string(REPLACE "(fmt_nch != 1 && fmt_nch != 2)" "(fmt_nch < 1 || fmt_nch > 64 || (m_riff_adpcm == 1 && fmt_nch > 2))" _miscfunc "${_miscfunc}")
string(REPLACE "if (fmt_bps != 8 && fmt_bps != 16 && fmt_bps != 24) goto bailout;" "if (m_riff_adpcm == 2 ? (fmt_bps != 32 && fmt_bps != 64) : (fmt_bps != 8 && fmt_bps != 16 && fmt_bps != 24 && fmt_bps != 32)) goto bailout;" _miscfunc "${_miscfunc}")
string(REPLACE "bailout:\n    fseek(m_fp,0,SEEK_SET);" "bailout:\n    fclose(m_fp); m_fp=NULL; m_itemsleft=0; return 0;" _miscfunc "${_miscfunc}")
set(_wide_decoder [=[
#ifdef JSFX_SUPPORT_WAV_DECODING
static bool readWideWav(fileHandleRec* f, EEL_F* value)
{
  if (f->m_itemsleft <= 0) return false;
  if (f->m_riff_bps == 64) {
    double sample;
    if (fread(&sample,1,8,f->m_fp)!=8) return false;
    *value=sample;
  } else if (f->m_riff_adpcm == 2) {
    float sample;
    if (fread(&sample,1,4,f->m_fp)!=4) return false;
    *value=sample;
  } else {
    int sample;
    if (fread(&sample,1,4,f->m_fp)!=4) return false;
    *value=(double)sample/2147483648.0;
  }
  --f->m_itemsleft;
  return true;
}
#endif
]=])
string(REPLACE "static int serializeInternal(" "${_wide_decoder}\nstatic int serializeInternal(" _miscfunc "${_miscfunc}")
string(REPLACE "  else if (tfh->m_riff_bps==8)" [=[  else if (tfh->m_riff_bps==32 || tfh->m_riff_bps==64)
  {
    EEL_F v=0;
    if (readWideWav(tfh,&v)) {if(var)*var=v; else strOut->SetFormatted(64,"%.10f",v);}
  }
  else if (tfh->m_riff_bps==8)]=] _miscfunc "${_miscfunc}")
# file_mem needs its own branch (the shared substring replacement above also reaches it).
string(REPLACE [=[          else if (tfh->m_riff_bps==32 || tfh->m_riff_bps==64)
  {
    EEL_F v=0;
    if (readWideWav(tfh,&v)) {if(var)*var=v; else strOut->SetFormatted(64,"%.10f",v);}
  }
  else if (tfh->m_riff_bps==8)]=] [=[          else if (tfh->m_riff_bps==32 || tfh->m_riff_bps==64)
          {
            if(readWideWav(tfh,ptr)){++ptr;++outcnt;}else break;
          }
          else if (tfh->m_riff_bps==8)]=] _miscfunc "${_miscfunc}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/jsfx_miscfunc.cpp" "${_miscfunc}")
# Defensive null check: the independent host does not expose REAPER preset callbacks.
file(READ "${JSFX_SOURCE_ROOT}/jsfx/sfxui.cpp" _sfxui)
string(REPLACE "ret > 1000 && fxDoReaperPresetAction(" "ret > 1000 && fxDoReaperPresetAction && fxDoReaperPresetAction(" _sfxui "${_sfxui}")
# Atomic edit/enum commits must close automation gestures; continuous slider drags retain their own end notification.
string(REPLACE "m_slider_automate(m_hostctx, indexToParm(x), false);" "m_slider_automate(m_hostctx, indexToParm(x), true);" _sfxui "${_sfxui}")
string(REPLACE "m_slider_automate(m_hostctx," "m_slider_automate(midi_ctxdata," _sfxui "${_sfxui}")

# Standalone hosts know their active bus width without a REAPER track callback.
# Preserve the REAPER callback path when one is supplied; setters still require host routing.
string(REPLACE [=[  if (!p || !p->m_hostctx || !fxGetSetHostNumChan)
  {
    return (EEL_F)0;
  }
  return (EEL_F)fxGetSetHostNumChan(p->m_hostctx, NULL);]=] [=[  if (!p || !p->m_hostctx || !fxGetSetHostNumChan)
  {
    return p ? (EEL_F)(p->m_hostnch > 0 ? p->m_hostnch : 0) : 0;
  }
  return (EEL_F)fxGetSetHostNumChan(p->m_hostctx, NULL);]=] _miscfunc "${_miscfunc}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/jsfx_miscfunc.cpp" "${_miscfunc}")

include("${CMAKE_CURRENT_LIST_DIR}/reaper-services.cmake")

# The standalone upstream state writer uses %lf (six fractional digits), losing
# small continuous parameters in modern synths. Keep the existing text state
# grammar, but serialize every double with round-trip precision.
file(READ "${JSFX_SOURCE_ROOT}/jsfx/effectproc.cpp" _effectproc)
string(REPLACE "snprintf(buf, sizeof(buf), \"%lf\", v);\n          WDL_remove_trailing_decimal_zeros(buf, 0);" "snprintf(buf, sizeof(buf), \"%.17g\", v);" _effectproc "${_effectproc}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/jsfx_effectproc.cpp" "${_effectproc}")
get_target_property(_all_sources jsfx_runtime SOURCES)
list(FILTER _all_sources EXCLUDE REGEX "/effectproc\\.cpp$")
set_property(TARGET jsfx_runtime PROPERTY SOURCES "${_all_sources};${CMAKE_CURRENT_BINARY_DIR}/jsfx_effectproc.cpp")

# A standalone format must not display a dead REAPER preset button. Retain the
# modern config: menu (e.g. DrNine output mode) without any REAPER dependency.
string(REPLACE "  resizer.init_item(IDC_BUTTON1, 1, 0, 1, 0);" "  resizer.init_item(IDC_BUTTON1, 1, 0, 1, 0);\n  ShowWindow(GetDlgItem(hwndDlg,IDC_BUTTON1), m_config_items.GetSize() ? SW_SHOWNA : SW_HIDE);" _sfxui "${_sfxui}")
string(REPLACE "command == IDC_BUTTON1 && m_hostctx" "command == IDC_BUTTON1 && m_config_items.GetSize()" _sfxui "${_sfxui}")
string(REPLACE "HMENU menu=LoadMenu(g_hInst,MAKEINTRESOURCE(IDR_MENU2));" "HMENU menu=NULL; // plugin preset management belongs to the DAW" _sfxui "${_sfxui}")
set(_reaper_preset_menu [=[    if (fxGetReaperPresetNamesRaw)
    {
      HWND combo = GetDlgItem(hwndDlg,IDC_COMBO1);
      int a=combo ? SendMessage(combo, CB_GETCURSEL, 0, 0) : 0;
      int type=combo ? SendMessage(combo, CB_GETITEMDATA, a, 0) : 0;

      int sz=fxGetReaperPresetNamesRaw(m_hostctx, 0);

      EnableMenuItem(menu, IDC_RENAME_PRESET, MF_BYCOMMAND|(type == 1 || type == 2 ? MF_ENABLED : MF_GRAYED));
      EnableMenuItem(menu, IDC_DELETE_PRESET, MF_BYCOMMAND|(type == 1 ? MF_ENABLED : MF_GRAYED));
      EnableMenuItem(menu, IDC_MOVE_UP_PRESET, MF_BYCOMMAND|(type == 1 && a-2 > 0 ? MF_ENABLED : MF_GRAYED));
      EnableMenuItem(menu, IDC_MOVE_DOWN_PRESET, MF_BYCOMMAND|(type == 1 && a-2 < sz-1 ? MF_ENABLED : MF_GRAYED));

      if (fxDoReaperPresetAction)
      {
        fxDoReaperPresetAction(m_hostctx,(const char *)sm,20);
      }
    }
]=])
string(FIND "${_sfxui}" "${_reaper_preset_menu}" _preset_pos)
if(_preset_pos LESS 0)
  message(FATAL_ERROR "Upstream preset menu changed; inspect standalone configuration patch")
endif()
string(REPLACE "${_reaper_preset_menu}" "" _sfxui "${_sfxui}")
string(REPLACE "if (fxSetUndoPoint) fxSetUndoPoint(m_hostctx);" "if (m_hostctx && fxSetUndoPoint) fxSetUndoPoint(m_hostctx);" _sfxui "${_sfxui}")
string(REPLACE "Preset and Performance Management" "Script configuration" _sfxui "${_sfxui}")
string(REPLACE "        resizer.onResize();" [=[        // Reclaim the hidden Options slot; name ends immediately before Edit.
        WDL_WndSizer__rec *name_rect=resizer.get_item(IDC_EFFECTNAME);
        WDL_WndSizer__rec *next_rect=resizer.get_item(m_config_items.GetSize()?IDC_BUTTON1:IDC_EDITFX);
        if (name_rect && next_rect) name_rect->orig.right=next_rect->orig.left-4;
        resizer.onResize();]=] _sfxui "${_sfxui}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/jsfx_sfxui.cpp" "${_sfxui}")
get_target_property(_all_sources jsfx_runtime SOURCES)
list(FILTER _all_sources EXCLUDE REGEX "/sfxui\\.cpp$")
set_property(TARGET jsfx_runtime PROPERTY SOURCES "${_all_sources};${CMAKE_CURRENT_BINARY_DIR}/jsfx_sfxui.cpp")
