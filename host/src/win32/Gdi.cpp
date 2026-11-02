// Software GDI for the engine's launcher, ported from AzimuthUwp's GuestGdi:
// memory DCs with 32-bit bitmaps and DIB sections, window DCs that draw into
// the window's client area, the copies and blends the launcher uses, and
// single-line text through gfx::Font (TrueType files the user copies to the
// console, see fontDirectories). The last picture copied to a window is what
// the host saves as an image.
#include "Win32.h"
#include "../GuestThreads.h"
#include <atomic>
#include "../Platform.h"
#include "../gfx/Font.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <dirent.h>
#include <strings.h>
#include <cmath>
#include <map>
#include <mutex>
#include <sstream>
#include <unistd.h>

#define EXPORT(dll,name,ret,params,args) extern "C" ret win_##name params;
#include "exports.inc"
#undef EXPORT

using namespace azimuth::win32;

namespace {
void* const StockBitmap=reinterpret_cast<void*>(uintptr_t(0x0c000004));
void* const StockFont=reinterpret_cast<void*>(uintptr_t(0x0c000008));
struct Rect {int32_t left,top,right,bottom;};
struct BitmapInfoHeader {uint32_t size;int32_t width,height;uint16_t planes,bitCount;uint32_t compression,sizeImage;int32_t xPels,yPels;uint32_t used,important;};
static_assert(sizeof(BitmapInfoHeader)==40);
enum : int {Transparent=1,Opaque=2,BlackOnWhite=1,Halftone=4};
enum : uint32_t {SrcCopy=0x00cc0020,EtoOpaque=0x2,EtoClipped=0x4,ClrInvalid=0xffffffff};
enum : uint32_t {DtCenter=0x1,DtRight=0x2,DtVCenter=0x4,DtBottom=0x8,DtSingleLine=0x20,DtNoClip=0x100,DtNoPrefix=0x800,DtPathEllipsis=0x4000,DtEndEllipsis=0x8000};
enum : int {RegionError=0,NullRegion=1,SimpleRegion=2};

std::string hex(uint64_t value){std::ostringstream s;s<<"0x"<<std::hex<<value;return s.str();}
// COLORREF is 0x00BBGGRR; a 32-bit DIB pixel is B,G,R,X in memory.
uint32_t pixel(uint32_t c){return ((c&0xff)<<16)|(c&0xff00)|((c>>16)&0xff);}
Rect intersect(Rect a,Rect b){return {std::max(a.left,b.left),std::max(a.top,b.top),std::min(a.right,b.right),std::min(a.bottom,b.bottom)};}
bool empty(Rect r){return r.right<=r.left||r.bottom<=r.top;}
uint32_t mix(uint32_t d,uint32_t c,float coverage) {
    uint32_t out=d&0xff000000;
    for(int shift=0;shift<24;shift+=8) {
        int dv=(d>>shift)&0xff,cv=(c>>shift)&0xff;
        out|=uint32_t(dv+int(std::lround((cv-dv)*coverage)))<<shift;
    }
    return out;
}
struct Weight {int index;float weight;};
// Separable tent filter: bilinear when enlarging, an area-like average when
// reducing. Indices are clamped to the source rectangle.
std::vector<Weight> taps(int start,int length,int outLength,int i) {
    float scale=float(length)/float(outLength),center=start+(i+0.5f)*scale-0.5f,radius=std::max(1.f,scale);
    std::vector<Weight> w;float total=0;
    for(int j=int(std::ceil(center-radius));j<=int(std::floor(center+radius));++j) {
        float v=1.f-std::fabs(j-center)/radius;if(v<=0)continue;
        w.push_back({std::clamp(j,start,start+length-1),v});total+=v;
    }
    if(w.empty())w.push_back({std::clamp(int(center+0.5f),start,start+length-1),1});else for(auto& e:w)e.weight/=total;
    return w;
}

struct Bitmap {
    int width=0,height=0;bool bottomUp=false;std::vector<uint32_t> pixels;void* selectedIn=nullptr;
    uint32_t* row(int y){return pixels.data()+size_t(bottomUp?height-1-y:y)*width;}
    Rect bounds()const{return {0,0,width,height};}
};
struct Font {int height;int weight;std::u16string face;std::shared_ptr<azimuth::gfx::Font> font;azimuth::gfx::Font::Size size;};
struct DcState {void* bitmap=StockBitmap;void* font=StockFont;uint32_t text=0,background=0xffffff;int mode=Opaque,stretch=BlackOnWhite;int32_t origin[2]{};bool clipped=false;Rect clip{};};
enum class Kind {Memory,Window,Screen};
struct Dc {Kind kind;void* window;DcState s;std::vector<DcState> saved;};

std::mutex mutex;
std::map<void*,Bitmap> bitmaps;
std::map<void*,Font> fonts;
std::map<void*,uint32_t> brushes;
std::map<void*,std::pair<Bitmap,Bitmap>> icons;
std::map<void*,Dc> dcs;
std::map<void*,Bitmap> surfaces;
std::map<std::string,std::shared_ptr<azimuth::gfx::Font>> fontFiles;
uintptr_t next=0x0c000010;
// The window whose surface last received a complete picture (BitBlt or
// StretchDIBits to its DC), and how many pictures there have been.
void* shownWindow=nullptr;uint64_t shownSerial=0;

// Every GDI call holds the mutex; a stop releases it first, so the host's
// frame saving and the other engine threads on their way out are not blocked.
thread_local std::unique_lock<std::mutex>* held=nullptr;
// Who holds the lock (an engine thread id, or "host"), for the run's reports.
std::atomic<uint32_t> owner{0};
struct Lock {
    std::unique_lock<std::mutex> lock{mutex};
    Lock(){held=&lock;owner=azimuth::currentGuestThreadId()?azimuth::currentGuestThreadId():1;}
    ~Lock(){owner=0;held=nullptr;}
};
[[noreturn]] void unsupported(const std::string& what) {
    if(held&&held->owns_lock())held->unlock();
    held=nullptr;
    stopGuest("GDI ainda sem adaptacao para este uso: "+what);
}
template<class T=void*>T handle(){next+=4;return reinterpret_cast<T>(next);}
Dc* dc(void* h){auto f=dcs.find(h);return f==dcs.end()?nullptr:&f->second;}
Bitmap* surface(void* window) {
    int w,h;if(!windowClientSize(window,w,h)||w<=0||h<=0)return nullptr;
    auto& s=surfaces[window];
    if(s.width!=w||s.height!=h)s={w,h,false,std::vector<uint32_t>(size_t(w)*h,0),nullptr};
    return &s;
}
// The bitmap a DC draws into.
Bitmap* target(Dc& d,const char* api) {
    if(d.kind==Kind::Window)return surface(d.window);
    if(d.kind==Kind::Screen)unsupported(std::string(api)+" desenhando direto na tela");
    if(d.s.bitmap==StockBitmap)unsupported(std::string(api)+" em DC de memoria sem bitmap selecionado");
    return &bitmaps.at(d.s.bitmap);
}
Rect drawable(Dc& d,Bitmap& b){return d.s.clipped?intersect(d.s.clip,b.bounds()):b.bounds();}
void shown(Dc& d){if(d.kind==Kind::Window){shownWindow=d.window;++shownSerial;}}

// ---- Fonts -------------------------------------------------------------
// TrueType files are looked for in REPORT_DIR/fonts (/data/azimuth-ps5/fonts
// on the console), then azimuth-ps5/fonts at the root of the engine's
// drive, then the PC's Windows fonts (the Linux build under WSL).
std::vector<std::string> fontDirectories() {
    std::vector<std::string> dirs{azimuth::platform::reportDir()+"/fonts"};
    for(auto const& m:process().mounts)if(m.guest==u"E:")dirs.push_back(m.host+"/azimuth-ps5/fonts");
    dirs.push_back("/mnt/c/Windows/Fonts");
    return dirs;
}
// Looked for once per file name, found or not: the launcher creates its fonts
// again every frame, and each look was a few opens, some on the USB drive
// where vkd3d-proton writes its pipeline cache while the launcher compiles
// (title 0.51: the picture froze behind those writes).
std::shared_ptr<azimuth::gfx::Font> loadFontFile(const std::string& file) {
    if(auto found=fontFiles.find(file);found!=fontFiles.end())return found->second;
    std::shared_ptr<azimuth::gfx::Font> font;
    for(auto const& dir:fontDirectories()) {
        auto path=dir+"/"+file;
        // Any case (title 1.25: the internal disk tells ARIAL.TTF from
        // arial.ttf, the USB drive did not): the folder's own spelling.
        if(azimuth::platform::readable(path)!=0) {
            bool found=false;
            if(DIR* listing=opendir(dir.c_str())) {
                while(dirent* item=readdir(listing))
                    if(strcasecmp(item->d_name,file.c_str())==0){path=dir+"/"+item->d_name;found=true;break;}
                closedir(listing);
            }
            if(!found||azimuth::platform::readable(path)!=0)continue;
        }
        font=azimuth::gfx::Font::load(path);
        if(!font){log("Fonte invalida ignorada: "+path);continue;}
        log("Fonte carregada: "+path);
        break;
    }
    return fontFiles[file]=font;
}
std::string lower(const std::u16string& face) {
    std::string s;for(auto c:face)s.push_back(char(c<128?std::tolower(int(c)):'?'));
    return s;
}
std::shared_ptr<azimuth::gfx::Font> faceFile(const std::string& face,bool bold) {
    struct Face {const char* name;const char* regular;const char* bold;};
    static const Face faces[]={
        {"arial","arial.ttf","arialbd.ttf"},{"segoe ui","segoeui.ttf","segoeuib.ttf"},{"tahoma","tahoma.ttf","tahomabd.ttf"},
        {"verdana","verdana.ttf","verdanab.ttf"},{"calibri","calibri.ttf","calibrib.ttf"},
    };
    for(auto const& f:faces)if(face==f.name)return loadFontFile(bold?f.bold:f.regular);
    return nullptr;
}
Font& font(Dc& d,const char* api) {
    if(d.s.font==StockFont)unsupported(std::string(api)+" com a fonte padrao do DC");
    auto& f=fonts.at(d.s.font);
    if(!f.font) {
        bool bold=f.weight>=600;auto face=lower(f.face);
        f.font=faceFile(face,bold);
        if(!f.font&&face!="arial") {
            f.font=faceFile("arial",bold);
            if(f.font)log("Fonte "+face+" ausente; texto do motor usa Arial");
        }
        if(!f.font) {
            std::string dirs;for(auto const& dir:fontDirectories())dirs+=(dirs.empty()?"":", ")+dir;
            unsupported("fonte "+face+(bold?" negrito":"")+" e Arial ausentes: copie arial.ttf e arialbd.ttf de C:\\Windows\\Fonts para "+dirs);
        }
        f.size=f.font->size(f.height);
    }
    return f;
}
int textWidth(Font& f,const std::u16string& text){int w=0;for(int a:f.font->advances(text,f.size))w+=a;return w;}
// GDI's ellipses: the end of the text, or (DT_PATH_ELLIPSIS) the middle of
// a path, keeping its last component.
std::u16string ellipsize(Font& f,const std::u16string& text,int room,bool path) {
    if(textWidth(f,text)<=room)return text;
    const std::u16string dots=u"...";
    std::u16string head=text,tail;
    if(path){auto cut=text.find_last_of(u'\\');if(cut!=std::u16string::npos&&cut>0){head=text.substr(0,cut);tail=text.substr(cut);}}
    for(size_t n=head.size();;--n) {
        auto candidate=head.substr(0,n)+dots+tail;
        if(textWidth(f,candidate)<=room||n==0) {
            if(n==0&&!tail.empty()&&textWidth(f,candidate)>room)return ellipsize(f,text,room,false);
            return candidate;
        }
    }
}
}

