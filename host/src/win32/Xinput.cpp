// xinput1_4's XInput for the engine (title 1.07: the game reached its menu,
// but the engine logged "desktop XInput unavailable; no controller exposed"
// and its Windows.Xbox.Input had no gamepad). User 0 is the PS5 controller
// Input.cpp polls; the others are not connected.
#include "Win32.h"
#include "../Input.h"
#include "GamepadAbi.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

using namespace azimuth::win32;
namespace input=azimuth::input;

namespace {
constexpr uint32_t Success=0,BadArguments=160,DeviceNotConnected=1167;
std::atomic<bool> enabled{true},announced{false};
// XINPUT_GAMEPAD: wButtons, bLeftTrigger, bRightTrigger, sThumbLX/LY/RX/RY.
struct Gamepad {uint16_t buttons;uint8_t leftTrigger,rightTrigger;int16_t thumbLX,thumbLY,thumbRX,thumbRY;};
static_assert(sizeof(Gamepad)==12,"XINPUT_GAMEPAD");
// A ScePad stick byte (0..255, down/right growing) as an XInput axis.
int16_t axis(uint8_t value,bool flip) {
    int v=(int(value)-128)*258;
    if(flip)v=-v;
    return int16_t(v<-32768?-32768:v>32767?32767:v);
}
Gamepad gamepad(const input::Pad& pad) {
    struct Bit {uint32_t sce;uint16_t xinput;};
    static const Bit bits[]={
        {input::Up,0x1},{input::Down,0x2},{input::Left,0x4},{input::Right,0x8},
        {input::Options,0x10},{input::TouchPad,0x20},{input::L3,0x40},{input::R3,0x80},
        {input::L1,0x100},{input::R1,0x200},
        {input::Cross,0x1000},{input::Circle,0x2000},{input::Square,0x4000},{input::Triangle,0x8000}};
    Gamepad g{};
    for(auto const& b:bits)if(pad.buttons&b.sce)g.buttons|=b.xinput;
    g.leftTrigger=pad.left2;g.rightTrigger=pad.right2;
    g.thumbLX=axis(pad.leftX,false);g.thumbLY=axis(pad.leftY,true);
    g.thumbRX=axis(pad.rightX,false);g.thumbRY=axis(pad.rightY,true);
    return g;
}
bool connected(uint32_t user){return user==0&&input::pad().connected;}
}

extern "C" {
uint32_t win_XInputGetState(uint32_t user,void* state) {
    if(user>3||!state)return BadArguments;
    if(!connected(user))return DeviceNotConnected;
    input::polledByXInput();
    if(!announced.exchange(true))log("XInput: controle do PS5 entregue ao motor como usuario 0");
    auto pad=input::pad();
    // XINPUT_STATE: dwPacketNumber, Gamepad. With XInputEnable(FALSE) the state reads neutral.
    Gamepad g=enabled?gamepad(pad):Gamepad{};
    auto out=static_cast<uint8_t*>(state);
    std::memcpy(out,&pad.packet,4);std::memcpy(out+4,&g,sizeof g);
    return Success;
}
uint32_t win_XInputSetState(uint32_t user,const void* vibration) {
    if(user>3||!vibration)return BadArguments;
    if(!connected(user))return DeviceNotConnected;
    // XINPUT_VIBRATION: wLeftMotorSpeed (the large, low-frequency one), wRightMotorSpeed.
    uint16_t motors[2];std::memcpy(motors,vibration,4);
    if(enabled)input::vibrate(uint8_t(motors[0]>>8),uint8_t(motors[1]>>8));
    return Success;
}
uint32_t win_XInputGetCapabilities(uint32_t user,uint32_t,void* capabilities) {
    if(user>3||!capabilities)return BadArguments;
    if(!connected(user))return DeviceNotConnected;
    // XINPUT_CAPABILITIES: Type (gamepad), SubType (gamepad), Flags, Gamepad (the
    // inputs it has, all of them at full resolution), Vibration (both motors).
    auto out=static_cast<uint8_t*>(capabilities);
    std::memset(out,0,20);
    out[0]=1;out[1]=1;
    Gamepad all{0xf3ff,0xff,0xff,-64,-64,-64,-64};
    std::memcpy(out+4,&all,sizeof all);
    uint16_t motors[2]={0xffff,0xffff};std::memcpy(out+16,motors,4);
    return Success;
}
void win_XInputEnable(int32_t enable) {
    enabled=enable!=0;
    if(!enabled)input::vibrate(0,0);
}
uint32_t win_XInputGetBatteryInformation(uint32_t user,uint8_t,void* information) {
    if(user>3||!information)return BadArguments;
    if(!connected(user))return DeviceNotConnected;
    // XINPUT_BATTERY_INFORMATION: BatteryType (wired), BatteryLevel (full).
    auto out=static_cast<uint8_t*>(information);
    out[0]=1;out[1]=3;
    return Success;
}
}

