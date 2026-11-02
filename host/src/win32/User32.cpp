// Windows, classes and message queues of the engine's launcher, ported from
// AzimuthUwp's GuestWindows: top-level windows only, each owned by the
// thread that created it, with that thread's queue of sent and posted
// messages and its timers. Window procedures run on their owner thread
// only; a message for another thread's window is a diagnostic stop. Nothing
// reaches a screen: GDI (Gdi.cpp) draws into the windows' client areas and
// the host saves the last picture. No input reaches the engine yet.
#include "Win32.h"
#include "../GuestThreads.h"
#include "../HostHeap.h"
#include "../Platform.h"
#include "../guest_abi.h"
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <sstream>
#include <vector>

#define EXPORT(dll,name,ret,params,args) extern "C" ret win_##name params;
#include "exports.inc"
#undef EXPORT

using namespace azimuth::win32;

namespace {
// x64 layouts of the structures the engine passes.
struct Rect {int32_t left,top,right,bottom;};
struct Point {int32_t x,y;};
struct Msg {void* window;uint32_t message;uint64_t w;int64_t l;uint32_t time;Point pt;uint32_t priv;};
struct WndClassEx {uint32_t size,style;void* proc;int32_t classExtra,windowExtra;void* instance;void* icon;void* cursor;void* background;const uint16_t* menuName;const uint16_t* className;void* smallIcon;};
struct CreateStruct {void* parameter;void* instance;void* menu;void* parent;int32_t cy,cx,y,x;int32_t style;const uint16_t* name;const uint16_t* className;uint32_t exStyle;};
struct WindowPos {void* window;void* after;int32_t x,y,cx,cy;uint32_t flags;};
struct WindowPlacement {uint32_t length,flags,showCmd;Point minPosition,maxPosition;Rect normalPosition;};
struct MonitorInfo {uint32_t size;Rect monitor,work;uint32_t flags;};
struct MinMaxInfo {Point reserved,maxSize,maxPosition,minTrackSize,maxTrackSize;};
struct StyleStruct {uint32_t styleOld,styleNew;};
static_assert(sizeof(Msg)==48&&offsetof(Msg,w)==16&&offsetof(Msg,time)==32&&offsetof(Msg,pt)==36);
static_assert(sizeof(WndClassEx)==80&&offsetof(WndClassEx,className)==64);
static_assert(sizeof(CreateStruct)==80&&offsetof(CreateStruct,cy)==32&&offsetof(CreateStruct,style)==48&&offsetof(CreateStruct,exStyle)==72);
static_assert(sizeof(WindowPos)==40&&sizeof(WindowPlacement)==44&&sizeof(MonitorInfo)==40&&sizeof(MinMaxInfo)==40);

enum : uint32_t {
    WmCreate=0x1,WmDestroy=0x2,WmMove=0x3,WmSize=0x5,WmActivate=0x6,WmSetFocus=0x7,WmKillFocus=0x8,WmEnable=0xa,WmClose=0x10,
    WmQueryEndSession=0x11,WmQuit=0x12,WmShowWindow=0x18,WmGetMinMaxInfo=0x24,WmWindowPosChanging=0x46,WmWindowPosChanged=0x47,
    WmStyleChanging=0x7c,WmStyleChanged=0x7d,WmNcCreate=0x81,WmNcDestroy=0x82,WmNcCalcSize=0x83,
    WmKeyDown=0x100,WmKeyUp=0x101,WmSysKeyDown=0x104,WmSysKeyUp=0x105,WmSysCommand=0x112,WmTimer=0x113,
    // Above 0xFFFF messages are the system's and never reach a window procedure:
    // the queue uses this one to activate a window from another thread.
    HostActivate=0x10001
};
enum : uint32_t {WsVisible=0x10000000,WsDisabled=0x08000000,WsChild=0x40000000};
enum : uint32_t {
    SwpNoSize=0x1,SwpNoMove=0x2,SwpNoZOrder=0x4,SwpNoRedraw=0x8,SwpNoActivate=0x10,SwpFrameChanged=0x20,SwpShowWindow=0x40,
    SwpHideWindow=0x80,SwpNoCopyBits=0x100,SwpNoOwnerZOrder=0x200,SwpNoSendChanging=0x400,SwpDeferErase=0x2000,SwpAsyncWindowPos=0x4000
};
enum : int {SwHide=0,SwShowNormal=1,SwShowNoActivate=4,SwShow=5,SwShowNa=8,SwRestore=9,SwShowDefault=10};
enum : int {GwlStyle=-16,GwlExStyle=-20,GwlpWndProc=-4,GwlpUserData=-21,GwlpId=-12,GwlpHInstance=-6,GwlpHwndParent=-8};
enum : uint32_t {
    ErrorInvalidWindowHandle=1400,ErrorCannotFindWndClass=1407,ErrorClassAlreadyExists=1410,ErrorInvalidIndex=1413,
    ErrorInvalidMonitorHandle=1461,ErrorResourceTypeNotFound=1813
};
constexpr uint32_t Infinite=0xffffffff,WaitObject0=0,WaitTimeout=258,WaitFailed=0xffffffff;
constexpr uint32_t PmRemove=1,PmNoYield=2,MwmoAlertable=2,MwmoInputAvailable=4;
constexpr uint32_t QsKey=0x1,QsPostMessage=0x8,QsTimer=0x10,QsSendMessage=0x40;
constexpr uint32_t UserTimerMinimum=0xa,UserTimerMaximum=0x7fffffff;
void* const Monitor=reinterpret_cast<void*>(uintptr_t(0xaf0001));

std::string hex(uint64_t value){std::ostringstream s;s<<"0x"<<std::hex<<value;return s.str();}
std::string text(const uint16_t* value){return value?utf8(value,length(value)):std::string();}
// Class names compare case-insensitively, as in USER32.
std::u16string key(const uint16_t* name) {
    std::u16string s;for(;*name;++name)s.push_back(char16_t(*name>='a'&&*name<='z'?*name-32:*name));
    return s;
}
bool isAtom(const void* name){return reinterpret_cast<uintptr_t>(name)<=0xffff;}
uint32_t self(){return win_GetCurrentThreadId();}
uint32_t tick(){return uint32_t(win_GetTickCount64());}
[[noreturn]] void unsupported(const std::string& what){stopGuest("USER32 ainda sem adaptacao para este uso: "+what);}
int64_t makeLong(int32_t a,int32_t b){return int64_t(uint32_t(uint16_t(a))|(uint32_t(uint16_t(b))<<16));}

struct Windows {
    struct Class {void* proc;void* instance;uint32_t style;uint16_t atom;};
    struct Window {uint32_t thread;void* proc;uint16_t atom;void* instance;int64_t style,exStyle,userData,id;Rect rect;bool destroying;};
    struct Timer {void* window;uint64_t id;uint32_t thread;uint32_t elapse;uint64_t due;void* proc;};
    struct Queue {std::deque<Msg> sent,posted;void* signal=nullptr;};
    std::mutex mutex;
    std::map<std::u16string,Class> classes;
    std::map<void*,Window> windows;
    std::map<uint32_t,Queue> queues;
    std::vector<Timer> timers;
    void* active=nullptr;void* focused=nullptr;void* lastActive=nullptr;
    void* cursor=nullptr;
    Point cursorPos{DisplayWidth/2,DisplayHeight/2};   // Windows starts it at the centre
    uintptr_t nextWindow=0xae0002;
    uint16_t nextAtom=0xc100;
    uint64_t nextTimer=0x7e00;

