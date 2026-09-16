#ifdef NDEBUG
#undef NDEBUG
#endif
#include "audio-layout.h"
#include <obs.h>
#include <stdio.h>
#include <assert.h>
#include <atomic>
#include <math.h>
static std::atomic<unsigned> received{0};
static void Audio(void*,obs_source_t*,const audio_data* audio,bool){if(audio->frames)received+=audio->frames;}
static int Feed(void*,void* p,int bytes){float* f=(float*)p;for(int i=0;i<bytes/4;i++)f[i]=0.2f*sinf(float(i)*0.03f);return bytes;}
int main(int argc,char** argv){
    assert(argc==4);SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);setvbuf(stdout,nullptr,_IONBF,0);
    AddDllDirectory(L"C:/Program Files/obs-studio/bin/64bit");
    HMODULE module=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);assert(module);
    AudioLayout::Layout layout;assert(AudioLayout::Discover((BYTE*)module,layout));
    BYTE device[32]{},impl[32]{},graph[0x180]{};void* feedVt[2]={nullptr,(void*)&Feed};void* upstream[1]={feedVt};
    struct Tee {void* vtable;LONG lock;LONG pad;void* input;void* spare;void* callback;void* context;} tee{};
    tee.input=upstream;
    *(void**)(device+8)=impl;*(void**)(impl+8)=graph;*(void**)(graph+0x150)=&tee;
    *(void**)(layout.soundGlobal+8)=device;
    assert(LoadLibraryA(argv[2]));
    HANDLE mapping=nullptr;for(int i=0;i<100&&!mapping;i++){Sleep(20);mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,Live::MappingName);}assert(mapping);
    auto shared=(Live::Shared*)MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Live::Shared));assert(shared&&shared->pid==GetCurrentProcessId());
    for(int i=0;i<100&&layout.feed[0]==0x48;i++)Sleep(10);assert(layout.feed[0]!=0x48);
    assert(obs_startup("en-US",nullptr,nullptr));obs_audio_info ai{48000,SPEAKERS_STEREO};assert(obs_reset_audio(&ai));
    obs_video_info vi{};vi.graphics_module="C:/Program Files/obs-studio/bin/64bit/libobs-d3d11.dll";vi.fps_num=60;vi.fps_den=1;
    vi.base_width=vi.output_width=1280;vi.base_height=vi.output_height=720;vi.output_format=VIDEO_FORMAT_NV12;
    vi.colorspace=VIDEO_CS_709;vi.range=VIDEO_RANGE_PARTIAL;vi.scale_type=OBS_SCALE_BICUBIC;vi.gpu_conversion=true;
    assert(obs_reset_video(&vi)==OBS_VIDEO_SUCCESS);
    obs_module_t* capture=nullptr;assert(obs_open_module(&capture,"C:/Program Files/obs-studio/obs-plugins/64bit/win-capture.dll","C:/Program Files/obs-studio/data/obs-plugins/win-capture")==MODULE_SUCCESS);assert(obs_init_module(capture));
    obs_module_t* plugin=nullptr;assert(obs_open_module(&plugin,argv[3],".")==MODULE_SUCCESS);assert(obs_init_module(plugin));
    obs_data_t* settings=obs_data_create();obs_data_set_bool(settings,"iidx_auto_window",false);
    obs_source_t* source=obs_source_create("iidx_live","Integration test",settings,nullptr);assert(source);
    assert(obs_source_get_width(source)==1920&&obs_source_get_height(source)==1080);
    obs_enter_graphics();
    auto texture=gs_texrender_create(GS_RGBA,GS_ZS_NONE);assert(gs_texrender_begin(texture,1920,1080));
    vec4 transparent{};gs_clear(GS_CLEAR_COLOR,&transparent,0,0);
    gs_ortho(0,1920,0,1080,-100,100);obs_source_video_render(source);gs_texrender_end(texture);
    auto stage=gs_stagesurface_create(1920,1080,GS_RGBA);gs_stage_texture(stage,gs_texrender_get_texture(texture));
    uint8_t* pixels=nullptr;uint32_t stride=0;assert(gs_stagesurface_map(stage,&pixels,&stride));
    for(unsigned y=0;y<1080;y++)for(unsigned x=0;x<1920;x++)assert(pixels[y*stride+x*4+3]==0);
    gs_stagesurface_unmap(stage);gs_stagesurface_destroy(stage);gs_texrender_destroy(texture);obs_leave_graphics();
    puts("PASS: idle source reports 1920x1080 and every rendered pixel remains transparent");
    obs_data_release(settings);obs_source_add_audio_capture_callback(source,Audio,nullptr);
    float data[882]{};auto call=(int(*)(void*,void*,int))layout.feed;
    for(int i=0;i<200;i++){assert(call(&tee,data,sizeof data)==sizeof data);Sleep(10);}
    printf("Shared packets=%lld, OBS received frames=%u\n",Live::Read(&shared->published),received.load());
    assert(Live::Read(&shared->published)==200);assert(received>48000);
    Live::Packet packet{};assert(Live::Copy(shared,200,packet));assert(packet.frames==441&&packet.rate==44100);assert(fabs(packet.samples[5]-data[5])<0.00001f);
    obs_source_remove_audio_capture_callback(source,Audio,nullptr);obs_source_release(source);obs_shutdown();
    puts("PASS: native detour -> shared memory -> OBS source audio, with game-capture child loaded");
    // DLL worker and hooks are deliberately process-lifetime resources.
    return 0;
}
