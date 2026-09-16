#ifdef NDEBUG
#undef NDEBUG
#endif
#include "protocol.h"
#include "MinHook.h"
#include <stdio.h>
#include <assert.h>
static HMODULE self=GetModuleHandleW(nullptr);
static void Log(const wchar_t*){}
#include "main-screen.h"
int main(int argc,char** argv){
    assert(argc==2);assert(MH_Initialize()==MH_OK);assert(MainScreen::Install());
    HMODULE caller=LoadLibraryA(argv[1]);assert(caller);
    auto dos=(IMAGE_DOS_HEADER*)caller;auto nt=(IMAGE_NT_HEADERS64*)((BYTE*)caller+dos->e_lfanew);
    MainScreen::obsBegin=(uintptr_t)caller;MainScreen::obsEnd=(uintptr_t)caller+nt->OptionalHeader.SizeOfImage;
    auto read=(HRESULT(*)(IDirect3DDevice9*,DWORD,IDirect3DSurface9**))GetProcAddress(caller,"ReadTarget");assert(read);
    auto create=(HRESULT(WINAPI*)(UINT,IDirect3D9Ex**))GetProcAddress(GetModuleHandleW(L"d3d9.dll"),"Direct3DCreate9Ex");
    IDirect3D9Ex* api=nullptr;assert(SUCCEEDED(create(D3D_SDK_VERSION,&api)));
    HWND window=CreateWindowExW(0,L"STATIC",L"beatmania IIDX test main",WS_POPUP,0,0,1920,1080,nullptr,nullptr,self,nullptr);assert(window);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;
    pp.BackBufferWidth=1920;pp.BackBufferHeight=1080;pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    IDirect3DDevice9Ex* device=nullptr;assert(SUCCEEDED(api->CreateDeviceEx(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED|D3DCREATE_NOWINDOWCHANGES,&pp,nullptr,&device)));
    IDirect3DSurface9 *main=nullptr,*sub=nullptr,*got=nullptr;
    assert(SUCCEEDED(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&main)));
    assert(SUCCEEDED(device->CreateRenderTarget(1280,720,D3DFMT_X8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&sub,nullptr)));
    assert(SUCCEEDED(device->SetRenderTarget(0,sub)));
    assert(SUCCEEDED(device->GetRenderTarget(0,&got))&&got==sub);got->Release();
    assert(SUCCEEDED(read(device,0,&got))&&got==main);D3DSURFACE_DESC desc{};got->GetDesc(&desc);assert(desc.Width==1920&&desc.Height==1080);got->Release();
    assert(SUCCEEDED(device->GetRenderTarget(0,&got))&&got==sub);got->Release();
    SetWindowTextW(window,L"beatmania IIDX test sub");
    assert(SUCCEEDED(read(device,0,&got))&&got==sub);got->Release();
    device->SetRenderTarget(0,main);sub->Release();main->Release();device->Release();api->Release();DestroyWindow(window);
    MH_DisableHook(MH_ALL_HOOKS);MH_Uninitialize();FreeLibrary(caller);
    puts("PASS: OBS caller receives main 1920x1080 backbuffer while sub target is bound; game queries and non-main devices unchanged");
}