    // Called with the mutex held.
    Queue& queue(uint32_t thread) {
        auto& q=queues[thread];
        if(!q.signal)q.signal=hostEvent();
        return q;
    }
    void deliver(uint32_t thread,const Msg& msg,bool sent) {
        auto& q=queue(thread);(sent?q.sent:q.posted).push_back(msg);setHostEvent(q.signal,true);
    }
    void* procFor(void* window,uint32_t* thread) {
        std::lock_guard<std::mutex> lock(mutex);
        auto found=windows.find(window);if(found==windows.end())return nullptr;
        if(thread)*thread=found->second.thread;
        return found->second.proc;
    }
    void requireOwner(void* window,const char* api) {
        uint32_t thread=0;
        if(procFor(window,&thread)&&thread!=self())unsupported(std::string(api)+" sobre janela de outra thread");
    }
    bool exists(void* window){std::lock_guard<std::mutex> lock(mutex);return window&&windows.count(window);}
    bool visible(void* window) {
        std::lock_guard<std::mutex> lock(mutex);auto found=windows.find(window);
        return found!=windows.end()&&(found->second.style&WsVisible);
    }
    bool enabled(void* window) {
        std::lock_guard<std::mutex> lock(mutex);auto found=windows.find(window);
        return found!=windows.end()&&!(found->second.style&WsDisabled);
    }
    void* snapshot(void* const& field){std::lock_guard<std::mutex> lock(mutex);return field;}

