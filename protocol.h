#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace Live {
constexpr wchar_t MappingName[]=L"Local\\IIDXLiveAudio_v1";
constexpr wchar_t OwnerName[]=L"Local\\IIDXLiveAudioOwner_v1";
constexpr uint32_t Magic=0x314c5849, Version=1, Slots=128, MaxFrames=2048;
struct alignas(64) Packet {
    volatile LONG64 stamp; // odd while writing; even = sequence * 2
    uint64_t timestamp;
    uint32_t frames, rate;
    float samples[MaxFrames*2];
};
struct alignas(64) Shared {
    uint32_t magic, version, size, pid;
    uint64_t epoch;
    volatile LONG64 published, dropped;
    Packet packets[Slots];
};
inline uint64_t Now() {
    LARGE_INTEGER t,f; QueryPerformanceCounter(&t); QueryPerformanceFrequency(&f);
    return (uint64_t)(t.QuadPart/f.QuadPart)*1000000000ULL+
           (uint64_t)(t.QuadPart%f.QuadPart)*1000000000ULL/f.QuadPart;
}
inline LONG64 Read(volatile LONG64* p){return InterlockedCompareExchange64(p,0,0);}
// Single producer, any number of independent readers. Producer never waits.
inline void Publish(Shared* s,const float* data,uint32_t frames,uint32_t rate,uint64_t timestamp){
    while(frames){
        uint32_t n=frames>MaxFrames?MaxFrames:frames;
        LONG64 seq=Read(&s->published)+1;auto& p=s->packets[seq%Slots];
        InterlockedExchange64(&p.stamp,seq*2+1);
        p.timestamp=timestamp;p.frames=n;p.rate=rate;
        memcpy(p.samples,data,n*2*sizeof(float));
        InterlockedExchange64(&p.stamp,seq*2);
        InterlockedExchange64(&s->published,seq);
        data+=n*2;frames-=n;timestamp+=(uint64_t)n*1000000000ULL/rate;
    }
}
inline bool Copy(Shared* s,LONG64 sequence,Packet& out){
    auto& p=s->packets[sequence%Slots];
    if(Read(&p.stamp)!=sequence*2)return false;
    memcpy(&out.timestamp,&p.timestamp,sizeof(Packet)-offsetof(Packet,timestamp));
    MemoryBarrier();
    return Read(&p.stamp)==sequence*2 && out.frames>0 && out.frames<=MaxFrames &&
           out.rate>=8000 && out.rate<=192000;
}
}