namespace azimuth::win32 {
std::string gdiOwner() {
    uint32_t who=owner.load();
    return who==0?std::string("ninguem"):who==1?std::string("uma thread do host"):"a thread "+std::to_string(who);
}
bool latestFrame(Frame& frame,uint64_t after) {
    std::unique_lock<std::mutex> lock(mutex,std::try_to_lock);
    if(!lock||shownSerial<=after)return false;
    auto found=surfaces.find(shownWindow);if(found==surfaces.end())return false;
    auto& b=found->second;
    frame.pixels.resize(size_t(b.width)*b.height);
    for(int y=0;y<b.height;++y)std::copy(b.row(y),b.row(y)+b.width,frame.pixels.begin()+size_t(y)*b.width);
    frame.width=b.width;frame.height=b.height;frame.window=shownWindow;frame.serial=shownSerial;
    return true;
}
}

// ---- Device contexts ---------------------------------------------------------
extern "C" void* win_GetDC(void* window) {
    Lock lock;
    Dc d{window?Kind::Window:Kind::Screen,window,{},{}};
    if(window&&!surface(window)){setLastError(1400);return nullptr;}   // ERROR_INVALID_WINDOW_HANDLE
    auto h=handle();dcs[h]=d;return h;
}
extern "C" int32_t win_ReleaseDC(void*,void* h) {
    Lock lock;auto d=dc(h);
    if(!d||d->kind==Kind::Memory)return 0;
    dcs.erase(h);return 1;
}
extern "C" void* win_CreateCompatibleDC(void*){Lock lock;auto h=handle();dcs[h]={Kind::Memory,nullptr,{},{}};return h;}
extern "C" int32_t win_DeleteDC(void* h) {
    Lock lock;auto d=dc(h);
    if(!d||d->kind!=Kind::Memory){setLastError(ErrorInvalidHandle);return 0;}
    if(d->s.bitmap!=StockBitmap)bitmaps.at(d->s.bitmap).selectedIn=nullptr;
    dcs.erase(h);return 1;
}
extern "C" int32_t win_SaveDC(void* h) {
    Lock lock;auto d=dc(h);if(!d)return 0;
    d->saved.push_back(d->s);return int32_t(d->saved.size());
}
extern "C" int32_t win_RestoreDC(void* h,int32_t level) {
    Lock lock;auto d=dc(h);if(!d)return 0;
    int size=int(d->saved.size()),to=level<0?size+1+level:level;
    if(to<1||to>size){setLastError(ErrorInvalidParameter);return 0;}
    auto restored=d->saved[to-1];d->saved.resize(to-1);
    if(restored.bitmap!=d->s.bitmap) {
        if(d->s.bitmap!=StockBitmap)bitmaps.at(d->s.bitmap).selectedIn=nullptr;
        if(restored.bitmap!=StockBitmap) {
            auto f=bitmaps.find(restored.bitmap);if(f==bitmaps.end())unsupported("RestoreDC de bitmap ja apagado");
            f->second.selectedIn=h;
        }
    }
    d->s=restored;return 1;
}
extern "C" uint32_t win_SetBkColor(void* h,uint32_t c){Lock lock;auto d=dc(h);if(!d)return ClrInvalid;std::swap(c,d->s.background);return c;}
extern "C" uint32_t win_SetTextColor(void* h,uint32_t c){Lock lock;auto d=dc(h);if(!d)return ClrInvalid;std::swap(c,d->s.text);return c;}
extern "C" int32_t win_SetBkMode(void* h,int32_t mode) {
    Lock lock;auto d=dc(h);
    if(!d||(mode!=Transparent&&mode!=Opaque)){setLastError(ErrorInvalidParameter);return 0;}
    std::swap(mode,d->s.mode);return mode;
}
extern "C" int32_t win_SetStretchBltMode(void* h,int32_t mode) {
    Lock lock;auto d=dc(h);
    if(!d||mode<BlackOnWhite||mode>Halftone){setLastError(ErrorInvalidParameter);return 0;}
    std::swap(mode,d->s.stretch);return mode;
}
extern "C" int32_t win_SetBrushOrgEx(void* h,int32_t x,int32_t y,int32_t* old) {
    Lock lock;auto d=dc(h);if(!d)return 0;
    if(old){old[0]=d->s.origin[0];old[1]=d->s.origin[1];}
    d->s.origin[0]=x;d->s.origin[1]=y;return 1;
}
extern "C" int32_t win_IntersectClipRect(void* h,int32_t left,int32_t top,int32_t right,int32_t bottom) {
    Lock lock;auto d=dc(h);if(!d)return RegionError;
    Rect r{left,top,right,bottom};
    d->s.clip=d->s.clipped?intersect(d->s.clip,r):r;d->s.clipped=true;
    return empty(d->s.clip)?NullRegion:SimpleRegion;
}
extern "C" int32_t win_GdiFlush(void){return 1;}

