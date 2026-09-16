#ifdef NDEBUG
#undef NDEBUG
#endif
#include "audio-layout.h"
#include <assert.h>
#include <stdio.h>
int main(int argc,char** argv){
    assert(argc==2);HMODULE m=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);assert(m);
    AudioLayout::Layout l;bool ok=AudioLayout::Discover((BYTE*)m,l);
    printf("discovery=%d feed=%llx global=%llx\n",ok,(unsigned long long)(l.feed-(BYTE*)m),(unsigned long long)(l.soundGlobal-(BYTE*)m));
    assert(ok);DWORD old;VirtualProtect(l.feed,1,PAGE_EXECUTE_READWRITE,&old);BYTE save=*l.feed;*l.feed=0xcc;
    AudioLayout::Layout bad;assert(!AudioLayout::Discover((BYTE*)m,bad));*l.feed=save;
    puts("PASS: real-image discovery and changed-ABI rejection");return 0;
}
