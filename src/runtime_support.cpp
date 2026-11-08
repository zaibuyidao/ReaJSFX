// SPDX-License-Identifier: LGPL-2.1-or-later
#include "runtime.h"
#include <map>
#include <new>
#include <mutex>
#include <string>
#include <vector>
#include "../upstream/jsfx/jsfx/sfxui.h"
#include <commctrl.h>
HINSTANCE g_hInst=nullptr;
namespace {
std::mutex apiMutex;
bool apiBound=false;
RuntimeApiGetter resolvingGetter=nullptr;
void* resolvingContext=nullptr;
std::map<std::string,void*> apiFunctions;
int (*realRegister)(const char*,void*)=nullptr;
std::vector<void*> accelerators;
int registerRuntime(const char* name,void* value){
 if(!realRegister)return 0;
 int result=realRegister(name,value);
 if(result&&!strcmp(name,"accelerator"))accelerators.push_back(value);
 return result;
}
void* resolveApi(const char* name){
 void* fn=resolvingGetter?resolvingGetter(resolvingContext,name):nullptr;
 apiFunctions[name]=fn;
 if(!strcmp(name,"plugin_register")){realRegister=reinterpret_cast<decltype(realRegister)>(fn);return fn?reinterpret_cast<void*>(registerRuntime):nullptr;}
 return fn;
}
}
bool runtime_bind_reaper(RuntimeApiGetter getter,void* context){
 std::lock_guard<std::mutex> lock(apiMutex);
 if(apiBound)return true;
 if(!getter||!getter(context,"GetAppVersion"))return false;
 resolvingGetter=getter;resolvingContext=context;
 sx_provideAPIFunctionGetter(resolveApi);
 // Extra public services used by project-scoped export and diagnostic checks.
 for(auto name:{"EnumProjects","SelectProjectInstance","ValidatePtr","Undo_BeginBlock2","Undo_EndBlock2","GetMasterTrack","TrackFX_GetPinMappings","TrackFX_SetPinMappings","TakeFX_GetPinMappings","TakeFX_SetPinMappings","GetMediaItemTake_Item","GetItemProjectContext"})resolveApi(name);
 resolvingGetter=nullptr;resolvingContext=nullptr;
 if(!get_eel_funcdesc)get_eel_funcdesc=default_get_eel_funcdesc;
 apiBound=true;return true;
}
void* runtime_reaper_api(const char* name){auto i=apiFunctions.find(name);return i==apiFunctions.end()?nullptr:i->second;}
// The private fxGetSetPinMap2 setter can change a CLAP instance's live map
// without updating the map REAPER serializes for fixed port configurations.
// Use the public setter when the owning track/take FX resolves unambiguously.
bool runtime_set_pin_mapping(SX_Instance* instance,int isout,int pin,int start,
 unsigned int mask,unsigned int value,unsigned int* result){
 if(!instance||!result||!instance->m_hostctx||GetCurrentThreadId()!=instance->m_main_thread||
    !GetProjectFromFxDsp||!fxGetPlacement||!GetTrack)return false;
 if(pin<0||pin>=128||start<0||start>=128){*result=0;return true;}
 auto get=reinterpret_cast<int (*)(MediaTrack*,int,int,int,int*)>(runtime_reaper_api("TrackFX_GetPinMappings"));
 auto set=reinterpret_cast<bool (*)(MediaTrack*,int,int,int,int,int)>(runtime_reaper_api("TrackFX_SetPinMappings"));
 auto master=reinterpret_cast<MediaTrack* (*)(ReaProject*)>(runtime_reaper_api("GetMasterTrack"));
 auto validate=reinterpret_cast<bool (*)(void*,const char*)>(runtime_reaper_api("ValidatePtr"));
 auto getTake=reinterpret_cast<int (*)(MediaItem_Take*,int,int,int,int*)>(runtime_reaper_api("TakeFX_GetPinMappings"));
 auto setTake=reinterpret_cast<bool (*)(MediaItem_Take*,int,int,int,int,int)>(runtime_reaper_api("TakeFX_SetPinMappings"));
 if(!get||!set)return false;
 ReaProject* project=GetProjectFromFxDsp(instance->m_hostctx);
 if(!project||(validate&&!validate(project,"ReaProject*")))return false;
 int fx=-1,flags=0;
 int trackIndex=fxGetPlacement(instance->m_hostctx,&fx,&flags);
 if(fx<0||trackIndex<-2)return false;
 MediaTrack* track=nullptr;MediaItem_Take* take=nullptr;
 if(flags&1){
   // Query the instance's live host context: a cached take could be stale after
   // moving an FX. Do not infer ownership from track index or plugin name.
   if(runtime_parent_context(instance,4)!=instance->m_hostctx)return false;
   take=static_cast<MediaItem_Take*>(runtime_parent_context(instance,2));
   auto itemFromTake=reinterpret_cast<MediaItem* (*)(MediaItem_Take*)>(runtime_reaper_api("GetMediaItemTake_Item"));
   auto projectFromItem=reinterpret_cast<ReaProject* (*)(MediaItem*)>(runtime_reaper_api("GetItemProjectContext"));
   if(!take||!getTake||!setTake||!validate||!validate(take,"MediaItem_Take*")||!itemFromTake||!projectFromItem)return false;
   auto item=itemFromTake(take);
   if(!item||!validate(item,"MediaItem*")||projectFromItem(item)!=project)return false;
 }else{
   track=trackIndex>=0?GetTrack(project,trackIndex):(master?master(project):nullptr);
   if(!track||(validate&&!validate(track,"MediaTrack*")))return false;
   if((flags&2)||trackIndex==-2)fx|=0x1000000;
 }
 // A container placement already supplies its encoded FX address.
 unsigned int words[4]={};
 auto read=[&](int segment){int high=0;const int p=pin|(segment?0x1000000:0);words[segment*2]=(unsigned int)(take?getTake(take,fx,isout!=0,p,&high):get(track,fx,isout!=0,p,&high));words[segment*2+1]=(unsigned int)high;};
 const int first=start/64,last=(start+31<128?start+31:127)/64;
 for(int segment=first;segment<=last;++segment)read(segment);
 auto window=[&](){unsigned int bits=0;for(int i=0;i<32&&start+i<128;++i)if(words[(start+i)/32]&(1u<<((start+i)%32)))bits|=1u<<i;return bits;};
 // Match upstream semantics: value may set bits outside the clearing mask.
 unsigned int changed=(window()&~mask)|value;
 for(int i=0;i<32&&start+i<128;++i){const unsigned int bit=1u<<((start+i)%32);auto& word=words[(start+i)/32];word=(word&~bit)|((changed&(1u<<i))?bit:0);}
 for(int segment=first;segment<=last;++segment){
   const int p=pin|(segment?0x1000000:0),lo=(int)words[segment*2],hi=(int)words[segment*2+1];
   if(take)setTake(take,fx,isout!=0,p,lo,hi);else set(track,fx,isout!=0,p,lo,hi);
 }
 // Return the effective host mapping, including any channel-limit rejection.
 for(int segment=first;segment<=last;++segment)read(segment);
 *result=window();return true;
}
namespace {
struct ExportScope {
 ReaProject* previous=nullptr;
 ReaProject* target=nullptr;
 void (*select)(ReaProject*)=nullptr;
 void (*endUndo)(ReaProject*,const char*,int)=nullptr;
};
}
bool runtime_export_begin(SX_Instance* instance,void** token){
 if(!token)return false;*token=nullptr;
 if(!instance||!instance->m_hostctx||GetCurrentThreadId()!=instance->m_main_thread||!GetProjectFromFxDsp)return false;
 auto enumerate=reinterpret_cast<ReaProject* (*)(int,char*,int)>(runtime_reaper_api("EnumProjects"));
 auto select=reinterpret_cast<void (*)(ReaProject*)>(runtime_reaper_api("SelectProjectInstance"));
 auto validate=reinterpret_cast<bool (*)(void*,const char*)>(runtime_reaper_api("ValidatePtr"));
 auto target=GetProjectFromFxDsp(instance->m_hostctx);
 if(!enumerate||!target||(validate&&!validate(target,"ReaProject*")))return false;
 auto previous=enumerate(-1,nullptr,0);
 if(!previous)return false;
 auto scope=new(std::nothrow) ExportScope{previous,target,select,nullptr};
 if(!scope)return false;
 if(previous!=target){if(!select){delete scope;return false;}select(target);if(enumerate(-1,nullptr,0)!=target){select(previous);delete scope;return false;}}
 auto begin=reinterpret_cast<void (*)(ReaProject*)>(runtime_reaper_api("Undo_BeginBlock2"));
 auto end=reinterpret_cast<void (*)(ReaProject*,const char*,int)>(runtime_reaper_api("Undo_EndBlock2"));
 if(begin&&end){begin(target);scope->endUndo=end;}
 *token=scope;return true;
}
void runtime_export_end(void* token){
 auto scope=static_cast<ExportScope*>(token);if(!scope)return;
 if(scope->endUndo)scope->endUndo(scope->target,"JSFX: export buffer to project",-1);
 if(scope->previous!=scope->target&&scope->select)scope->select(scope->previous);
 delete scope;
}
void Sliders_Init(HINSTANCE,bool,int);
void Meters_Init(HINSTANCE,bool);
void runtime_init(HINSTANCE module){
 g_hInst=module;
 INITCOMMONCONTROLSEX cc={sizeof(cc),ICC_WIN95_CLASSES};InitCommonControlsEx(&cc);
 Sliders_Init(module,true,1015);Meters_Init(module,true);
 get_eel_funcdesc=default_get_eel_funcdesc;
}
void runtime_shutdown(){if(realRegister)for(auto p:accelerators)realRegister("-accelerator",p);accelerators.clear();if(apiBound)sx_provideAPIFunctionGetter([](const char*)->void*{return nullptr;});apiBound=false;apiFunctions.clear();realRegister=nullptr;if(g_hInst){Sliders_Init(g_hInst,false,0);Meters_Init(g_hInst,false);g_hInst=nullptr;}}
// Required by the native JSFX resource/UI implementation, otherwise provided by its VST2 frame.
const char* escapeAmpersandsForMenu(const char* in,char* buf,int size){if(!in||!buf||size<1)return in;int at=0;for(const char* p=in;*p && at<size-1;++p){if(*p=='&'){if(at>=size-2)break;buf[at++]='&';}buf[at++]=*p;}buf[at]=0;return buf;}
void InsertMenuItemFilter(HMENU menu,int pos,BOOL byPos,MENUITEMINFO* info){if(info&&(info->fMask&MIIM_TYPE)&&info->fType==MFT_STRING&&info->dwTypeData){auto copy=*info;char buf[2048];copy.dwTypeData=(char*)escapeAmpersandsForMenu(info->dwTypeData,buf,sizeof(buf));InsertMenuItem(menu,pos,byPos,&copy);}else InsertMenuItem(menu,pos,byPos,info);}