// ---- Objects -------------------------------------------------------------------
extern "C" void* win_CreateCompatibleBitmap(void* h,int32_t width,int32_t height) {
    Lock lock;auto d=dc(h);
    if(!d){setLastError(ErrorInvalidHandle);return nullptr;}
    // Compatible with a memory DC's default bitmap means monochrome in GDI.
    if(d->kind==Kind::Memory&&d->s.bitmap==StockBitmap)unsupported("CreateCompatibleBitmap monocromatico");
    if(width<=0||height<=0)unsupported("CreateCompatibleBitmap vazio");
    auto b=handle();bitmaps[b]={width,height,false,std::vector<uint32_t>(size_t(width)*height,0),nullptr};return b;
}
extern "C" void* win_CreateDIBSection(void*,const void* info,uint32_t usage,void** bits,void* section,uint32_t) {
    Lock lock;
    if(!info||!bits){setLastError(ErrorInvalidParameter);return nullptr;}
    auto const& h=*static_cast<const BitmapInfoHeader*>(info);
    if(section||usage!=0||h.size<sizeof(BitmapInfoHeader)||h.bitCount!=32||h.compression!=0||h.planes!=1||h.width<=0||!h.height)
        unsupported("CreateDIBSection fora de 32 bpp BI_RGB em memoria");
    int height=std::abs(h.height);
    auto b=handle();
    auto& bitmap=bitmaps[b]={h.width,height,h.height>0,std::vector<uint32_t>(size_t(h.width)*height,0),nullptr};
    *bits=bitmap.pixels.data();return b;
}
extern "C" void* win_CreateFontW(int32_t height,int32_t width,int32_t escapement,int32_t orientation,int32_t weight,uint32_t italic,uint32_t underline,
                                 uint32_t strikeOut,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,const uint16_t* face) {
    Lock lock;
    if(width||escapement||orientation||italic||underline||strikeOut||!height)unsupported("CreateFontW com largura, rotacao, estilo ou altura 0");
    auto f=handle();
    fonts[f]={height,weight?weight:400,face&&*face?std::u16string(reinterpret_cast<const char16_t*>(face)):u"Arial",nullptr,{}};
    return f;
}
extern "C" void* win_SelectObject(void* h,void* object) {
    Lock lock;auto d=dc(h);
    if(!d){setLastError(ErrorInvalidHandle);return nullptr;}
    if(object==StockBitmap||bitmaps.count(object)) {
        if(d->kind!=Kind::Memory){setLastError(ErrorInvalidParameter);return nullptr;}
        if(object!=StockBitmap) {
            auto& b=bitmaps.at(object);
            if(b.selectedIn&&b.selectedIn!=h){setLastError(ErrorInvalidParameter);return nullptr;}
            b.selectedIn=h;
        }
        auto old=d->s.bitmap;if(old!=StockBitmap&&old!=object)bitmaps.at(old).selectedIn=nullptr;
        d->s.bitmap=object;return old;
    }
    if(object==StockFont||fonts.count(object)){auto old=d->s.font;d->s.font=object;return old;}
    unsupported("SelectObject de objeto "+hex(reinterpret_cast<uintptr_t>(object)));
}
extern "C" int32_t win_DeleteObject(void* object) {
    Lock lock;
    if(object==StockBitmap||object==StockFont)return 1;
    if(brushes.erase(object))return 1;
    if(auto b=bitmaps.find(object);b!=bitmaps.end()){if(b->second.selectedIn)return 0;bitmaps.erase(b);return 1;}
    if(fonts.count(object)) {
        for(auto const& [h,d]:dcs)if(d.s.font==object)return 0;
        fonts.erase(object);return 1;
    }
    setLastError(ErrorInvalidHandle);return 0;
}