    int64_t send(void* window,uint32_t message,uint64_t w,int64_t l) {
        uint32_t thread=0;auto proc=procFor(window,&thread);
        if(!proc){setLastError(ErrorInvalidWindowHandle);return 0;}
        if(thread!=self())unsupported("mensagem enviada a janela de outra thread");
        return guestCallWindowProc(proc,window,message,w,l);
    }
    void changeActive(void* window) {
        void* old;
        {std::lock_guard<std::mutex> lock(mutex);old=active;if(old==window)return;active=window;if(window)lastActive=window;}
        if(old&&exists(old)) {
            send(old,WmActivate,0,reinterpret_cast<int64_t>(window));
            if(snapshot(focused)==old)focus(nullptr);
        }
        if(window&&exists(window))send(window,WmActivate,1,reinterpret_cast<int64_t>(old));
    }
    bool activate(void* window) {
        uint32_t thread=0;
        if(!procFor(window,&thread)){setLastError(ErrorInvalidWindowHandle);return false;}
        {
            std::lock_guard<std::mutex> lock(mutex);lastActive=window;
            if(thread!=self()){deliver(thread,{window,HostActivate,1,0,0,{},0},true);return true;}
        }
        changeActive(window);return true;
    }
    void* focus(void* window) {
        if(window&&!exists(window)){setLastError(ErrorInvalidWindowHandle);return nullptr;}
        if(window)requireOwner(window,"SetFocus");
        void* old;{std::lock_guard<std::mutex> lock(mutex);old=focused;if(old==window)return old;focused=window;}
        if(old&&exists(old))send(old,WmKillFocus,reinterpret_cast<uint64_t>(window),0);
        if(window&&exists(window))send(window,WmSetFocus,reinterpret_cast<uint64_t>(old),0);
        return old;
    }
    bool destroy(void* window) {
        bool wasActive,wasFocus;
        {
            std::unique_lock<std::mutex> lock(mutex);
            auto found=windows.find(window);if(found==windows.end()){setLastError(ErrorInvalidWindowHandle);return false;}
            if(found->second.thread!=self()){lock.unlock();unsupported("DestroyWindow de janela de outra thread");}
            if(found->second.destroying)return true;
            found->second.destroying=true;found->second.style&=~int64_t(WsVisible);
            if(lastActive==window)lastActive=nullptr;
            wasActive=active==window;wasFocus=focused==window;
        }
        if(wasActive)changeActive(nullptr);
        if(wasFocus)focus(nullptr);
        send(window,WmDestroy,0,0);
        send(window,WmNcDestroy,0,0);
        std::lock_guard<std::mutex> lock(mutex);
        auto& q=queue(windows[window].thread);
        auto mine=[&](Msg const& m){return m.window==window;};
        q.posted.erase(std::remove_if(q.posted.begin(),q.posted.end(),mine),q.posted.end());
        q.sent.erase(std::remove_if(q.sent.begin(),q.sent.end(),mine),q.sent.end());
        timers.erase(std::remove_if(timers.begin(),timers.end(),[&](Timer const& t){return t.window==window;}),timers.end());
        windows.erase(window);
        log("Janela do motor destruida: "+hex(reinterpret_cast<uintptr_t>(window)));
        return true;
    }
    bool show(void* window,int command) {
        if(!exists(window)){setLastError(ErrorInvalidWindowHandle);return false;}
        requireOwner(window,"ShowWindow");
        bool was=visible(window);
        if(command==SwHide) {
            if(was) {
                send(window,WmShowWindow,0,0);
                {std::lock_guard<std::mutex> lock(mutex);auto f=windows.find(window);if(f!=windows.end())f->second.style&=~int64_t(WsVisible);}
                if(snapshot(active)==window)changeActive(nullptr);
            }
            return was;
        }
        if(command!=SwShowNormal&&command!=SwShow&&command!=SwShowNa&&command!=SwRestore&&command!=SwShowDefault&&command!=SwShowNoActivate)
            unsupported("ShowWindow com comando "+std::to_string(command));
        if(!was) {
            send(window,WmShowWindow,1,0);
            std::lock_guard<std::mutex> lock(mutex);auto f=windows.find(window);if(f!=windows.end())f->second.style|=WsVisible;
        }
        if(command!=SwShowNa&&command!=SwShowNoActivate)activate(window);
        return was;
    }
    int64_t getLong(void* window,int index) {
        std::lock_guard<std::mutex> lock(mutex);
        auto found=windows.find(window);if(found==windows.end()){setLastError(ErrorInvalidWindowHandle);return 0;}
        auto const& w=found->second;
        switch(index) {
        case GwlStyle:return w.style;
        case GwlExStyle:return w.exStyle;
        case GwlpWndProc:return reinterpret_cast<int64_t>(w.proc);
        case GwlpUserData:return w.userData;
        case GwlpId:return w.id;
        case GwlpHInstance:return reinterpret_cast<int64_t>(w.instance);
        case GwlpHwndParent:return 0;
        }
        setLastError(ErrorInvalidIndex);return 0;
    }
    int64_t setLong(void* window,int index,int64_t value) {
        if(!exists(window)){setLastError(ErrorInvalidWindowHandle);return 0;}
        requireOwner(window,"SetWindowLongPtrW");
        if(index==GwlStyle||index==GwlExStyle) {
            if(index==GwlStyle&&(value&WsChild))unsupported("SetWindowLongPtrW tornando a janela filha");
            StyleStruct change{uint32_t(getLong(window,index)),uint32_t(value)};
            send(window,WmStyleChanging,uint64_t(int64_t(index)),reinterpret_cast<int64_t>(&change));
            {std::lock_guard<std::mutex> lock(mutex);auto& w=windows[window];(index==GwlStyle?w.style:w.exStyle)=int64_t(change.styleNew);}
            send(window,WmStyleChanged,uint64_t(int64_t(index)),reinterpret_cast<int64_t>(&change));
            return int64_t(change.styleOld);
        }
        if(index!=GwlpWndProc&&index!=GwlpUserData&&index!=GwlpId)unsupported("SetWindowLongPtrW com indice "+std::to_string(index));
        if(index==GwlpWndProc&&!value){setLastError(ErrorInvalidParameter);return 0;}
        std::lock_guard<std::mutex> lock(mutex);auto& w=windows[window];int64_t old;
        if(index==GwlpWndProc){old=reinterpret_cast<int64_t>(w.proc);w.proc=reinterpret_cast<void*>(value);}
        else if(index==GwlpUserData){old=w.userData;w.userData=value;}
        else{old=w.id;w.id=value;}
        return old;
    }
    bool setPosition(void* window,void* after,int x,int y,int cx,int cy,uint32_t flags) {
        constexpr uint32_t allowed=SwpNoSize|SwpNoMove|SwpNoZOrder|SwpNoRedraw|SwpNoActivate|SwpFrameChanged|SwpShowWindow|SwpHideWindow
                                  |SwpNoCopyBits|SwpNoOwnerZOrder|SwpNoSendChanging|SwpDeferErase|SwpAsyncWindowPos;
        if(flags&~allowed)unsupported("SetWindowPos com flags "+hex(flags));
        if(!(flags&SwpNoZOrder)&&after)unsupported("SetWindowPos com ordem Z "+hex(reinterpret_cast<uintptr_t>(after)));
        if(!exists(window)){setLastError(ErrorInvalidWindowHandle);return false;}
        requireOwner(window,"SetWindowPos");
        Rect now;{std::lock_guard<std::mutex> lock(mutex);now=windows[window].rect;}
        if(flags&SwpNoMove){x=now.left;y=now.top;}
        if(flags&SwpNoSize){cx=now.right-now.left;cy=now.bottom-now.top;}
        WindowPos position{window,after,x,y,cx,cy,flags};
        if(!(flags&SwpNoSendChanging))send(window,WmWindowPosChanging,0,reinterpret_cast<int64_t>(&position));
        if(flags&SwpFrameChanged){Rect client{position.x,position.y,position.x+position.cx,position.y+position.cy};send(window,WmNcCalcSize,0,reinterpret_cast<int64_t>(&client));}
        {
            std::lock_guard<std::mutex> lock(mutex);auto found=windows.find(window);if(found==windows.end())return true;
            auto& w=found->second;
            if(!(position.flags&SwpNoMove)||!(position.flags&SwpNoSize))w.rect={position.x,position.y,position.x+position.cx,position.y+position.cy};
            if(flags&SwpShowWindow)w.style|=WsVisible;
            if(flags&SwpHideWindow)w.style&=~int64_t(WsVisible);
        }
        send(window,WmWindowPosChanged,0,reinterpret_cast<int64_t>(&position));
        if((flags&SwpHideWindow)&&snapshot(active)==window)changeActive(nullptr);
        else if(!(flags&SwpNoActivate)&&visible(window))activate(window);
        return true;
    }
};
Windows& W(){static Windows windows;return windows;}

// ---- Classes and windows -------------------------------------------------
int64_t defaultProc(void* window,uint32_t message,uint64_t w,int64_t l) {
    switch(message) {
    case WmNcCreate:case WmQueryEndSession:return 1;
    case WmClose:W().destroy(window);return 0;
    case WmSysCommand:if((w&0xfff0)==0xf060)W().send(window,WmClose,0,0);return 0;   // SC_CLOSE
    case WmSysKeyDown:if(w==0x73)W().send(window,WmSysCommand,0xf060,0);return 0;    // Alt+F4
    case WmActivate:if((w&0xffff)!=0)W().focus(window);return 0;
    case WmWindowPosChanged: {
        auto p=reinterpret_cast<const WindowPos*>(l);
        if(!(p->flags&SwpNoMove))W().send(window,WmMove,0,makeLong(p->x,p->y));
        if(!(p->flags&SwpNoSize))W().send(window,WmSize,0,makeLong(p->cx,p->cy));
        return 0;
    }
    default:return 0;
    }
}
}

