#include "protocol.h"
#include <obs-module.h>
#include <graphics/vec4.h>
#include <atomic>
#include <string>
#include <thread>
#include <algorithm>

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("IIDXRecorder contributors")
MODULE_EXPORT const char* obs_module_description(void){return "IIDX native audio bridge with OBS game capture";}

struct Source {
    obs_source_t *source=nullptr,*video=nullptr;
    std::atomic<bool> stop{false};
    std::atomic<DWORD> pid{0};
    std::thread reader;
    bool automatic=true;
    float elapsed=0;
    std::string window;
};
static void Reader(Source* s){
    HANDLE mapping=nullptr,process=nullptr;Live::Shared* mem=nullptr;
    LONG64 next=0;uint64_t epoch=0;Live::Packet packet{};
    while(!s->stop.load()){
        if(!mem){
            mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,Live::MappingName);
            if(mapping)mem=(Live::Shared*)MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Live::Shared));
            if(mem&&mem->magic==Live::Magic&&mem->version==Live::Version&&mem->size==sizeof(Live::Shared)){
                process=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,mem->pid);
                if(process&&WaitForSingleObject(process,0)==WAIT_TIMEOUT){
                    epoch=mem->epoch;next=Live::Read(&mem->published)+1;s->pid=mem->pid;
                    blog(LOG_INFO,"[iidx-live] Connected to game PID %u",mem->pid);
                }else{UnmapViewOfFile(mem);mem=nullptr;}
            }else if(mem){UnmapViewOfFile(mem);mem=nullptr;}
            if(!mem){if(mapping)CloseHandle(mapping);mapping=nullptr;if(process)CloseHandle(process);process=nullptr;Sleep(100);continue;}
        }
        if(WaitForSingleObject(process,0)!=WAIT_TIMEOUT||mem->epoch!=epoch||mem->magic!=Live::Magic){
            s->pid=0;UnmapViewOfFile(mem);mem=nullptr;CloseHandle(mapping);mapping=nullptr;CloseHandle(process);process=nullptr;
            blog(LOG_INFO,"[iidx-live] Game disconnected; waiting for restart");continue;
        }
        LONG64 latest=Live::Read(&mem->published);
        if(next<=latest-Live::Slots)next=latest; // recover live, never replay a backlog
        unsigned limit=Live::Slots;
        while(next<=latest&&limit--){
            bool ok=Live::Copy(mem,next++,packet);if(!ok)continue;
            uint64_t now=Live::Now();
            if(packet.timestamp>now+100000000ULL||now>packet.timestamp+250000000ULL)continue;
            obs_source_audio audio{};audio.data[0]=(uint8_t*)packet.samples;
            audio.frames=packet.frames;audio.speakers=SPEAKERS_STEREO;
            audio.format=AUDIO_FORMAT_FLOAT;audio.samples_per_sec=packet.rate;
            audio.timestamp=packet.timestamp;obs_source_output_audio(s->source,&audio);
        }
        Sleep(2);
    }
    if(mem)UnmapViewOfFile(mem);if(mapping)CloseHandle(mapping);if(process)CloseHandle(process);
}
static std::string Utf8(const wchar_t* text){
    int n=WideCharToMultiByte(CP_UTF8,0,text,-1,nullptr,0,nullptr,nullptr);if(n<=1)return {};
    std::string out(n,'\0');WideCharToMultiByte(CP_UTF8,0,text,-1,out.data(),n,nullptr,nullptr);out.pop_back();return out;
}
static std::string Escape(std::string text){
    std::string out;for(char c:text){if(c=='#')out+="#22";else if(c==':')out+="#3A";else out+=c;}return out;
}
struct WindowSearch{DWORD pid;HWND window=nullptr;LONG64 area=0;};
static BOOL CALLBACK FindWindow(HWND w,LPARAM param){
    auto s=(WindowSearch*)param;DWORD pid;GetWindowThreadProcessId(w,&pid);
    if(pid!=s->pid||!IsWindowVisible(w)||GetWindow(w,GW_OWNER))return TRUE;
    wchar_t title[512]{};GetWindowTextW(w,title,512);size_t n=wcslen(title);
    if(!wcsstr(title,L"beatmania IIDX")||n<5||_wcsicmp(title+n-5,L" main"))return TRUE;
    RECT r{};GetClientRect(w,&r);LONG64 area=(LONG64)(r.right-r.left)*(r.bottom-r.top);
    if(area>s->area){s->area=area;s->window=w;}return TRUE;
}
static void Tick(void* data,float seconds){
    auto s=(Source*)data;s->elapsed+=seconds;
    if(!s->automatic||s->elapsed<1||!s->video)return;s->elapsed=0;
    DWORD pid=s->pid.load();if(!pid)return;
    WindowSearch search{pid};EnumWindows(FindWindow,(LPARAM)&search);if(!search.window)return;
    wchar_t title[1024]{},cls[512]{},path[32768]{};GetWindowTextW(search.window,title,1024);GetClassNameW(search.window,cls,512);
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!process)return;
    DWORD count=32768;BOOL ok=QueryFullProcessImageNameW(process,0,path,&count);CloseHandle(process);if(!ok)return;
    const wchar_t* exe=wcsrchr(path,L'\\');exe=exe?exe+1:path;
    std::string window=Escape(Utf8(title))+":"+Escape(Utf8(cls))+":"+Escape(Utf8(exe));
    if(window==s->window)return;s->window=window;
    auto settings=obs_source_get_settings(s->video);
    obs_data_set_string(settings,"capture_mode","window");obs_data_set_string(settings,"window",window.c_str());
    obs_data_set_int(settings,"priority",1);obs_data_set_bool(settings,"capture_audio",false);
    obs_source_update(s->video,settings);obs_data_release(settings);
    blog(LOG_INFO,"[iidx-live] Video target: %s",window.c_str());
}
static void Update(void* data,obs_data_t* settings){
    auto s=(Source*)data;s->automatic=obs_data_get_bool(settings,"iidx_auto_window");s->window.clear();
    if(s->video){
        obs_data_set_bool(settings,"capture_audio",false);
        if(s->automatic){obs_data_set_int(settings,"priority",1);obs_data_set_string(settings,"capture_mode","window");}
        obs_source_update(s->video,settings);
    }
}
static void* Create(obs_data_t* settings,obs_source_t* source){
    auto s=new Source;s->source=source;
    obs_data_set_bool(settings,"capture_audio",false);
    s->video=obs_source_create_private("game_capture","IIDX internal game capture",settings);
    if(!s->video){blog(LOG_ERROR,"[iidx-live] OBS game_capture source unavailable");delete s;return nullptr;}
    obs_source_add_active_child(source,s->video);Update(s,settings);
    s->reader=std::thread(Reader,s);return s;
}
static void Destroy(void* data){auto s=(Source*)data;s->stop=true;s->reader.join();obs_source_remove_active_child(s->source,s->video);obs_source_release(s->video);delete s;}
static const char* Name(void*){return "IIDX 游戏音画（原生音频）";}
static void Defaults(obs_data_t* d){
    obs_data_set_default_bool(d,"iidx_auto_window",true);
    obs_data_set_default_string(d,"capture_mode","window");
    obs_data_set_default_string(d,"window","IIDX:IIDX:spice64.exe");
    obs_data_set_default_int(d,"priority",1);
    obs_data_set_default_bool(d,"capture_cursor",false);
    obs_data_set_default_bool(d,"limit_framerate",true);
    obs_data_set_default_bool(d,"anti_cheat_hook",false);
    obs_data_set_default_bool(d,"capture_audio",false);
}
static obs_properties_t* Properties(void* data){
    auto s=(Source*)data;auto p=s&&s->video?obs_source_properties(s->video):obs_properties_create();
    obs_properties_remove_by_name(p,"capture_audio");
    obs_properties_add_bool(p,"iidx_auto_window","自动选择 IIDX 主窗口（main，排除副屏）");
    obs_properties_add_text(p,"iidx_help","固定 1920×1080 画布，可在启动游戏前摆放。未捕获时完全透明，可在来源列表选中并调整；接上游戏后位置与大小保持不变。声音由 IIDXLive.dll 提供，保留 ASIO。",OBS_TEXT_INFO);
    return p;
}
static void Rectangle(float x,float y,uint32_t w,uint32_t h,float r,float g,float b){
    auto effect=obs_get_base_effect(OBS_EFFECT_SOLID);vec4 color;vec4_set(&color,r,g,b,1.0f);
    gs_matrix_push();gs_matrix_translate3f(x,y,0);
    gs_effect_set_vec4(gs_effect_get_param_by_name(effect,"color"),&color);
    while(gs_effect_loop(effect,"Solid"))gs_draw_sprite(nullptr,0,w,h);
    gs_matrix_pop();
}
static void Render(void* data,gs_effect_t*){
    auto s=(Source*)data;uint32_t w=obs_source_get_width(s->video),h=obs_source_get_height(s->video);
    // Keep the source bounds selectable without covering scene decorations.
    if(!w||!h)return;
    // Fixed source dimensions preserve OBS transforms across attach/restart.
    // Fit unusual capture sizes without distorting the aspect ratio.
    float scale=std::min(1920.0f/w,1080.0f/h);
    Rectangle(0,0,1920,1080,0,0,0);
    gs_matrix_push();gs_matrix_translate3f((1920-w*scale)/2,(1080-h*scale)/2,0);
    gs_matrix_scale3f(scale,scale,1);obs_source_video_render(s->video);gs_matrix_pop();
}
static uint32_t Width(void*){return 1920;}
static uint32_t Height(void*){return 1080;}
static void Enum(void* data,obs_source_enum_proc_t cb,void* param){auto s=(Source*)data;cb(s->source,s->video,param);}
bool obs_module_load(void){
    obs_source_info info{};info.id="iidx_live";info.type=OBS_SOURCE_TYPE_INPUT;
    info.output_flags=OBS_SOURCE_VIDEO|OBS_SOURCE_AUDIO|OBS_SOURCE_CUSTOM_DRAW;
    info.get_name=Name;info.create=Create;info.destroy=Destroy;info.get_defaults=Defaults;
    info.get_properties=Properties;info.update=Update;info.video_render=Render;
    info.get_width=Width;info.get_height=Height;info.video_tick=Tick;info.enum_active_sources=Enum;
    obs_register_source(&info);return true;
}
