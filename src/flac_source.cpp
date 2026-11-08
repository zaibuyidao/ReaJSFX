// SPDX-License-Identifier: LGPL-2.1-or-later
// Private file_open fallback, never exposed as a REAPER project media source.
#include <windows.h>
#include <cmath>
#include <string>
#include <array>
#include "../upstream/jsfx/jmde/reaper_plugin.h"
#define DR_FLAC_IMPLEMENTATION
#include "../upstream/dr-libs/dr_flac.h"
namespace {
class FlacSource final:public PCM_source {
 drflac* decoder=nullptr;
 std::string filename;
 drflac_uint64 position=0;
 std::array<drflac_int32,8192> buffer{};
public:
 explicit FlacSource(const char* name):filename(name){int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name,-1,nullptr,0);if(n<=0)return;std::wstring path(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name,-1,path.data(),n);decoder=drflac_open_file_w(path.c_str(),nullptr);if(decoder&&(decoder->channels<1||decoder->channels>8||!decoder->sampleRate||!decoder->totalPCMFrameCount)){drflac_close(decoder);decoder=nullptr;}}
 ~FlacSource()override{if(decoder)drflac_close(decoder);}
 PCM_source* Duplicate()override{return new FlacSource(filename.c_str());}
 bool IsAvailable()override{return decoder!=nullptr;}
 const char* GetType()override{return "FLAC";}
 const char* GetFileName()override{return filename.c_str();}
 bool SetFileName(const char*)override{return false;}
 int GetNumChannels()override{return decoder?decoder->channels:0;}
 double GetSampleRate()override{return decoder?decoder->sampleRate:0;}
 double GetLength()override{return decoder?double(decoder->totalPCMFrameCount)/decoder->sampleRate:0;}
 int GetBitsPerSample()override{return decoder?decoder->bitsPerSample:0;}
 int PropertiesWindow(HWND)override{return 0;}
 void GetSamples(PCM_source_transfer_t*b)override{
  b->samples_out=0;
  // fileHandleRec requests source-rate, source-channel blocks. Refuse unsupported
  // transformations instead of silently returning audio at the wrong rate.
  if(!decoder||!b->samples||b->length<=0||b->nch!=decoder->channels||b->samplerate!=decoder->sampleRate||!std::isfinite(b->time_s)||b->time_s<0)return;
  double at=std::floor(b->time_s*decoder->sampleRate+.5);if(at>=double(decoder->totalPCMFrameCount))return;
  auto frame=(drflac_uint64)at;
  if(frame!=position){if(!drflac_seek_to_pcm_frame(decoder,frame))return;position=frame;}
  while(b->samples_out<b->length){auto wanted=(drflac_uint64)(b->length-b->samples_out);auto capacity=buffer.size()/decoder->channels;if(wanted>capacity)wanted=capacity;auto got=drflac_read_pcm_frames_s32(decoder,wanted,buffer.data());if(!got)break;size_t offset=size_t(b->samples_out)*decoder->channels;for(size_t i=0;i<got*decoder->channels;++i)b->samples[offset+i]=double(buffer[i])/2147483648.0;b->samples_out+=(int)got;position+=got;}
 }
 void GetPeakInfo(PCM_source_peaktransfer_t*)override{}
 void SaveState(ProjectStateContext*)override{}
 int LoadState(const char*,ProjectStateContext*)override{return -1;}
 void Peaks_Clear(bool)override{}
 int PeaksBuild_Begin()override{return 0;}
 int PeaksBuild_Run()override{return 0;}
 void PeaksBuild_Finish()override{}
};
}
PCM_source* modern_open_flac(const char*fn){auto p=new FlacSource(fn);if(p->IsAvailable())return p;delete p;return nullptr;}