namespace azimuth::win32 {
bool windowClientSize(void* window,int& width,int& height) {
    auto& w=W();std::lock_guard<std::mutex> lock(w.mutex);
    auto found=w.windows.find(window);if(found==w.windows.end())return false;
    width=found->second.rect.right-found->second.rect.left;height=found->second.rect.bottom-found->second.rect.top;
    return true;
}
}

extern "C" uint16_t win_RegisterClassExW(const void* argument) {
    auto wc=static_cast<const WndClassEx*>(argument);
    if(!wc||wc->size!=sizeof(WndClassEx)||!wc->proc||!wc->className){setLastError(ErrorInvalidParameter);return 0;}
    if(isAtom(wc->className))unsupported("RegisterClassExW com nome de classe por atom");
    if(wc->classExtra||wc->windowExtra||wc->menuName)unsupported("RegisterClassExW com bytes extras ou menu");
    auto& w=W();std::lock_guard<std::mutex> lock(w.mutex);
    auto name=key(wc->className);
    if(w.classes.count(name)){setLastError(ErrorClassAlreadyExists);return 0;}
    auto atom=w.nextAtom++;w.classes[name]={wc->proc,wc->instance,wc->style,atom};
    log("Classe de janela registrada: "+text(wc->className));
    return atom;
}
extern "C" void* win_CreateWindowExW(uint32_t exStyle,const uint16_t* className,const uint16_t* windowName,uint32_t style,int32_t x,int32_t y,
                                     int32_t width,int32_t height,void* parent,void* menu,void* instance,void* parameter) {
    if(parent||menu||(style&WsChild))unsupported("CreateWindowExW de janela filha, com dono ou com menu");
    if(!className){setLastError(ErrorInvalidParameter);return nullptr;}
    auto& w=W();void* handle;Rect rect;
    {
        std::lock_guard<std::mutex> lock(w.mutex);
        const Windows::Class* cls=nullptr;
        for(auto const& [name,c]:w.classes)
            if(isAtom(className)?c.atom==uint16_t(reinterpret_cast<uintptr_t>(className)):name==key(className))cls=&c;
        if(!cls){setLastError(ErrorCannotFindWndClass);return nullptr;}
        if(x==int32_t(0x80000000))x=y=0;   // CW_USEDEFAULT
        if(width==int32_t(0x80000000)){width=DisplayWidth;height=DisplayHeight;}
        rect={x,y,x+width,y+height};
        handle=reinterpret_cast<void*>(w.nextWindow);w.nextWindow+=2;
        w.windows[handle]={self(),cls->proc,cls->atom,instance,int64_t(style&~WsVisible),int64_t(exStyle),0,0,rect,false};
        w.queue(self());
    }
    log("Janela do motor criada: "+hex(reinterpret_cast<uintptr_t>(handle))+" \""+text(windowName)+"\" "+
        std::to_string(width)+"x"+std::to_string(height)+" em "+std::to_string(x)+","+std::to_string(y)+" estilo="+hex(style));
    CreateStruct cs{parameter,instance,menu,parent,height,width,y,x,int32_t(style),windowName,className,exStyle};
    MinMaxInfo limits{};w.send(handle,WmGetMinMaxInfo,0,reinterpret_cast<int64_t>(&limits));
    if(!w.exists(handle)||!w.send(handle,WmNcCreate,0,reinterpret_cast<int64_t>(&cs))) {
        if(w.exists(handle)){w.send(handle,WmNcDestroy,0,0);std::lock_guard<std::mutex> lock(w.mutex);w.windows.erase(handle);}
        return nullptr;
    }
    Rect client=rect;w.send(handle,WmNcCalcSize,0,reinterpret_cast<int64_t>(&client));
    if(!w.exists(handle)||w.send(handle,WmCreate,0,reinterpret_cast<int64_t>(&cs))==-1){w.destroy(handle);return nullptr;}
    // CreateWindowEx ends with the initial size and position notifications.
    w.send(handle,WmSize,0,makeLong(width,height));
    w.send(handle,WmMove,0,makeLong(x,y));
    if(w.exists(handle)&&(style&WsVisible))w.show(handle,SwShow);
    return w.exists(handle)?handle:nullptr;
}
extern "C" int32_t win_DestroyWindow(void* window){return W().destroy(window);}
extern "C" int64_t win_DefWindowProcW(void* window,uint32_t message,uint64_t w,int64_t l){return defaultProc(window,message,w,l);}

