"""Prepare VST2 sources with ABI, resource-path and branding adaptations."""
from pathlib import Path
import argparse, shutil

ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
TREE=a.output.resolve()
TREE.mkdir(parents=True,exist_ok=True)
shutil.copytree(ROOT/'upstream/jsfx',TREE,dirs_exist_ok=True,ignore=shutil.ignore_patterns('.git'))
original = (ROOT / 'dependencies/vst2-vestige/vestige.h').read_text()
header = original.replace('char dump[sizeof (VstMidiEvent)];',
    'int32_t type, byteSize, deltaFrames, flags; char data[16];')
header = header.replace('float unkown_float;', 'float ioRatio;')
header = header.replace('typedef struct _AEffect AEffect;', 'typedef struct _AEffect AEffect;')
# Reserve the complete VST 2.4 tail. No double-processing capability is enabled.
needle = 'void (__cdecl *processReplacing) (struct _AEffect *, float **, float **, int);'
assert needle in header
header = header.replace(needle, needle + '\n void (__cdecl *processDoubleReplacing)(struct _AEffect*,double**,double**,int);\n char future[56];')
header += r'''
// ReaJS build compatibility declarations. These are ABI facts, not DSP code.
#include <stddef.h>
typedef int32_t VstInt32;
typedef intptr_t VstIntPtr;
#define VSTCALLBACK __cdecl
#define effFlagsProgramChunks (1 << 5)
#define effGetParamLabel 6
#define effGetParamDisplay 7
#define effIdentify 22
#define effGetChunk 23
#define effSetChunk 24
#define effCanBeAutomated 26
#define effGetProgramNameIndexed 29
#define effGetInputProperties 33
#define effGetOutputProperties 34
#define kVstSysExType 6
struct VstMidiSysexEvent {
 int32_t type, byteSize, deltaFrames, flags, dumpBytes;
 intptr_t resvd1;
 char* sysexDump;
 intptr_t resvd2;
};
struct VstPinProperties {
 char label[64]; int32_t flags, arrangementType; char shortLabel[8], future[48];
};
static_assert(sizeof(void*)==8,"x64 baseline only");
static_assert(sizeof(AEffect)==192 && offsetof(AEffect,object)==96 && offsetof(AEffect,processReplacing)==120,"AEffect ABI");
static_assert(sizeof(VstMidiEvent)==32 && sizeof(VstEvent)==32,"MIDI ABI");
static_assert(sizeof(VstEvents)==32 && offsetof(VstEvents,events)==16,"events ABI");
static_assert(sizeof(VstMidiSysexEvent)==48 && offsetof(VstMidiSysexEvent,sysexDump)==32,"SysEx ABI");
static_assert(sizeof(VstTimeInfo)==88 && sizeof(VstPinProperties)==128,"metadata ABI");
'''
(TREE/'jmde/aeffectx.h').write_text(header,encoding='utf-8')
frame=TREE/'jsfx/reajs-vst2/vstframe.cpp'
s=frame.read_text(encoding='utf-8')
for old,new in [('#define VENDOR "Cockos"','#define VENDOR "Cockos / zaibuyidao"'),
                ('#define EFFECT_NAME "ReaJS"','#define EFFECT_NAME "ReaJSFX"'),
                ('m_effect.version=1100;','m_effect.version=REAJSFX_VST2_VERSION;'),
                ('ReaJS by Cockos Incorporated - www.reaper.fm','ReaJSFX - Cockos / zaibuyidao')]:
    if old not in s:raise RuntimeError('Upstream branding anchor changed: '+old)
    s=s.replace(old,new)
# Persist the Windows fallback path constructed by upstream.
fallback = '        lstrcatn(buf,WDL_DIRCHAR_STR "JS", sizeof(buf));'
if s.count(fallback) != 1:
    raise RuntimeError('Upstream Windows JS resource path anchor changed')
s = s.replace(fallback, fallback + '\n        g_root_path.Set(buf);')
frame.write_text(s,encoding='utf-8')
rc=TREE/'jsfx/reajs-vst2/res.rc'
s=rc.read_text(encoding='utf-8').replace('"afxres.h"','<winres.h>').replace('"ReaJS"','"ReaJSFX"')
bitmap='..\\\\standalone-helpers\\\\cockos_hslider.bmp'
if bitmap not in s:raise RuntimeError('Upstream bitmap resource path changed')
s=s.replace(bitmap,(TREE/'jsfx/standalone-helpers/cockos_hslider.bmp').as_posix())
rc.write_text(s,encoding='utf-8')