// ---- Drawing -------------------------------------------------------------------
extern "C" int32_t win_ExtTextOutW(void* h,int32_t,int32_t,uint32_t options,const int32_t* rect,const uint16_t* text,uint32_t count,const int32_t*) {
    Lock lock;auto d=dc(h);
    if(!d){setLastError(ErrorInvalidHandle);return 0;}
    if((text&&count)||(options&~(EtoOpaque|EtoClipped)))unsupported("ExtTextOutW com texto ou opcoes "+hex(options));
    if(!(options&EtoOpaque)||!rect)return 1;
    auto b=target(*d,"ExtTextOutW");if(!b)return 0;
    auto area=intersect({rect[0],rect[1],rect[2],rect[3]},drawable(*d,*b));auto color=pixel(d->s.background);
    for(int y=area.top;y<area.bottom;++y)std::fill(b->row(y)+area.left,b->row(y)+std::max(area.left,area.right),color);
    return 1;
}
extern "C" int32_t win_StretchDIBits(void* h,int32_t xd,int32_t yd,int32_t wd,int32_t hd,int32_t xs,int32_t ys,int32_t ws,int32_t hs,
                                     const void* bits,const void* info,uint32_t usage,uint32_t rop) {
    Lock lock;auto d=dc(h);
    if(!d||!bits||!info){setLastError(ErrorInvalidParameter);return 0;}
    auto const& header=*static_cast<const BitmapInfoHeader*>(info);
    if(rop!=SrcCopy||usage!=0||header.bitCount!=32||header.compression!=0||header.width<=0||!header.height||wd<=0||hd<=0||ws<=0||hs<=0)
        unsupported("StretchDIBits fora de copia 32 bpp sem espelhamento");
    auto b=target(*d,"StretchDIBits");if(!b)return 0;
    int width=header.width,height=std::abs(header.height);bool bottomUp=header.height>0;
    // For bottom-up DIBs the source y is measured from the bottom row.
    int top=bottomUp?height-ys-hs:ys;
    auto src=[&](int x,int y){y=std::clamp(y,0,height-1);x=std::clamp(x,0,width-1);
        return static_cast<const uint32_t*>(bits)[size_t(bottomUp?height-1-y:y)*width+x];};
    auto area=intersect({xd,yd,xd+wd,yd+hd},drawable(*d,*b));
    if(empty(area))return hs;
    if(d->s.stretch!=Halftone) {
        for(int y=area.top;y<area.bottom;++y)for(int x=area.left;x<area.right;++x)
            b->row(y)[x]=src(xs+int((x-xd+0.5)*ws/wd),top+int((y-yd+0.5)*hs/hd))&0xffffff;
        shown(*d);return hs;
    }
    std::vector<std::vector<Weight>> cols(area.right-area.left),rows(area.bottom-area.top);
    for(int x=area.left;x<area.right;++x)cols[x-area.left]=taps(xs,ws,wd,x-xd);
    for(int y=area.top;y<area.bottom;++y)rows[y-area.top]=taps(top,hs,hd,y-yd);
    int first=rows.front().front().index,last=rows.back().back().index;
    for(auto& r:rows)for(auto& t:r){first=std::min(first,t.index);last=std::max(last,t.index);}
    // Horizontal pass over the source rows the clipped area needs.
    size_t w=cols.size();std::vector<float> line(size_t(last-first+1)*w*3);
    for(int y=first;y<=last;++y)for(size_t x=0;x<w;++x) {
        float acc[3]{};for(auto& t:cols[x]){auto p=src(t.index,y);for(int c=0;c<3;++c)acc[c]+=t.weight*float((p>>(8*c))&0xff);}
        std::copy(acc,acc+3,line.begin()+long((size_t(y-first)*w+x)*3));
    }
    for(int y=area.top;y<area.bottom;++y)for(size_t x=0;x<w;++x) {
        float acc[3]{};for(auto& t:rows[y-area.top])for(int c=0;c<3;++c)acc[c]+=t.weight*line[(size_t(t.index-first)*w+x)*3+c];
        uint32_t p=0;for(int c=0;c<3;++c)p|=uint32_t(std::clamp(int(acc[c]+0.5f),0,255))<<(8*c);
        b->row(y)[area.left+long(x)]=p;
    }
    shown(*d);return hs;
}
namespace {
Bitmap* source(void* h,const char* api) {
    auto d=dc(h);if(!d){setLastError(ErrorInvalidHandle);return nullptr;}
    if(d->kind!=Kind::Memory)unsupported(std::string(api)+" lendo de DC de janela ou tela");
    return target(*d,api);
}
}
extern "C" int32_t win_GdiAlphaBlend(void* dst,int32_t xd,int32_t yd,int32_t wd,int32_t hd,void* src,int32_t xs,int32_t ys,int32_t ws,int32_t hs,uint32_t blend) {
    Lock lock;auto d=dc(dst);
    if(!d){setLastError(ErrorInvalidHandle);return 0;}
    // BLENDFUNCTION by value: BlendOp, BlendFlags, SourceConstantAlpha, AlphaFormat.
    unsigned op=blend&0xff,flags=(blend>>8)&0xff,constant=(blend>>16)&0xff,format=blend>>24;
    constexpr unsigned SrcAlpha=1;
    if(op!=0||flags||(format&~SrcAlpha))unsupported("GdiAlphaBlend com operacao "+hex(blend));
    auto s=source(src,"GdiAlphaBlend");auto b=target(*d,"GdiAlphaBlend");if(!s||!b)return 0;
    if(wd<0||hd<0||ws<0||hs<0||xs<0||ys<0||xs+ws>s->width||ys+hs>s->height){setLastError(ErrorInvalidParameter);return 0;}
    if(!wd||!hd||!ws||!hs)return 1;
    auto area=intersect({xd,yd,xd+wd,yd+hd},drawable(*d,*b));
    for(int y=area.top;y<area.bottom;++y) {
        auto srow=s->row(ys+int((y-yd+0.5)*hs/hd));auto drow=b->row(y);
        for(int x=area.left;x<area.right;++x) {
            uint32_t p=srow[xs+int((x-xd+0.5)*ws/wd)],q=drow[x],out=0;
            // AC_SRC_ALPHA: the source is premultiplied by its own alpha.
            unsigned a=(format&SrcAlpha)?((p>>24)*constant+127)/255:constant;
            for(int c=0;c<32;c+=8)out|=std::min(255u,(((p>>c)&0xff)*constant+127)/255+(((q>>c)&0xff)*(255-a)+127)/255)<<c;
            drow[x]=out;
        }
    }
    return 1;
}
extern "C" int32_t win_BitBlt(void* dst,int32_t x,int32_t y,int32_t w,int32_t h,void* src,int32_t xs,int32_t ys,uint32_t rop) {
    Lock lock;auto d=dc(dst);
    if(!d){setLastError(ErrorInvalidHandle);return 0;}
    if(rop!=SrcCopy)unsupported("BitBlt com ROP "+hex(rop));
    auto s=source(src,"BitBlt");auto b=target(*d,"BitBlt");if(!s||!b)return 0;
    auto area=intersect(intersect({x,y,x+w,y+h},drawable(*d,*b)),{x-xs,y-ys,x-xs+s->width,y-ys+s->height});
    for(int py=area.top;py<area.bottom;++py)
        std::copy(s->row(py-y+ys)+(area.left-x+xs),s->row(py-y+ys)+(area.right-x+xs),b->row(py)+area.left);
    shown(*d);
    return 1;
}