// ---- Messages ------------------------------------------------------------
extern "C" int32_t win_PostMessageW(void* window,uint32_t message,uint64_t wp,int64_t lp) {
    if(reinterpret_cast<uintptr_t>(window)==0xffff)unsupported("PostMessageW para HWND_BROADCAST");
    auto& w=W();std::lock_guard<std::mutex> lock(w.mutex);
    uint32_t thread=self();
    if(window) {
        auto found=w.windows.find(window);if(found==w.windows.end()){setLastError(ErrorInvalidWindowHandle);return 0;}
        thread=found->second.thread;
    }
    w.deliver(thread,{window,message,wp,lp,tick(),w.cursorPos,0},false);return 1;
}
namespace {
bool peek(Msg* msg,void* window,uint32_t first,uint32_t last,uint32_t remove,bool wait) {
    if(!msg){setLastError(ErrorInvalidParameter);return false;}
    if(remove&~(PmRemove|PmNoYield))unsupported("PeekMessageW com filtro PM_QS "+hex(remove));
    auto& w=W();
    void* const thisThread=reinterpret_cast<void*>(intptr_t(-1));
    if(window&&window!=thisThread&&!w.exists(window)){setLastError(ErrorInvalidWindowHandle);return false;}
    auto matches=[&](void* h,uint32_t m){
        bool target=!window||h==window||(window==thisThread&&!h);
        return target&&((!first&&!last)||(m>=first&&m<=last));
    };
    for(;;) {
        // Sent messages are delivered here, before anything is returned.
        for(;;) {
            Msg sent;
            {
                std::lock_guard<std::mutex> lock(w.mutex);auto& q=w.queue(self());setHostEvent(q.signal,false);
                if(q.sent.empty())break;
                sent=q.sent.front();q.sent.pop_front();
            }
            if(sent.message==HostActivate)w.changeActive(sent.w?sent.window:nullptr);
            else if(w.exists(sent.window))w.send(sent.window,sent.message,sent.w,sent.l);
        }
        void* signal;uint32_t timeout=Infinite;
        {
            std::lock_guard<std::mutex> lock(w.mutex);auto& q=w.queue(self());
            for(auto m=q.posted.begin();m!=q.posted.end();++m)if(matches(m->window,m->message)) {
                *msg=*m;if(remove&PmRemove)q.posted.erase(m);
                return true;
            }
            // WM_TIMER is generated only when nothing else is queued.
            auto now=win_GetTickCount64();
            for(auto& t:w.timers)if(t.thread==self()&&matches(t.window,WmTimer)) {
                if(t.due<=now) {
                    *msg={t.window,WmTimer,t.id,reinterpret_cast<int64_t>(t.proc),uint32_t(now),w.cursorPos,0};
                    if(remove&PmRemove)t.due=now+t.elapse;
                    return true;
                }
                timeout=uint32_t(std::min<uint64_t>(timeout,t.due-now));
            }
            if(!wait)return false;
            signal=q.signal;
        }
        win_WaitForSingleObject(signal,timeout);
    }
}
}
extern "C" int32_t win_PeekMessageW(void* msg,void* window,uint32_t first,uint32_t last,uint32_t remove) {
    return peek(static_cast<Msg*>(msg),window,first,last,remove,false);
}
extern "C" int32_t win_GetMessageW(void* msg,void* window,uint32_t first,uint32_t last) {
    if(window&&window!=reinterpret_cast<void*>(intptr_t(-1))&&!W().exists(window)){setLastError(ErrorInvalidWindowHandle);return -1;}
    if(!peek(static_cast<Msg*>(msg),window,first,last,PmRemove,true))return -1;
    return static_cast<Msg*>(msg)->message!=WmQuit;
}
// No keyboard reaches the engine yet, so there is no WM_CHAR to generate.
extern "C" int32_t win_TranslateMessage(const void* argument) {
    auto msg=static_cast<const Msg*>(argument);
    return msg&&(msg->message==WmKeyDown||msg->message==WmKeyUp||msg->message==WmSysKeyDown||msg->message==WmSysKeyUp);
}
extern "C" int64_t win_DispatchMessageW(const void* argument) {
    auto msg=static_cast<const Msg*>(argument);
    if(!msg){setLastError(ErrorInvalidParameter);return 0;}
    if(msg->message==WmTimer&&msg->l) {
        guestCallTimerProc(reinterpret_cast<void*>(msg->l),msg->window,WmTimer,msg->w,tick());
        return 0;
    }
    if(!msg->window)return 0;
    if(msg->message==0x100||msg->message==0x101) {
        static std::atomic<unsigned> logged{0};
        if(logged++<20)log("Tecla entregue ao motor: mensagem 0x"+hex(msg->message)+" vk 0x"+hex(msg->w)+" thread "+std::to_string(self()));
    }
    return W().send(msg->window,msg->message,msg->w,msg->l);
}
extern "C" uint32_t win_MsgWaitForMultipleObjectsEx(uint32_t count,void* const* handles,uint32_t milliseconds,uint32_t mask,uint32_t flags) {
    constexpr uint32_t needed=QsKey|QsPostMessage|QsTimer|QsSendMessage;
    if((mask&needed)!=needed)unsupported("MsgWaitForMultipleObjects com mascara "+hex(mask));
    if(flags&~(MwmoAlertable|MwmoInputAvailable))unsupported("MsgWaitForMultipleObjectsEx com flags "+hex(flags));
    if(count>=64||(count&&!handles)){setLastError(ErrorInvalidParameter);return WaitFailed;}
    auto& w=W();auto start=win_GetTickCount64();
    for(;;) {
        void* all[64];std::copy(handles,handles+count,all);
        uint64_t limit=Infinite;
        {
            std::lock_guard<std::mutex> lock(w.mutex);auto& q=w.queue(self());
            // Without MWMO_INPUTAVAILABLE only input queued since the last
            // Peek/GetMessage counts; those calls reset the signal.
            bool ready=(flags&MwmoInputAvailable)?(!q.sent.empty()||!q.posted.empty()):hostEventSet(q.signal);
            auto now=win_GetTickCount64();
            for(auto const& t:w.timers)if(t.thread==self()){if(t.due<=now)ready=true;else limit=std::min<uint64_t>(limit,t.due-now);}
            if(ready)return WaitObject0+count;
            all[count]=q.signal;
            if(milliseconds!=Infinite) {
                auto elapsed=now-start;if(elapsed>=milliseconds)return WaitTimeout;
                limit=std::min<uint64_t>(limit,milliseconds-elapsed);
            }
        }
        auto result=win_WaitForMultipleObjects(count+1,all,0,uint32_t(limit));
        if(result!=WaitTimeout)return result;
    }
}
extern "C" uint32_t win_MsgWaitForMultipleObjects(uint32_t count,void* const* handles,int32_t waitAll,uint32_t milliseconds,uint32_t mask) {
    if(waitAll)unsupported("MsgWaitForMultipleObjects com bWaitAll");
    return win_MsgWaitForMultipleObjectsEx(count,handles,milliseconds,mask,0);
}

