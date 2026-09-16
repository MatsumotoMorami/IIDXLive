#pragma once
#include "protocol.h"

// Read-only discovery. Accept only the inspected MSVC RTTI + audio ABI, not
// merely a similar byte pattern. No disk offsets or version dates are used.
namespace AudioLayout {
struct Layout {BYTE* feed=nullptr;BYTE* soundGlobal=nullptr;};
struct Image {
    BYTE* base;DWORD size;IMAGE_SECTION_HEADER* sections;unsigned count;
    bool contains(const void* p,size_t n=1)const{
        uintptr_t a=(uintptr_t)p,b=(uintptr_t)base;
        return a>=b && n<=size && a-b<=size-n;
    }
    bool executable(const void* p)const{
        if(!contains(p))return false;auto r=(uintptr_t)p-(uintptr_t)base;
        for(unsigned i=0;i<count;i++)if((sections[i].Characteristics&IMAGE_SCN_MEM_EXECUTE)&&r>=sections[i].VirtualAddress&&r-sections[i].VirtualAddress<sections[i].Misc.VirtualSize)return true;
        return false;
    }
    BYTE* unique(const char* bytes,size_t n)const{
        BYTE* found=nullptr;
        for(unsigned i=0;i<count;i++){
            auto& s=sections[i];if(!(s.Characteristics&IMAGE_SCN_MEM_READ)||s.SizeOfRawData<n)continue;
            if(!contains(base+s.VirtualAddress,s.SizeOfRawData))return nullptr;
            for(size_t j=0;j+n<=s.SizeOfRawData;j++){
                BYTE* p=base+s.VirtualAddress+j;
                if(*p==(BYTE)*bytes&&!memcmp(p,bytes,n)){if(found)return nullptr;found=p;}
            }
        }return found;
    }
    void** table(const char* type,unsigned slots)const{
        BYTE* name=unique(type,strlen(type)+1);if(!name||!contains(name-16))return nullptr;
        DWORD td=(DWORD)(name-16-base);BYTE* col=nullptr;
        for(unsigned i=0;i<count;i++){
            auto& s=sections[i];if(!(s.Characteristics&IMAGE_SCN_MEM_READ)||s.Characteristics&IMAGE_SCN_MEM_EXECUTE)continue;
            for(size_t j=0;j+24<=s.SizeOfRawData;j+=4){
                auto p=(DWORD*)(base+s.VirtualAddress+j);
                if(p[0]==1&&p[1]==0&&p[3]==td&&p[5]==s.VirtualAddress+j){if(col)return nullptr;col=(BYTE*)p;}
            }
        }
        if(!col)return nullptr;void** found=nullptr;
        for(unsigned i=0;i<count;i++){
            auto& s=sections[i];if(!(s.Characteristics&IMAGE_SCN_MEM_READ)||s.Characteristics&IMAGE_SCN_MEM_EXECUTE)continue;
            for(size_t j=0;j+(slots+1)*8<=s.SizeOfRawData;j+=8){
                auto p=(void**)(base+s.VirtualAddress+j);if(*p!=col)continue;
                bool good=true;for(unsigned k=1;k<=slots;k++)if(!executable(p[k]))good=false;
                if(good){if(found)return nullptr;found=p+1;}
            }
        }return found;
    }
};
inline bool Discover(BYTE* base,Layout& out){
    auto dos=(IMAGE_DOS_HEADER*)base;if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<0||dos->e_lfanew>0x100000)return false;
    auto nt=(IMAGE_NT_HEADERS64*)(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC||nt->FileHeader.NumberOfSections>96)return false;
    Image im{base,nt->OptionalHeader.SizeOfImage,IMAGE_FIRST_SECTION(nt),nt->FileHeader.NumberOfSections};
    auto tee=im.table(".?AVCAudioTee@BMSoundLib2017@@",3);
    auto device=im.table(".?AVCBeatmaniaIIDXSoundDevice@BeatmaniaIIDXAudio@BMSoundLib2017@@",23);
    if(!tee||!device)return false;
    BYTE* feed=(BYTE*)tee[1];BYTE* reg=(BYTE*)device[22];
    const char feedPrefix[]="\x48\x89\x5c\x24\x08\x48\x89\x6c\x24\x10\x48\x89\x74\x24\x18\x57\x48\x83\xec\x20";
    const char regPrefix[]="\x48\x89\x5c\x24\x10\x48\x89\x6c\x24\x18\x56\x48\x83\xec\x30\x48\x8b\x49\x08\x49\x8b\xe9\x48\x83\xc1\x08\x49\x63\xd8\xe8";
    if(!im.contains(feed,128)||!im.contains(reg,380)||
       memcmp(feed,feedPrefix,sizeof(feedPrefix)-1)||memcmp(reg,regPrefix,sizeof(regPrefix)-1))return false;
    // Verify graph 0 array layout and tee index 0 offset, and callback layout.
    const char graphLayout[]="\x48\x83\xc3\x15\x48\xc1\xe3\x04\x48\x03\xde";
    bool graphOK=false;for(unsigned i=0;i<350;i++)if(!memcmp(reg+i,graphLayout,sizeof(graphLayout)-1))graphOK=true;
    const char callback[]="\x4c\x8b\x4b\x20\x33\xc0\x48\x8b\x4b\x28";
    if(!graphOK||memcmp(feed+0x4c,callback,sizeof(callback)-1))return false;
    // Native recorder's get-sound-device call. Locate unmodified code instead
    // of its vtable: IIDXRecorder may already have replaced the Start slot.
    BYTE* getter=nullptr;
    const char callTail[]="\x48\x8b\x08\x4c\x8b\x49\x08\x48\x8d\x54";
    for(unsigned k=0;k<im.count;k++){
        auto& sec=im.sections[k];if(!(sec.Characteristics&IMAGE_SCN_MEM_EXECUTE))continue;
        for(unsigned i=5;i+48<sec.SizeOfRawData;i++){
            BYTE* p=base+sec.VirtualAddress+i;
            if(p[-5]==0xe8&&!memcmp(p,callTail,sizeof(callTail)-1)&&!memcmp(p+42,"\xff\x90\xb0\x00\x00\x00",6)){
                if(getter)return false;getter=p+*(int32_t*)(p-4);
            }
        }
    }
    if(!getter||!im.executable(getter)||!im.contains(getter,256))return false;
    BYTE* global=nullptr;
    for(unsigned i=0;i<244;i++)if(!memcmp(getter+i,"\x48\x8d\x05",3)&&!memcmp(getter+i+7,"\x48\x83\xc4\x38\xc3",5)){
        if(global)return false;global=getter+i+7+*(int32_t*)(getter+i+3);
    }
    if(!global||!im.contains(global,24))return false;
    out={feed,global};return true;
}
}