// ---- Text --------------------------------------------------------------------
extern "C" int32_t win_GetTextExtentPoint32W(void* h,const uint16_t* text,int32_t count,int32_t* size) {
    Lock lock;auto d=dc(h);
    if(!d||!size||count<0||(count&&!text)){setLastError(ErrorInvalidParameter);return 0;}
    auto& f=font(*d,"GetTextExtentPoint32W");
    size[0]=count?textWidth(f,std::u16string(reinterpret_cast<const char16_t*>(text),size_t(count))):0;
    size[1]=f.size.cell;
    return 1;
}
extern "C" int32_t win_DrawTextW(void* h,const uint16_t* text,int32_t count,int32_t* rectangle,uint32_t format) {
    Lock lock;auto d=dc(h);
    if(!d||!rectangle||!text){setLastError(ErrorInvalidParameter);return 0;}
    constexpr uint32_t allowed=DtCenter|DtRight|DtVCenter|DtBottom|DtSingleLine|DtNoClip|DtNoPrefix|DtPathEllipsis|DtEndEllipsis;
    if((format&~allowed)||!(format&DtSingleLine)||!(format&DtNoPrefix))unsupported("DrawTextW com formato "+hex(format));
    if(count<0)count=int32_t(length(text));
    auto& f=font(*d,"DrawTextW");auto b=target(*d,"DrawTextW");if(!b)return 0;
    Rect rect{rectangle[0],rectangle[1],rectangle[2],rectangle[3]};
    int line=f.size.cell,top=rect.top;
    if(format&DtVCenter)top=(rect.top+rect.bottom-line)/2;else if(format&DtBottom)top=rect.bottom-line;
    if(count) {
        std::u16string s(reinterpret_cast<const char16_t*>(text),size_t(count));
        int room=std::max(0,rect.right-rect.left);
        if(format&(DtEndEllipsis|DtPathEllipsis))s=ellipsize(f,s,room,format&DtPathEllipsis);
        int width=textWidth(f,s),left=rect.left;
        if(format&DtCenter)left=(rect.left+rect.right-width)/2;else if(format&DtRight)left=rect.right-width;
        auto area=drawable(*d,*b);if(!(format&DtNoClip))area=intersect(area,rect);
        if(d->s.mode==Opaque) {
            auto box=intersect(area,{left,top,left+width,top+line});auto color=pixel(d->s.background);
            for(int y=box.top;y<box.bottom;++y)std::fill(b->row(y)+box.left,b->row(y)+std::max(box.left,box.right),color);
        }
        // Glyphs may reach past their advances: the coverage has a margin.
        int margin=line/2,cw=width+2*margin;
        std::vector<float> coverage(size_t(cw)*line,0.f);
        f.font->rasterize(s,f.size,float(margin),float(f.size.ascent),coverage,cw,line);
        auto box=intersect(area,{left-margin,top,left-margin+cw,top+line});auto color=pixel(d->s.text);
        for(int y=box.top;y<box.bottom;++y) {
            auto row=b->row(y);auto c=coverage.data()+size_t(y-top)*cw;
            for(int x=box.left;x<box.right;++x){float v=c[x-(left-margin)];if(v>0)row[x]=mix(row[x],color,v);}
        }
    }
    return (format&(DtVCenter|DtBottom))?top-rect.top+line:line;
}