// ---- Timers ----------------------------------------------------------------
extern "C" uint64_t win_SetTimer(void* window,uint64_t id,uint32_t elapse,void* proc) {
    if(window&&!id)unsupported("SetTimer de janela com id 0");
    auto& w=W();std::lock_guard<std::mutex> lock(w.mutex);
    if(window) {
        auto found=w.windows.find(window);if(found==w.windows.end()){setLastError(ErrorInvalidWindowHandle);return 0;}
        if(found->second.thread!=self()){setLastError(ErrorAccessDenied);return 0;}
    }
    elapse=std::clamp(elapse,UserTimerMinimum,UserTimerMaximum);
    auto now=win_GetTickCount64();
    for(auto& t:w.timers)if(t.window==window&&t.id==id&&(window||t.thread==self())) {
        t.elapse=elapse;t.due=now+elapse;t.proc=proc;return id;
    }
    if(!window)id=w.nextTimer++;
    w.timers.push_back({window,id,self(),elapse,now+elapse,proc});return id;
}
extern "C" int32_t win_KillTimer(void* window,uint64_t id) {
    auto& w=W();std::lock_guard<std::mutex> lock(w.mutex);
    auto found=std::find_if(w.timers.begin(),w.timers.end(),[&](Windows::Timer const& t){return t.window==window&&t.id==id&&(window||t.thread==self());});
    if(found==w.timers.end()){setLastError(ErrorInvalidParameter);return 0;}
    w.timers.erase(found);return 1;
}

