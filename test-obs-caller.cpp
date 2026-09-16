#include <d3d9.h>
extern "C" __declspec(dllexport) HRESULT ReadTarget(IDirect3DDevice9* device,DWORD index,IDirect3DSurface9** out){
    // Prevent a tail call: the return address must remain in this test DLL.
    volatile HRESULT result=device->GetRenderTarget(index,out);return result;
}
