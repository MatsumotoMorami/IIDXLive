#ifdef NDEBUG
#undef NDEBUG
#endif
#include "protocol.h"
#include <thread>
#include <atomic>
#include <assert.h>
#include <stdio.h>
int main(){
 auto s=(Live::Shared*)VirtualAlloc(nullptr,sizeof(Live::Shared),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(s);
 std::atomic<bool> done{false};std::atomic<unsigned> good{0};
 auto reader=[&]{Live::Packet p{};while(!done){LONG64 n=Live::Read(&s->published);if(n&&Live::Copy(s,n,p)){
   assert(p.frames==Live::MaxFrames&&p.rate==44100);for(unsigned i=0;i<p.frames*2;i++)assert(p.samples[i]==float(p.timestamp));++good;
 }}};
 std::thread a(reader),b(reader);float samples[Live::MaxFrames*2];
 for(unsigned n=1;n<=20000;n++){for(auto& f:samples)f=float(n);Live::Publish(s,samples,Live::MaxFrames,44100,n);}
 done=true;a.join();b.join();assert(good>0);Live::Packet packet{};
 assert(!Live::Copy(s,1,packet));assert(Live::Copy(s,20000,packet));
 printf("PASS: 20000 packets, concurrent readers verified %u consistent snapshots; overwritten packets rejected\n",good.load());
 VirtualFree(s,0,MEM_RELEASE);
}
