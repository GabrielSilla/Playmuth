// Windows and GDI of the host, checked on Linux without the engine: the
// checks of AzimuthUwp's GuestGdiTests over the PS5 implementations (a real
// TEB through gs, a window procedure with the Windows x64 convention), then
// a 1920x1080 test picture drawn with the launcher's calls and saved by the
// host's frame path. GdiTest <out.png>
#include "../src/Teb.h"
#include "../src/gfx/Png.h"
#include "../src/win32/Win32.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

#define EXPORT(dll,name,ret,params,args) extern "C" ret win_##name params;
#include "../src/win32/exports.inc"
#undef EXPORT

namespace {
std::string stopped;
struct WndClassEx {uint32_t size,style;void* proc;int32_t classExtra,windowExtra;void* instance;void* icon;void* cursor;void* background;const uint16_t* menuName;const uint16_t* className;void* smallIcon;};
struct BitmapInfoHeader {uint32_t size;int32_t width,height;uint16_t planes,bitCount;uint32_t compression,sizeImage;int32_t xPels,yPels;uint32_t used,important;};
constexpr uint32_t EtoOpaque=2,SrcCopy=0x00cc0020,ColorOnColor=3,Halftone=4,BlackOnWhite=1,Transparent=1,SimpleRegion=2;
constexpr uint32_t DtCenter=1,DtRight=2,DtVCenter=4,DtSingleLine=0x20,DtNoPrefix=0x800,DtPathEllipsis=0x4000,DtEndEllipsis=0x8000;
constexpr uint32_t Blend=0x01ff0000;   // AC_SRC_OVER, flags 0, constant 255, AC_SRC_ALPHA
__attribute__((ms_abi)) int64_t proc(void* window,uint32_t message,uint64_t w,int64_t l){return win_DefWindowProcW(window,message,w,l);}
const uint16_t* w16(const char16_t* text){return reinterpret_cast<const uint16_t*>(text);}
uint32_t at(const azimuth::win32::Frame& f,int x,int y){return f.pixels[size_t(y)*f.width+x]&0xffffff;}
}
namespace azimuth::win32 {
void log(const std::string& text){std::printf("  [host] %s\n",text.c_str());}
void stopGuest(const std::string& reason){stopped=reason;throw std::runtime_error("parada: "+reason);}
}
using namespace azimuth::win32;