extern "C" void* win_CreateSolidBrush(uint32_t color){Lock lock;auto h=handle();brushes[h]=color;return h;}
extern "C" void* win_CreateBitmap(int32_t width,int32_t height,uint32_t planes,uint32_t bits,const void* data){
    Lock lock;
    if(width<=0||height<=0||width>16384||height>16384||planes!=1||(bits!=1&&bits!=32)){setLastError(ErrorInvalidParameter);return nullptr;}
    auto h=handle();auto& b=bitmaps[h]={width,height,false,std::vector<uint32_t>(size_t(width)*height,0),nullptr};
    if(data){auto bytes=static_cast<const uint8_t*>(data);size_t stride=((size_t(width)*bits+15)/16)*2;
        for(int y=0;y<height;++y)for(int x=0;x<width;++x){
            if(bits==32)std::memcpy(&b.pixels[size_t(y)*width+x],bytes+size_t(y)*stride+x*4,4);
            else b.pixels[size_t(y)*width+x]=(bytes[size_t(y)*stride+x/8]&(0x80>>(x%8)))?0xffffff:0;
        }
    }
    return h;
}
extern "C" void* win_CreateIconIndirect(const void* value){
    struct Info {int32_t icon;uint32_t x,y,pad;void* mask;void* color;};
    if(!value){setLastError(ErrorInvalidParameter);return nullptr;}
    auto& info=*static_cast<const Info*>(value);Lock lock;
    if(!bitmaps.count(info.mask)||(info.color&&!bitmaps.count(info.color))){setLastError(ErrorInvalidHandle);return nullptr;}
    auto h=handle();icons[h]={bitmaps.at(info.mask),info.color?bitmaps.at(info.color):Bitmap{}};return h;
}
extern "C" int32_t win_DestroyIcon(void* icon){Lock lock;return icons.erase(icon)?1:0;}