// ---- Window state ----------------------------------------------------------
extern "C" int32_t win_ShowWindow(void* window,int32_t command){return W().show(window,command);}
extern "C" int32_t win_SetForegroundWindow(void* window){return W().activate(window);}
extern "C" void* win_GetForegroundWindow(void){return W().snapshot(W().active);}
extern "C" void* win_SetFocus(void* window){return W().focus(window);}
extern "C" int32_t win_EnableWindow(void* window,int32_t enable) {
    auto& w=W();
    if(!w.exists(window)){setLastError(ErrorInvalidWindowHandle);return 0;}
    w.requireOwner(window,"EnableWindow");
    bool wasDisabled=!w.enabled(window);
    if(wasDisabled==!enable)return wasDisabled;
    {std::lock_guard<std::mutex> lock(w.mutex);auto& s=w.windows[window].style;s=enable?(s&~int64_t(WsDisabled)):(s|WsDisabled);}
    if(!enable&&w.snapshot(w.focused)==window)w.focus(nullptr);
    w.send(window,WmEnable,uint64_t(enable!=0),0);
    return wasDisabled;
}
extern "C" int32_t win_IsWindowEnabled(void* window){return W().enabled(window);}
extern "C" int32_t win_IsWindow(void* window){return W().exists(window);}
extern "C" int32_t win_IsWindowVisible(void* window){return W().visible(window);}
extern "C" void* win_GetWindow(void* window,uint32_t command) {
    if(!W().exists(window)){setLastError(ErrorInvalidWindowHandle);return nullptr;}
    // Only top-level windows exist: none has an owner, popups or children.
    if(command==4||command==5||command==6)return nullptr;   // GW_OWNER, GW_CHILD, GW_ENABLEDPOPUP
    unsupported("GetWindow com comando "+std::to_string(command));
}
extern "C" uint32_t win_GetWindowThreadProcessId(void* window,uint32_t* process) {
    auto& w=W();std::lock_guard<std::mutex> lock(w.mutex);auto found=w.windows.find(window);
    if(found==w.windows.end()){setLastError(ErrorInvalidWindowHandle);return 0;}
    if(process)*process=win_GetCurrentProcessId();
    return found->second.thread;
}
extern "C" int64_t win_GetWindowLongPtrW(void* window,int32_t index){return W().getLong(window,index);}
extern "C" int64_t win_SetWindowLongPtrW(void* window,int32_t index,int64_t value){return W().setLong(window,index,value);}
extern "C" int32_t win_SetWindowPos(void* window,void* after,int32_t x,int32_t y,int32_t cx,int32_t cy,uint32_t flags) {
    return W().setPosition(window,after,x,y,cx,cy,flags);
}
extern "C" int32_t win_GetWindowPlacement(void* window,void* argument) {
    auto placement=static_cast<WindowPlacement*>(argument);
    if(!placement){setLastError(ErrorInvalidParameter);return 0;}
    auto& w=W();std::lock_guard<std::mutex> lock(w.mutex);
    auto found=w.windows.find(window);if(found==w.windows.end()){setLastError(ErrorInvalidWindowHandle);return 0;}
    // USER32 fills length itself here; only SetWindowPlacement checks it.
    *placement={sizeof(WindowPlacement),0,uint32_t(SwShowNormal),{-1,-1},{-1,-1},found->second.rect};
    return 1;
}
extern "C" int32_t win_SetWindowPlacement(void* window,const void* argument) {
    auto placement=static_cast<const WindowPlacement*>(argument);
    if(!placement||placement->length!=sizeof(WindowPlacement)){setLastError(ErrorInvalidParameter);return 0;}
    auto& w=W();
    if(!w.exists(window)){setLastError(ErrorInvalidWindowHandle);return 0;}
    int command=int(placement->showCmd);
    if(command!=SwShowNormal&&command!=SwShow&&command!=SwRestore&&command!=SwHide&&command!=SwShowNa&&command!=SwShowNoActivate)
        unsupported("SetWindowPlacement com showCmd "+std::to_string(command));
    auto const& r=placement->normalPosition;
    w.setPosition(window,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SwpNoZOrder|SwpNoActivate);
    w.show(window,command);
    return 1;
}
extern "C" int32_t win_GetClientRect(void* window,int32_t* rect) {
    if(!rect){setLastError(ErrorInvalidParameter);return 0;}
    int width,height;if(!windowClientSize(window,width,height)){setLastError(ErrorInvalidWindowHandle);return 0;}
    rect[0]=0;rect[1]=0;rect[2]=width;rect[3]=height;return 1;
}
// No frame: the client area is the whole window.
extern "C" int32_t win_AdjustWindowRectEx(int32_t* rect,uint32_t,int32_t menu,uint32_t) {
    if(!rect){setLastError(ErrorInvalidParameter);return 0;}
    if(menu)unsupported("AdjustWindowRectEx com menu");
    return 1;
}

