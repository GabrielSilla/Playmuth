#include "Input.h"
#include "Platform.h"
#include "win32/Win32.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <cstdio>
#include <cstring>
#include <thread>
#include <unistd.h>

#if !defined(__linux__)
// libScePad / libSceUserService (the pad sequence the castation and PS5_Vulkan
// projects validated on the console): initialize, open the initial user's
// standard pad, read its cached state.
extern "C" {
int sceUserServiceInitialize(const void* parameters);
int sceUserServiceGetInitialUser(int32_t* user);
int scePadInit(void);
int scePadOpen(int32_t user,int32_t type,int32_t index,void* parameters);
int scePadGetHandle(int32_t user,int32_t type,int32_t index);
int scePadReadState(int32_t handle,void* data);
int scePadSetVibration(int32_t handle,const void* parameters);   // ScePadVibrationParam: large motor, small motor
}
#endif

namespace azimuth::input {
namespace {
std::mutex lock;
Pad latest;
std::once_flag started;
std::atomic<int64_t> lastXInputPoll{-1000000};   // ms on the steady clock
int64_t milliseconds(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
[[maybe_unused]] bool xinputActive(){return milliseconds()-lastXInputPoll.load()<1000;}

#if !defined(__linux__)
std::atomic<int> padHandle{-1};
// ScePadData: buttons u32 at 0, sticks at 4..7, triggers at 8 and 9, connected at 76.
constexpr size_t SampleBytes=256,ConnectedAt=76;
int open() {
    int init=sceUserServiceInitialize(nullptr);
    int32_t user=-1;
    int result=sceUserServiceGetInitialUser(&user);
    win32::log("Controle: sceUserServiceInitialize "+std::to_string(init)+", usuario inicial "+std::to_string(user)+" ("+std::to_string(result)+")");
    int pad=scePadInit();
    int handle=scePadOpen(user,0,0,nullptr);
    win32::log("Controle: scePadInit "+std::to_string(pad)+", scePadOpen "+std::to_string(handle));
    if(handle<0)handle=scePadGetHandle(user,0,0);
    if(handle<0)win32::log("Controle: sem acesso ao controle (scePadGetHandle "+std::to_string(handle)+")");
    return handle;
}
void poll() {
    if(auto scheduling=platform::shareCpu(true);!scheduling.empty())win32::log("Agendamento do controle: "+scheduling);
    int handle=open();
    if(handle<0)return;
    padHandle=handle;
    uint8_t sample[SampleBytes];
    bool announced=false;
    for(;;) {
        if(scePadReadState(handle,sample)>=0) {
            Pad next;
            uint32_t buttons;std::memcpy(&buttons,sample,4);
            next.buttons=(buttons&0x80000000u)?0:buttons;
            next.leftX=sample[4];next.leftY=sample[5];next.rightX=sample[6];next.rightY=sample[7];
            next.left2=sample[8];next.right2=sample[9];
            next.connected=sample[ConnectedAt]!=0;
            if(next.connected&&!announced){announced=true;win32::log("Controle conectado");}
            std::lock_guard<std::mutex> guard(lock);
            next.packet=latest.packet;
            if(next.buttons!=latest.buttons||next.connected!=latest.connected||next.leftX!=latest.leftX||next.leftY!=latest.leftY||
               next.rightX!=latest.rightX||next.rightY!=latest.rightY||next.left2!=latest.left2||next.right2!=latest.right2)++next.packet;
            latest=next;
        }
        // Edges become key presses for the launcher.
        Pad now=pad();
        static Pad before;
        static unsigned logged=0;
        if(now.buttons!=before.buttons&&logged<60){++logged;char text[96];std::snprintf(text,sizeof text,"Controle: botoes 0x%x, analogico esquerdo %u,%u",now.buttons,now.leftX,now.leftY);win32::log(text);}
        // A key goes down only while the engine does not read XInput; a key
        // that went down always comes up.
        static bool keyDown[256];
        bool keys=!xinputActive();
        auto post=[&](uint32_t vk,uint32_t scan,bool is) {
            if(is&&keys&&!keyDown[vk]){keyDown[vk]=true;win32::postKey(vk,scan,true);}
            else if(!is&&keyDown[vk]){keyDown[vk]=false;win32::postKey(vk,scan,false);}
        };
        auto key=[&](uint32_t bit,uint32_t vk,uint32_t scan) {
            bool was=before.buttons&bit,is=now.buttons&bit;
            if(is!=was)post(vk,scan,is);
        };
        key(Up,0x26,0x48);key(Down,0x28,0x50);key(Left,0x25,0x4b);key(Right,0x27,0x4d);
        key(Cross,0x0d,0x1c);key(Options,0x0d,0x1c);key(Circle,0x1b,0x01);
        // The left stick as the arrows, with a dead zone and hysteresis.
        auto stick=[&](bool wasOn,bool isOn,uint32_t vk,uint32_t scan){if(isOn!=wasOn)post(vk,scan,isOn);};
        static bool sUp,sDown,sLeft,sRight;
        bool up=now.leftY<64,down=now.leftY>192,left=now.leftX<64,right=now.leftX>192;
        stick(sUp,up,0x26,0x48);stick(sDown,down,0x28,0x50);stick(sLeft,left,0x25,0x4b);stick(sRight,right,0x27,0x4d);
        sUp=up;sDown=down;sLeft=left;sRight=right;
        before=now;
        usleep(4000);
    }
}
#endif
}

void start() {
    std::call_once(started,[]{
#if !defined(__linux__)
        std::thread(poll).detach();
#else
        win32::log("Controle: so no console");
#endif
    });
}
Pad pad(){std::lock_guard<std::mutex> guard(lock);return latest;}
void polledByXInput(){lastXInputPoll=milliseconds();}
void vibrate(uint8_t large,uint8_t small) {
#if !defined(__linux__)
    static std::atomic<uint16_t> last{0};
    uint16_t now=uint16_t(large<<8|small);
    if(padHandle<0||last.exchange(now)==now)return;
    uint8_t motors[2]={large,small};
    scePadSetVibration(padHandle,motors);
#else
    (void)large;(void)small;
#endif
}
}
