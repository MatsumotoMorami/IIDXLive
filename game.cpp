#include "protocol.h"
#include <stdio.h>
#include <string>
#include "MinHook.h"
#include "audio-layout.h"

static HMODULE self;
static std::wstring ini, logPath;
static Live::Shared* shared;
static HANDLE mapHandle, owner;
static BYTE* game;
static AudioLayout::Layout layout;
static uint32_t sampleRate=44100;
static volatile LONG producerBusy;
using Feed=int(*)(void*,void*,int);
static Feed original;
static void Log(const wchar_t* text){
    FILE* f=_wfopen(logPath.c_str(),L"a, ccs=UTF-8");if(!f)return;
    SYSTEMTIME t;GetLocalTime(&t);
    fwprintf(f,L"%04u-%02u-%02u %02u:%02u:%02u %ls\n",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,text);fclose(f);
}
// Native recorder registers on device graph 0, tee 0 (validated by discovery).
// The same tee receives interleaved F32 stereo before native F32->S16 conversion.
// Read these pointers on the feed thread, where this graph is in use, never
// from a polling thread that could race graph destruction.
static bool IsRecordingTee(void* tee){
    auto device=*(BYTE**)(layout.soundGlobal+8);if(!device)return false;
    auto impl=*(BYTE**)(device+8);if(!impl)return false;
    auto graph=*(BYTE**)(impl+8);if(!graph)return false;
    return *(void**)(graph+0x150)==tee;
}
static int Capture(void* tee,void* data,int bytes){
    int result=original(tee,data,bytes);
    if(!shared||!data||bytes<=0||bytes>1024*1024||(bytes%8)||!IsRecordingTee(tee))return result;
    if(InterlockedCompareExchange(&producerBusy,1,0)){InterlockedIncrement64(&shared->dropped);return result;}
    Live::Publish(shared,(float*)data,bytes/8,sampleRate,Live::Now());
    InterlockedExchange(&producerBusy,0);
    return result;
}
static bool Install(HMODULE module){
    auto b=(BYTE*)module;
    if(!AudioLayout::Discover(b,layout)){Log(L"Automatic discovery failed or audio ABI changed; no hook installed.");return false;}
    wchar_t found[160];swprintf(found,160,L"Auto-discovered audio feed RVA=%llx, sound global RVA=%llx",(unsigned long long)(layout.feed-b),(unsigned long long)(layout.soundGlobal-b));Log(found);
    owner=CreateMutexW(nullptr,FALSE,Live::OwnerName);if(!owner)return false;
    DWORD wait=WaitForSingleObject(owner,0);if(wait!=WAIT_OBJECT_0&&wait!=WAIT_ABANDONED){Log(L"Another game owns the audio bridge.");CloseHandle(owner);owner=nullptr;return false;}
    mapHandle=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Live::Shared),Live::MappingName);
    if(mapHandle)shared=(Live::Shared*)MapViewOfFile(mapHandle,FILE_MAP_ALL_ACCESS,0,0,sizeof(Live::Shared));
    if(!shared){Log(L"Cannot create audio shared memory.");return false;}
    memset(shared,0,sizeof(*shared));shared->version=Live::Version;shared->size=sizeof(*shared);shared->pid=GetCurrentProcessId();shared->epoch=Live::Now();
    MemoryBarrier();shared->magic=Live::Magic;game=b;
    if(MH_Initialize()!=MH_OK||MH_CreateHook(layout.feed,(void*)&Capture,(void**)&original)!=MH_OK||MH_EnableHook(layout.feed)!=MH_OK){Log(L"MinHook installation failed.");shared->magic=0;return false;}
    wchar_t msg[160];swprintf(msg,160,L"Audio bridge installed: native F32 stereo, %u Hz. OBS supplies game video capture.",sampleRate);Log(msg);
    return true;
}
#include "main-screen.h"
static DWORD WINAPI Worker(void*){
    wchar_t path[32768];GetModuleFileNameW(self,path,32768);std::wstring root(path);root.resize(root.find_last_of(L"\\/")+1);
    ini=root+L"IIDXLive.ini";logPath=root+L"IIDXLive.log";
    if(!GetPrivateProfileIntW(L"Live",L"Enabled",1,ini.c_str()))return 0;
    sampleRate=GetPrivateProfileIntW(L"Live",L"SampleRate",44100,ini.c_str());
    if(sampleRate!=44100&&sampleRate!=48000){Log(L"Invalid SampleRate; using 44100.");sampleRate=44100;}
    for(int i=0;i<600;i++){
        if(auto m=GetModuleHandleW(L"bm2dx.dll")){
            if(Install(m)){
                if(GetPrivateProfileIntW(L"Live",L"MainScreenOnly",1,ini.c_str())){
                    if(MainScreen::Install()){
                        Log(L"Main-screen capture guard installed; waiting for OBS graphics hook");
                        MainScreen::TrackObs();
                    }else Log(L"Main-screen capture guard FAILED; OBS may capture wrong render target");
                }
                WaitForSingleObject(GetCurrentProcess(),INFINITE); // keep ownership until process exit
            }
            return 0;
        }Sleep(100);
    }
    Log(L"bm2dx.dll not loaded within 60 seconds.");return 0;
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID){
    if(reason==DLL_PROCESS_ATTACH){self=module;DisableThreadLibraryCalls(module);
        // Hooks remain installed until process exit; pin our code accordingly.
        HMODULE pinned;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,(LPCWSTR)&Capture,&pinned);
        HANDLE t=CreateThread(nullptr,0,Worker,nullptr,0,nullptr);if(t)CloseHandle(t);
    }return TRUE;
}