// ---- Display, cursor, keyboard -----------------------------------------------
extern "C" int32_t win_GetSystemMetrics(int32_t index) {
    switch(index) {
    case 0:return DisplayWidth;    // SM_CXSCREEN
    case 1:return DisplayHeight;   // SM_CYSCREEN
    // The standard 96-DPI icon sizes; the engine only uses them to load icons.
    case 11:case 12:return 32;     // SM_CXICON, SM_CYICON
    case 13:case 14:return 32;     // SM_CXCURSOR, SM_CYCURSOR (the 1.0.2 engine, title 1.46)
    case 49:case 50:return 16;     // SM_CXSMICON, SM_CYSMICON
    }
    unsupported("GetSystemMetrics("+std::to_string(index)+")");
}
extern "C" int32_t win_SystemParametersInfoW(uint32_t action,uint32_t,void* value,uint32_t) {
    if(action!=0x30)unsupported("SystemParametersInfoW("+hex(action)+")");   // SPI_GETWORKAREA
    if(!value){setLastError(ErrorInvalidParameter);return 0;}
    *static_cast<Rect*>(value)={0,0,DisplayWidth,DisplayHeight};return 1;
}
extern "C" void* win_MonitorFromWindow(void*,uint32_t){return Monitor;}
extern "C" int32_t win_GetMonitorInfoW(void* monitor,void* argument) {
    auto info=static_cast<MonitorInfo*>(argument);
    if(monitor!=Monitor){setLastError(ErrorInvalidMonitorHandle);return 0;}
    if(!info||(info->size!=40&&info->size!=104)){setLastError(ErrorInvalidParameter);return 0;}   // MONITORINFO, MONITORINFOEXW
    info->monitor=info->work={0,0,DisplayWidth,DisplayHeight};info->flags=1;   // MONITORINFOF_PRIMARY
    if(info->size==104) {
        static const char16_t device[]=u"\\\\.\\DISPLAY1";
        std::memcpy(reinterpret_cast<uint8_t*>(info)+40,device,sizeof device);
    }
    return 1;
}
extern "C" int32_t win_GetCursorPos(int32_t* point) {
    if(!point){setLastError(ErrorInvalidParameter);return 0;}
    auto& w=W();std::lock_guard<std::mutex> lock(w.mutex);point[0]=w.cursorPos.x;point[1]=w.cursorPos.y;return 1;
}
extern "C" void* win_SetCursor(void* cursor){auto& w=W();std::lock_guard<std::mutex> lock(w.mutex);std::swap(cursor,w.cursor);return cursor;}
// System cursors are handles of the virtual window system; nothing is drawn.
extern "C" void* win_LoadCursorW(void* instance,const uint16_t* name) {
    auto id=reinterpret_cast<uintptr_t>(name);
    if(instance||!isAtom(name)||id<32512||id>32651)unsupported("LoadCursorW de recurso proprio");
    return reinterpret_cast<void*>(0xcc0000|id);
}
extern "C" void* win_LoadImageW(void*,const uint16_t*,uint32_t type,int32_t,int32_t,uint32_t) {
    if(type!=1)unsupported("LoadImageW do tipo "+std::to_string(type));   // IMAGE_ICON
    // An icon is optional for RegisterClassExW; the engine tolerates none.
    log("LoadImageW: icone do motor nao carregado; janela virtual sem icone");
    setLastError(ErrorResourceTypeNotFound);return nullptr;
}
// No key is ever down: no input reaches the engine yet.
namespace {
std::atomic<uint8_t> keyDown[256];
}
// The pad thread's keys (Input.cpp): a message to the active (else the first
// visible) window, and the state GetKeyState reads.
namespace azimuth::win32 {
void postKey(uint32_t vk,uint32_t scan,bool down) {
    if(vk>=256)return;
    keyDown[vk]=down;
    auto& w=W();std::lock_guard<std::mutex> lock(w.mutex);
    void* target=w.windows.count(w.active)?w.active:nullptr;
    if(!target)for(auto& entry:w.windows)if(entry.second.style&WsVisible){target=entry.first;break;}
    static unsigned logged=0;
    if(logged<40){++logged;char text[128];std::snprintf(text,sizeof text,"Tecla do controle: vk 0x%x %s -> janela %p",vk,down?"desce":"sobe",target);log(text);}
    if(!target)return;
    int64_t lp=1|(int64_t(scan&0xff)<<16);
    if(!down)lp|=(int64_t(1)<<30)|(int64_t(1)<<31);
    w.deliver(w.windows[target].thread,{target,down?0x100u:0x101u,vk,lp,tick(),w.cursorPos,0},false);
}
}
extern "C" int16_t win_GetKeyState(int32_t vk){return vk>=0&&vk<256&&keyDown[vk]?int16_t(0x8000):0;}
extern "C" int16_t win_GetAsyncKeyState(int32_t vk){return vk>=0&&vk<256&&keyDown[vk]?int16_t(0x8000):0;}
extern "C" uint32_t win_CharUpperBuffW(uint16_t* buffer,uint32_t count) {
    if(!buffer||!count)return 0;
    std::vector<uint16_t> copy(buffer,buffer+count);
    constexpr uint32_t Uppercase=0x200;
    if(!win_LCMapStringW(0x409,Uppercase,copy.data(),int32_t(count),buffer,int32_t(count)))return 0;
    return count;
}
// MB_OK only: the message is logged and shown as a console notification, and
// the box counts as dismissed. A box that asks a question is a stop.
extern "C" int32_t win_MessageBoxW(void*,const uint16_t* message,const uint16_t* caption,uint32_t type) {
    auto line="Mensagem do motor ["+text(caption)+"]: "+text(message);
    log(line);
    log("  alocador: "+std::to_string(azimuth::heapFallbacks())+" blocos que o malloc do sistema recusou foram servidos por mmap");
    log("  ultimas chamadas ao host, por thread:"+azimuth::recentHostCallsAllThreads(40));
    if(type&0xf)unsupported("MessageBoxW com botoes "+hex(type&0xf)+" ("+line+")");
    azimuth::platform::notify(line);
    return 1;   // IDOK
}

extern "C" int32_t win_GetWindowRect(void* window,int32_t* rect){
    if(!rect){setLastError(ErrorInvalidParameter);return 0;}
    auto& w=W();std::lock_guard<std::mutex> lock(w.mutex);auto it=w.windows.find(window);
    if(it==w.windows.end()){setLastError(ErrorInvalidWindowHandle);return 0;}
    std::memcpy(rect,&it->second.rect,16);return 1;
}
extern "C" int32_t win_ScreenToClient(void* window,int32_t* point){
    int32_t rect[4];if(!point||!win_GetWindowRect(window,rect))return 0;
    point[0]-=rect[0];point[1]-=rect[1];return 1;
}
extern "C" void* win_MonitorFromPoint(uint64_t point,uint32_t flags){
    int32_t x=int32_t(point),y=int32_t(point>>32);
    return (flags|| (x>=0&&y>=0&&x<DisplayWidth&&y<DisplayHeight))?Monitor:nullptr;
}
extern "C" int32_t win_PostThreadMessageW(uint32_t thread,uint32_t message,uint64_t wp,int64_t lp){
    auto& w=W();std::lock_guard<std::mutex> lock(w.mutex);
    if(!w.queues.count(thread)){setLastError(1444);return 0;}
    w.deliver(thread,{nullptr,message,wp,lp,tick(),w.cursorPos,0},false);return 1;
}

extern "C" void* win_WindowFromPoint(uint64_t point){
    int32_t x=int32_t(point),y=int32_t(point>>32);auto& w=W();std::lock_guard<std::mutex> lock(w.mutex);
    auto matches=[&](const Windows::Window& window){return !window.destroying&&(window.style&WsVisible)&&!(window.style&WsDisabled)&&x>=window.rect.left&&x<window.rect.right&&y>=window.rect.top&&y<window.rect.bottom;};
    auto active=w.windows.find(w.active);if(active!=w.windows.end()&&matches(active->second))return active->first;
    for(auto it=w.windows.rbegin();it!=w.windows.rend();++it)if(matches(it->second))return it->first;
    return nullptr;
}