// Windows.Gaming.Input's Gamepad for the 1.0.2 engine (title 1.51): it reads
// its controllers there and no longer loads XInput ("pads: Windows.Gaming.Input
// unavailable, no controller exposed"). RoGetActivationFactory
// (LauncherApi.cpp) gives the statics; Gamepads lists the PS5 controller,
// always (a disconnected one reads neutral), so the GamepadAdded and
// GamepadRemoved handlers are kept by the engine but never called.
namespace {
constexpr int32_t Ok=0,Pointer=int32_t(0x80004003),NoInterface=int32_t(0x80004002),Bounds=int32_t(0x8000000B);
struct Guid {uint32_t a;uint16_t b,c;uint8_t d[8];};
constexpr Guid Unknown{0x00000000,0x0000,0x0000,{0xc0,0,0,0,0,0,0,0x46}};
constexpr Guid Inspectable{0xaf86e2e0,0xb12d,0x4c6a,{0x9c,0x5a,0xd7,0xaa,0x65,0x10,0x1e,0x90}};
constexpr Guid Agile{0x94ea2b94,0xe9cc,0x49e0,{0xc0,0xff,0xee,0x64,0xca,0x8f,0x5b,0x90}};
constexpr Guid GamepadStatics{0x8bbce529,0xd49c,0x39e9,{0x95,0x60,0xe4,0x7d,0xde,0x96,0xb7,0xc8}};
constexpr Guid GamepadInterface{0xbc7bb43c,0x0a69,0x3903,{0x9e,0x9d,0xa5,0x0f,0x86,0xa4,0x5d,0xe5}};
bool same(const void* id,const Guid& g){return !std::memcmp(id,&g,sizeof g);}
// The three objects are the process's only ones: never freed.
struct Object {const void* const* vtable;};
Object statics{guestGamepadStaticsVtable},list{guestGamepadListVtable},device{guestGamepadVtable};
// GamepadVibration: LeftMotor, RightMotor, LeftTrigger, RightTrigger (0..1).
double vibration[4]={};
std::atomic<int64_t> tokens{0};
std::atomic<bool> readAnnounced{false};
double unit(uint8_t value){return value/255.0;}
// A ScePad stick byte as a Windows.Gaming.Input axis (-1..1, up positive).
double thumb(uint8_t value,bool flip) {
    double v=(int(value)-128)/127.0;
    if(flip)v=-v;
    return v<-1?-1:v>1?1:v;
}
uint32_t buttons(uint32_t sce) {
    struct Bit {uint32_t sce,wgi;};
    // GamepadButtons: Menu, View, A, B, X, Y, the pad, the shoulders, the sticks.
    static const Bit bits[]={
        {input::Options,0x1},{input::TouchPad,0x2},{input::Cross,0x4},{input::Circle,0x8},{input::Square,0x10},{input::Triangle,0x20},
        {input::Up,0x40},{input::Down,0x80},{input::Left,0x100},{input::Right,0x200},{input::L1,0x400},{input::R1,0x800},
        {input::L3,0x1000},{input::R3,0x2000}};
    uint32_t out=0;
    for(auto const& b:bits)if(sce&b.sce)out|=b.wgi;
    return out;
}
}

// RoGetActivationFactory("Windows.Gaming.Input.Gamepad").
int32_t gamepadStatics(const void* id,void** out) {
    if(!out)return Pointer;
    *out=nullptr;
    if(!id||!(same(id,GamepadStatics)||same(id,Inspectable)||same(id,Unknown)||same(id,Agile)))return NoInterface;
    log("Windows.Gaming.Input: controle do PS5 entregue ao motor como Gamepad");
    *out=&statics;return Ok;
}