int main(int argc,char** argv) {
    if(argc!=2){std::fprintf(stderr,"uso: GdiTest saida.png\n");return 2;}
    azimuth::ProcessBlock processBlock(0);
    azimuth::ThreadBlock thread(processBlock,nullptr,nullptr,256);
    if(thread.enter()){std::fprintf(stderr,"gs nao configurado\n");return 1;}
    configure({});
    int checks=0;
    auto check=[&](bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(std::string("falhou: ")+why);};
    try {
        WndClassEx wc{sizeof(WndClassEx),0,reinterpret_cast<void*>(&proc),0,0,nullptr,nullptr,nullptr,nullptr,nullptr,w16(u"Gdi.Test"),nullptr};
        check(win_RegisterClassExW(&wc)!=0,"RegisterClassExW");
        auto h=win_CreateWindowExW(0,w16(u"Gdi.Test"),w16(u""),0xcf0000,0,0,64,32,nullptr,nullptr,nullptr,nullptr);
        check(h!=nullptr,"CreateWindowExW");
        auto screen=win_GetDC(h);check(screen!=nullptr,"window DC");
        auto mem=win_CreateCompatibleDC(screen);auto bmp=win_CreateCompatibleBitmap(screen,64,32);
        check(mem&&bmp,"memory DC and compatible bitmap");
        auto old=win_SelectObject(mem,bmp);check(old!=nullptr,"stock bitmap returned");
        auto other=win_CreateCompatibleDC(screen);check(!win_SelectObject(other,bmp),"bitmap selected in one DC only");
        check(!win_DeleteObject(bmp),"selected bitmap not deleted");
        // Opaque fill with the background colour.
        check(win_SetBkColor(mem,0x161212)==0xffffff,"default background colour");
        int32_t all[4]{0,0,64,32};check(win_ExtTextOutW(mem,0,0,EtoOpaque,all,nullptr,0,nullptr),"opaque fill");
        win_BitBlt(screen,0,0,64,32,mem,0,0,SrcCopy);
        Frame f;check(latestFrame(f,0)&&f.width==64&&f.height==32&&f.window==h,"blit to window publishes frame");
        check(at(f,10,10)==0x121216,"COLORREF to BGRX");
        auto serial=f.serial;check(!latestFrame(f,serial),"no new frame without blit");
        // Clipping inside SaveDC/RestoreDC.
        check(win_SaveDC(mem)==1,"SaveDC level");
        check(win_IntersectClipRect(mem,0,0,8,8)==int(SimpleRegion),"clip rect");
        win_SetBkColor(mem,0x0000ff);win_ExtTextOutW(mem,0,0,EtoOpaque,all,nullptr,0,nullptr);
        check(win_RestoreDC(mem,-1),"RestoreDC");
        win_SetBkColor(mem,0x00ff00);int32_t tail[4]{32,0,64,32};win_ExtTextOutW(mem,0,0,EtoOpaque,tail,nullptr,0,nullptr);
        win_BitBlt(screen,0,0,64,32,mem,0,0,SrcCopy);latestFrame(f,serial);serial=f.serial;
        check(at(f,4,4)==0xff0000&&at(f,20,20)==0x121216&&at(f,40,4)==0x00ff00,"clip applied then restored");
        // AlphaBlend of a 1x1 premultiplied DIB, as the launcher's overlay.
        BitmapInfoHeader bi{sizeof(BitmapInfoHeader),1,1,1,32,0,0,0,0,0,0};void* bits=nullptr;
        auto dib=win_CreateDIBSection(mem,&bi,0,&bits,nullptr,0);check(dib&&bits,"DIB section");
        *static_cast<uint32_t*>(bits)=0xc03060a8;
        auto src=win_CreateCompatibleDC(mem);win_SelectObject(src,dib);
        int32_t box[4]{0,0,8,8};win_SetBkColor(mem,0xffffff);win_ExtTextOutW(mem,0,0,EtoOpaque,box,nullptr,0,nullptr);
        check(win_GdiAlphaBlend(mem,0,0,8,8,src,0,0,1,1,Blend),"alpha blend");
        win_BitBlt(screen,0,0,64,32,mem,0,0,SrcCopy);latestFrame(f,serial);serial=f.serial;
        // 0x30 + 255*(63/255) = 0x6f; 0x60 + 63 = 0x9f; 0xa8 + 63 = 0xe7.
        check(at(f,3,3)==0x6f9fe7,"premultiplied source-over");
        check(!win_GdiAlphaBlend(mem,0,0,8,8,src,0,0,2,2,Blend),"source rect outside bitmap");
        // StretchDIBits: top-down 2x1 image, nearest and HALFTONE.
        uint32_t image[2]={0x000000,0xffffff};BitmapInfoHeader ii{sizeof(BitmapInfoHeader),2,-1,1,32,0,0,0,0,0,0};
        check(win_StretchDIBits(mem,0,0,8,4,0,0,2,1,image,&ii,0,SrcCopy)==1,"stretch nearest");
        win_BitBlt(screen,0,0,64,32,mem,0,0,SrcCopy);latestFrame(f,serial);serial=f.serial;
        check(at(f,1,1)==0&&at(f,6,1)==0xffffff,"nearest halves");
        check(win_SetStretchBltMode(mem,Halftone)==int(BlackOnWhite),"stretch mode");
        win_StretchDIBits(mem,0,0,8,4,0,0,2,1,image,&ii,0,SrcCopy);
        win_BitBlt(screen,0,0,64,32,mem,0,0,SrcCopy);latestFrame(f,serial);serial=f.serial;
        auto mid=at(f,4,1)&0xff,edge=at(f,0,1)&0xff;check(mid>0x60&&mid<0xa0&&edge==0,"halftone interpolates, edges clamp");
        // Bottom-up DIB rows.
        uint32_t two[2]={0x0000ff,0x00ff00};BitmapInfoHeader bu{sizeof(BitmapInfoHeader),1,2,1,32,0,0,0,0,0,0};
        win_SetStretchBltMode(mem,ColorOnColor);win_StretchDIBits(mem,0,0,1,2,0,0,1,2,two,&bu,0,SrcCopy);
        win_BitBlt(screen,0,0,64,32,mem,0,0,SrcCopy);latestFrame(f,serial);serial=f.serial;
        check(at(f,0,0)==0x00ff00&&at(f,0,1)==0x0000ff,"bottom-up DIB: first row is the bottom");
        // Text: extent, centred single line, clipping.
        auto font=win_CreateFontW(-14,0,0,0,600,0,0,0,1,4,0,5,0x22,w16(u"Arial"));check(font!=nullptr,"font");
        win_SetBkColor(mem,0x000000);win_ExtTextOutW(mem,0,0,EtoOpaque,all,nullptr,0,nullptr);
        auto oldFont=win_SelectObject(mem,font);int32_t size[2]{};
        check(win_GetTextExtentPoint32W(mem,w16(u"Keep"),4,size)&&size[0]>15&&size[0]<45&&size[1]>=14&&size[1]<=18,"text extent");
        win_SetBkMode(mem,Transparent);check(win_SetTextColor(mem,0xffffff)==0,"text colour");
        int32_t line[4]{0,0,64,32};
        int height=win_DrawTextW(mem,w16(u"Keep"),-1,line,DtCenter|DtVCenter|DtSingleLine|DtNoPrefix);
        check(height==(32-size[1])/2+size[1],"DT_VCENTER return value");
        win_BitBlt(screen,0,0,64,32,mem,0,0,SrcCopy);latestFrame(f,serial);serial=f.serial;
        int lit=0,left=64,right=0;
        for(int y=0;y<32;++y)for(int x=0;x<64;++x)if(at(f,x,y)){++lit;left=std::min(left,x);right=std::max(right,x);}
        check(lit>40,"text rasterized");
        check(std::abs((left+right)/2-32)<=3,"text centred");
        win_ExtTextOutW(mem,0,0,EtoOpaque,all,nullptr,0,nullptr);int32_t narrow[4]{0,0,10,32};
        win_DrawTextW(mem,w16(u"Keep"),-1,narrow,DtVCenter|DtSingleLine|DtNoPrefix);
        win_BitBlt(screen,0,0,64,32,mem,0,0,SrcCopy);latestFrame(f,serial);serial=f.serial;
        bool outside=false;for(int y=0;y<32;++y)for(int x=10;x<64;++x)outside=outside||at(f,x,y);
        check(!outside,"DrawTextW clips to its rectangle");
        win_SelectObject(mem,oldFont);check(win_DeleteObject(font),"font deleted after deselect");
        // Cleanup and refusal of calls outside the subset.
        win_SelectObject(mem,old);check(win_DeleteObject(bmp)&&win_DeleteDC(mem)&&win_DeleteDC(src)&&win_DeleteDC(other),"cleanup");
        check(win_ReleaseDC(h,screen)==1,"ReleaseDC");
        bool refused=false;auto m2=win_CreateCompatibleDC(nullptr);
        try{win_CreateCompatibleBitmap(m2,4,4);}catch(std::runtime_error const&){refused=stopped.find("monocromatico")!=std::string::npos;}
        check(refused,"monochrome compatible bitmap refused");
        check(win_DestroyWindow(h)&&!win_IsWindow(h),"DestroyWindow");
        std::printf("%d verificacoes de janelas e GDI passaram\n",checks);

        // A 1920x1080 test picture with the launcher's calls (memory DC,
        // fonts, opaque rows, DrawTextW alignments and ellipses, BitBlt to
        // the window), saved through latestFrame as the host does.
        auto launcher=win_CreateWindowExW(0,w16(u"Gdi.Test"),w16(u"Azimuth"),0xcf0000,int32_t(0x80000000),0,int32_t(0x80000000),0,nullptr,nullptr,nullptr,nullptr);
        win_ShowWindow(launcher,5);
        auto dc=win_GetDC(launcher);auto back=win_CreateCompatibleDC(dc);auto page=win_CreateCompatibleBitmap(dc,DisplayWidth,DisplayHeight);
        auto oldPage=win_SelectObject(back,page);
        auto big=win_CreateFontW(-32,0,0,0,400,0,0,0,1,4,0,5,0x22,w16(u"Arial"));
        auto small=win_CreateFontW(-22,0,0,0,400,0,0,0,1,4,0,5,0x22,w16(u"Arial"));
        auto title=win_CreateFontW(-26,0,0,0,700,0,0,0,1,4,0,5,0x22,w16(u"Arial"));
        int32_t screenRect[4]{0,0,DisplayWidth,DisplayHeight};
        win_SetBkColor(back,0x141012);win_ExtTextOutW(back,0,0,EtoOpaque,screenRect,nullptr,0,nullptr);
        win_SetBkMode(back,Transparent);
        auto text=[&](void* font,uint32_t color,const char16_t* s,int32_t l,int32_t t,int32_t r,int32_t b,uint32_t format) {
            win_SelectObject(back,font);win_SetTextColor(back,color);int32_t rc[4]{l,t,r,b};
            win_DrawTextW(back,w16(s),-1,rc,format|DtSingleLine|DtNoPrefix|DtVCenter);
        };
        text(small,0x9a9a9a,u"Player name:",30,20,200,56,0);
        text(small,0xffffff,u"Player 1",172,20,400,56,0);
        text(big,0x9a9a9a,u"Start",0,96,DisplayWidth,164,DtCenter);
        const char16_t* rows[][2]={{u"Resolution",u"1080p"},{u"Antialiasing",u"Off"},{u"Graphics",u"Low"},{u"Audio",u"Stereo"},
                                   {u"Frame rate",u"30 fps"},{u"Frame pacing",u"Fixed"},{u"V-sync",u"On"},{u"Display",u"Fullscreen"},
                                   {u"Language",u"System"},{u"Game folder",u"E:\\PC\\FH2\\game"}};
        int32_t y=164;
        for(auto const& row:rows) {
            bool selected=row[0]==rows[9][0];
            if(selected){int32_t bar[4]{533,y,1387,y+67};win_SetBkColor(back,0xb06334);win_ExtTextOutW(back,0,0,EtoOpaque,bar,nullptr,0,nullptr);}
            text(big,0xffffff,row[0],555,y,1000,y+67,0);
            text(big,0xffffff,row[1],1000,y,1365,y+67,DtRight|DtPathEllipsis);
            y+=67;
        }
        text(big,0xffffff,u"Quit",0,y,DisplayWidth,y+67,DtCenter);
        text(small,0xffffff,u"Pasta do jogo no SSD: /mnt/usb0/PC/FH2/game (um caminho longo o bastante para cortar com reticencias no fim)",
             490,900,1430,956,DtCenter|DtEndEllipsis);
        int32_t bar[4]{555,973,1365,977};win_SetBkColor(back,0x5a5a5a);win_ExtTextOutW(back,0,0,EtoOpaque,bar,nullptr,0,nullptr);
        text(small,0x9a9a9a,u"Preparing graphics pipelines   0 of 2344",0,982,DisplayWidth,1018,DtCenter);
        text(small,0x9a9a9a,u"Enter  Choose      \u2190 \u2192  Change      Esc  Back",0,1020,DisplayWidth,1056,DtCenter);
        text(small,0x9a9a9a,u"v0.9.7",1700,1020,1890,1056,DtRight);
        text(title,0x40c0ff,u"Quadro de teste do GDI do host (n\u00e3o \u00e9 o motor)",30,1020,900,1056,0);
        win_BitBlt(dc,0,0,DisplayWidth,DisplayHeight,back,0,0,SrcCopy);
        win_SelectObject(back,oldPage);win_DeleteObject(page);win_DeleteDC(back);win_ReleaseDC(launcher,dc);
        Frame picture;check(latestFrame(picture,serial)&&picture.width==DisplayWidth&&picture.height==DisplayHeight,"launcher frame published");
        check(azimuth::gfx::savePng(argv[1],picture.pixels,picture.width,picture.height),"frame saved as PNG");
        std::printf("Quadro de teste gravado: %s (%dx%d)\n",argv[1],picture.width,picture.height);
        return 0;
    }catch(std::exception const& e){std::fprintf(stderr,"%s (%d verificacoes antes)\n",e.what(),checks);return 1;}
}
