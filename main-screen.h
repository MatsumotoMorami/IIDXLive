#pragma once
#include <d3d9.h>
#include <atomic>

// OBS's D3D9 capture asks for render target 0. TDJ may leave the sub-screen
// bound there at Present. Redirect only OBS's query, not game rendering calls.
namespace MainScreen {
using GetTarget=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,DWORD,IDirect3DSurface9**);
static GetTarget original=nullptr;
static std::atomic<uintptr_t> obsBegin{0},obsEnd{0};
static std::atomic<HWND> mainWindow{nullptr};
static bool IsMainWindow(HWND window){
    wchar_t title[512]{};if(!GetWindowTextW(window,title,512))return false;
    size_t n=wcslen(title);
    return wcsstr(title,L"beatmania IIDX")&&n>=5&&!_wcsicmp(title+n-5,L" main");
}
static bool IsObsCaller(uintptr_t caller){return caller>=obsBegin.load()&&caller<obsEnd.load();}
static BOOL CALLBACK FindMainWindow(HWND window,LPARAM result){
    DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    if(pid==GetCurrentProcessId()&&IsMainWindow(window)){
        *(HWND*)result=window;return FALSE;
    }
    return TRUE;
}
static void RefreshMainWindow(){
    HWND found=nullptr;EnumWindows(FindMainWindow,(LPARAM)&found);
    mainWindow.store(found,std::memory_order_release);
}
static HRESULT STDMETHODCALLTYPE Target(IDirect3DDevice9* device,DWORD index,IDirect3DSurface9** surface){
    uintptr_t caller=(uintptr_t)__builtin_return_address(0);
    if(index==0&&surface&&IsObsCaller(caller)){
        D3DDEVICE_CREATION_PARAMETERS cp{};
        // GetWindowText on a window in this process can synchronously message
        // its owning thread. Never do that from a per-frame D3D callback.
        HWND main=mainWindow.load(std::memory_order_acquire);
        if(main&&SUCCEEDED(device->GetCreationParameters(&cp))&&cp.hFocusWindow==main){
            // Swap chain 0 is the main 1920x1080 screen; chain 1 is TDJ sub.
            // GetBackBuffer adds the reference expected by OBS. Do not rebind
            // render targets and do not fall back to a possibly wrong screen.
            return device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,surface);
        }
    }
    return original(device,index,surface);
}
static bool Install(){
    HMODULE d3d=LoadLibraryExW(L"d3d9.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!d3d)return false;
    auto create=(HRESULT(WINAPI*)(UINT,IDirect3D9Ex**))GetProcAddress(d3d,"Direct3DCreate9Ex");
    if(!create)return false;
    IDirect3D9Ex* api=nullptr;if(FAILED(create(D3D_SDK_VERSION,&api)))return false;
    HWND window=CreateWindowExW(0,L"STATIC",L"IIDXLive D3D probe",WS_POPUP,0,0,16,16,nullptr,nullptr,self,nullptr);
    if(!window){api->Release();return false;}
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=16;pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    IDirect3DDevice9Ex* device=nullptr;
    HRESULT hr=api->CreateDeviceEx(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,
        D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED|D3DCREATE_NOWINDOWCHANGES,&pp,nullptr,&device);
    bool ok=false;
    if(SUCCEEDED(hr)){
        void* target=(*(void***)device)[38];
        ok=MH_CreateHook(target,(void*)&Target,(void**)&original)==MH_OK&&MH_EnableHook(target)==MH_OK;
        device->Release();
    }
    api->Release();DestroyWindow(window);
    // Keep d3d9 loaded: its method is hooked for the lifetime of this process.
    return ok;
}
static void TrackObs(){
    HMODULE last=nullptr;
    for(;;){
        RefreshMainWindow();
        HMODULE module=GetModuleHandleW(L"graphics-hook64.dll");
        if(module!=last){
            obsEnd=0;obsBegin=0;
            if(module){
                auto b=(BYTE*)module;auto dos=(IMAGE_DOS_HEADER*)b;
                auto nt=(IMAGE_NT_HEADERS64*)(b+dos->e_lfanew);
                obsBegin=(uintptr_t)b;obsEnd=(uintptr_t)b+nt->OptionalHeader.SizeOfImage;
                Log(L"OBS D3D9 capture restricted to main swap-chain backbuffer 0");
            }
            last=module;
        }
        Sleep(500);
    }
}
}