extern "C" {
int32_t wgi_QueryInterface(void* self,const void* id,void** out) {
    if(!out)return Pointer;
    *out=nullptr;
    if(!id)return Pointer;
    bool supported=same(id,Unknown)||same(id,Inspectable)||same(id,Agile)||
        (self==&statics&&same(id,GamepadStatics))||(self==&device&&same(id,GamepadInterface));
    if(!supported) {
        auto g=static_cast<const Guid*>(id);
        static std::atomic<unsigned> reports{0};
        if(reports++<20){char text[40];std::snprintf(text,sizeof text,"%08x-%04x-%04x",g->a,g->b,g->c);log(std::string("Windows.Gaming.Input: objeto sem a interface ")+text);}
        return NoInterface;
    }
    *out=self;return Ok;
}
uint32_t wgi_AddRef(void*){return 1;}
uint32_t wgi_Release(void*){return 1;}
int32_t wgi_GetIids(void*,uint32_t* count,void** ids){if(!count||!ids)return Pointer;*count=0;*ids=nullptr;return Ok;}
int32_t wgi_GetRuntimeClassName(void*,void** name){if(!name)return Pointer;*name=nullptr;return Ok;}
int32_t wgi_GetTrustLevel(void*,int32_t* level){if(!level)return Pointer;*level=0;return Ok;}

int32_t wgi_AddGamepadAdded(void*,void* handler,int64_t* token){if(!handler||!token)return Pointer;*token=++tokens;return Ok;}
int32_t wgi_RemoveGamepadAdded(void*,int64_t){return Ok;}
int32_t wgi_AddGamepadRemoved(void*,void* handler,int64_t* token){if(!handler||!token)return Pointer;*token=++tokens;return Ok;}
int32_t wgi_RemoveGamepadRemoved(void*,int64_t){return Ok;}
int32_t wgi_GetGamepads(void*,void** out){if(!out)return Pointer;*out=&list;return Ok;}

int32_t wgi_ListGetAt(void*,uint32_t index,void** item){if(!item)return Pointer;*item=nullptr;if(index)return Bounds;*item=&device;return Ok;}
int32_t wgi_ListGetSize(void*,uint32_t* size){if(!size)return Pointer;*size=1;return Ok;}
int32_t wgi_ListIndexOf(void*,void* item,uint32_t* index,uint8_t* found){if(!index||!found)return Pointer;*index=0;*found=item==&device;return Ok;}
int32_t wgi_ListGetMany(void*,uint32_t start,uint32_t capacity,void** items,uint32_t* actual) {
    if(!actual||(capacity&&!items))return Pointer;
    *actual=0;
    if(start==0&&capacity){items[0]=&device;*actual=1;}
    return Ok;
}

int32_t wgi_GetVibration(void*,void* out){if(!out)return Pointer;std::memcpy(out,vibration,sizeof vibration);return Ok;}
int32_t wgi_PutVibration(void*,const void* value) {
    if(!value)return Pointer;
    std::memcpy(vibration,value,sizeof vibration);
    auto motor=[](double v){return uint8_t(v<=0?0:v>=1?255:v*255);};
    input::vibrate(motor(vibration[0]),motor(vibration[1]));
    return Ok;
}
int32_t wgi_GetCurrentReading(void*,void* out) {
    if(!out)return Pointer;
    input::polledByXInput();
    if(!readAnnounced.exchange(true))log("Windows.Gaming.Input: motor leu o controle pela primeira vez");
    auto pad=input::pad();
    // GamepadReading: Timestamp (100 ns, QueryPerformanceCounter's), Buttons,
    // LeftTrigger, RightTrigger, LeftThumbstickX/Y, RightThumbstickX/Y.
    struct Reading {uint64_t timestamp;uint32_t buttons,pad;double leftTrigger,rightTrigger,leftX,leftY,rightX,rightY;} r{};
    static_assert(sizeof(Reading)==64,"GamepadReading");
    r.timestamp=uint64_t(fastMonotonicNs()/100);
    if(pad.connected) {
        r.buttons=buttons(pad.buttons);
        r.leftTrigger=unit(pad.left2);r.rightTrigger=unit(pad.right2);
        r.leftX=thumb(pad.leftX,false);r.leftY=thumb(pad.leftY,true);
        r.rightX=thumb(pad.rightX,false);r.rightY=thumb(pad.rightY,true);
    }
    std::memcpy(out,&r,sizeof r);
    return Ok;
}
}
