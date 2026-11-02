#include "Font.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace azimuth::gfx {
namespace {
struct Reader {
    const std::vector<uint8_t>& d;
    uint32_t check(uint32_t at,uint32_t size) const {
        if(at>d.size()||size>d.size()-at)throw std::runtime_error("fonte truncada");
        return at;
    }
    uint8_t u8(uint32_t at) const {return d[check(at,1)];}
    uint16_t u16(uint32_t at) const {check(at,2);return uint16_t(d[at]<<8|d[at+1]);}
    int16_t i16(uint32_t at) const {return int16_t(u16(at));}
    uint32_t u32(uint32_t at) const {check(at,4);return uint32_t(d[at])<<24|uint32_t(d[at+1])<<16|uint32_t(d[at+2])<<8|d[at+3];}
};
// Signed-area accumulation: each edge adds its coverage to the cells it
// crosses; a running sum along each row gives the coverage of every pixel.
void edge(std::vector<float>& acc,int stride,int width,int height,float x0,float y0,float x1,float y1) {
    if(y0==y1)return;
    float direction=1;
    if(y0>y1){std::swap(x0,x1);std::swap(y0,y1);direction=-1;}
    x0=std::clamp(x0,0.f,float(width));x1=std::clamp(x1,0.f,float(width));
    float slope=(x1-x0)/(y1-y0),x=x0;
    if(y0<0)x-=y0*slope;
    for(int y=std::max(0,int(std::floor(y0)));y<std::min(height,int(std::ceil(y1)));++y) {
        float* row=acc.data()+size_t(y)*stride;
        float dy=std::min(float(y+1),y1)-std::max(float(y),y0),next=x+slope*dy,d=dy*direction;
        float left=std::min(x,next),right=std::max(x,next);
        float leftFloor=std::floor(left);int l=int(leftFloor),r=int(std::ceil(right));
        if(r<=l+1) {
            float middle=0.5f*(x+next)-leftFloor;
            row[l]+=d-d*middle;row[l+1]+=d*middle;
        }else {
            float s=1/(right-left),leftFraction=left-leftFloor,a0=0.5f*s*(1-leftFraction)*(1-leftFraction);
            float rightFraction=right-float(r)+1,am=0.5f*s*rightFraction*rightFraction;
            row[l]+=d*a0;
            if(r==l+2)row[l+1]+=d*(1-a0-am);
            else {
                float a1=s*(1.5f-leftFraction);row[l+1]+=d*(a1-a0);
                for(int k=l+2;k<r-1;++k)row[k]+=d*s;
                float a2=a1+float(r-l-3)*s;row[r-1]+=d*(1-a2-am);
            }
            row[r]+=d*am;
        }
        x=next;
    }
}
}

std::shared_ptr<Font> Font::load(const std::string& path) {
    std::ifstream in(path,std::ios::binary);
    if(!in)return nullptr;
    auto font=std::make_shared<Font>();
    font->data_.assign(std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>());
    Reader r{font->data_};
    uint32_t head=0,maxp=0,hhea=0,os2=0,os2Size=0;
    for(uint16_t n=0,count=r.u16(4);n<count;++n) {
        uint32_t record=12+16u*n,tag=r.u32(record),offset=r.u32(record+8),size=r.u32(record+12);
        r.check(offset,size);
        switch(tag) {
            case 0x68656164:head=offset;break;           // head
            case 0x6d617870:maxp=offset;break;           // maxp
            case 0x68686561:hhea=offset;break;           // hhea
            case 0x686d7478:font->hmtx_=offset;break;    // hmtx
            case 0x6c6f6361:font->loca_=offset;break;    // loca
            case 0x676c7966:font->glyf_=offset;break;    // glyf
            case 0x636d6170:font->cmap_=offset;break;    // cmap
            case 0x4f532f32:os2=offset;os2Size=size;break; // OS/2
        }
    }
    if(!head||!maxp||!hhea||!font->hmtx_||!font->loca_||!font->glyf_||!font->cmap_)throw std::runtime_error("fonte sem as tabelas TrueType (glyf) esperadas: "+path);
    font->unitsPerEm_=r.u16(head+18);font->longLoca_=r.i16(head+50);
    font->glyphs_=r.u16(maxp+4);font->metrics_=r.u16(hhea+34);
    font->designAscent_=r.i16(hhea+4);font->designDescent_=-r.i16(hhea+6);font->designLineGap_=r.i16(hhea+8);
    if(os2&&os2Size>=78){font->winAscent_=r.u16(os2+74);font->winDescent_=r.u16(os2+76);}
    else{font->winAscent_=r.i16(hhea+4);font->winDescent_=-r.i16(hhea+6);}
    // The Unicode BMP subtable (format 4): Windows (3,1), else Unicode (0,x).
    uint32_t chosen=0;
    for(uint16_t n=0,count=r.u16(font->cmap_+2);n<count;++n) {
        uint32_t record=font->cmap_+4+8u*n,table=font->cmap_+r.u32(record+4);
        uint16_t platform=r.u16(record),encoding=r.u16(record+2);
        if(r.u16(table)!=4)continue;
        if(platform==3&&encoding==1){chosen=table;break;}
        if(platform==0&&!chosen)chosen=table;
    }
    if(!chosen)throw std::runtime_error("fonte sem cmap Unicode formato 4: "+path);
    font->cmap_=chosen;
    return font;
}
uint16_t Font::glyph(char16_t c) const {
    Reader r{data_};
    uint16_t segments2=r.u16(cmap_+6);
    uint32_t ends=cmap_+14,starts=ends+segments2+2,deltas=starts+segments2,ranges=deltas+segments2;
    for(uint16_t i=0;i<segments2;i+=2) {
        if(c>r.u16(ends+i))continue;
        uint16_t start=r.u16(starts+i);if(c<start)return 0;
        uint16_t delta=r.u16(deltas+i),range=r.u16(ranges+i);
        if(!range)return uint16_t(c+delta);
        uint16_t g=r.u16(ranges+i+range+2u*(c-start));
        return g?uint16_t(g+delta):0;
    }
    return 0;
}
int Font::advance(uint16_t g) const {
    Reader r{data_};
    return r.u16(hmtx_+4u*uint32_t(g<metrics_?g:metrics_-1));
}
Font::Size Font::size(int gdiHeight) const {
    Size s;
    float em=gdiHeight<0?float(-gdiHeight):float(gdiHeight)*float(unitsPerEm_)/float(winAscent_+winDescent_);
    s.scale=em/float(unitsPerEm_);
    s.ascent=int(std::lround(winAscent_*s.scale));
    s.cell=s.ascent+int(std::lround(winDescent_*s.scale));
    return s;
}
std::vector<int> Font::advances(const std::u16string& text,const Size& size) const {
    std::vector<int> out;
    for(auto c:text)out.push_back(int(std::lround(advance(glyph(c))*size.scale)));
    return out;
}
void Font::outline(uint16_t g,float a,float b,float c,float d,float dx,float dy,std::vector<std::vector<Point>>& contours,int depth) const {
    if(g>=glyphs_||depth>8)return;
    Reader r{data_};
    uint32_t start=longLoca_?r.u32(loca_+4u*g):2u*r.u16(loca_+2u*g);
    uint32_t end=longLoca_?r.u32(loca_+4u*(g+1)):2u*r.u16(loca_+2u*(g+1));
    if(end<=start)return;
    uint32_t p=glyf_+start;
    int16_t count=r.i16(p);
    if(count<0) {   // composite: components with an offset and an optional scale or 2x2 matrix
        uint32_t at=p+10;
        for(;;) {
            uint16_t flags=r.u16(at),component=r.u16(at+2);at+=4;
            float ox,oy;
            if(flags&1){ox=r.i16(at);oy=r.i16(at+2);at+=4;}
            else{ox=int8_t(r.u8(at));oy=int8_t(r.u8(at+1));at+=2;}
            if(!(flags&2))ox=oy=0;   // point matching: not used by the launcher's fonts
            float ca=1,cb=0,cc=0,cd=1;
            if(flags&8){ca=cd=r.i16(at)/16384.f;at+=2;}
            else if(flags&0x40){ca=r.i16(at)/16384.f;cd=r.i16(at+2)/16384.f;at+=4;}
            else if(flags&0x80){ca=r.i16(at)/16384.f;cb=r.i16(at+2)/16384.f;cc=r.i16(at+4)/16384.f;cd=r.i16(at+6)/16384.f;at+=8;}
            outline(component,a*ca+c*cb,b*ca+d*cb,a*cc+c*cd,b*cc+d*cd,a*ox+c*oy+dx,b*ox+d*oy+dy,contours,depth+1);
            if(!(flags&0x20))break;
        }
        return;
    }
    if(!count)return;
    std::vector<uint16_t> ends(static_cast<size_t>(count));
    for(int n=0;n<count;++n)ends[size_t(n)]=r.u16(p+10+2u*uint32_t(n));
    size_t points=size_t(ends.back())+1;
    uint32_t at=p+10+2u*uint32_t(count);at+=2+r.u16(at);
    std::vector<uint8_t> flags;
    while(flags.size()<points) {
        uint8_t f=r.u8(at++);flags.push_back(f);
        if(f&8){for(uint8_t k=r.u8(at++);k&&flags.size()<points;--k)flags.push_back(f);}
    }
    std::vector<float> xs(points),ys(points);
    int value=0;
    for(size_t n=0;n<points;++n) {
        auto f=flags[n];
        if(f&2){int v=r.u8(at++);value+=(f&16)?v:-v;}
        else if(!(f&16)){value+=r.i16(at);at+=2;}
        xs[n]=float(value);
    }
    value=0;
    for(size_t n=0;n<points;++n) {
        auto f=flags[n];
        if(f&4){int v=r.u8(at++);value+=(f&32)?v:-v;}
        else if(!(f&32)){value+=r.i16(at);at+=2;}
        ys[n]=float(value);
    }
    size_t first=0;
    for(auto last:ends) {
        std::vector<Point> contour;
        for(size_t n=first;n<=last&&n<points;++n)
            contour.push_back({a*xs[n]+c*ys[n]+dx,b*xs[n]+d*ys[n]+dy,(flags[n]&1)!=0});
        if(!contour.empty())contours.push_back(std::move(contour));
        first=size_t(last)+1;
    }
}
void Font::rasterize(const std::u16string& text,const Size& size,float x,float baseline,std::vector<float>& coverage,int width,int height) const {
    std::vector<uint16_t> indices;for(auto ch:text)indices.push_back(glyph(ch));
    rasterizeGlyphs(indices,{}, {},size,x,baseline,coverage,width,height);
}
Font::GlyphBox Font::glyphBox(uint16_t g) const {
    if(g>=glyphs_)return {};
    Reader r{data_};auto start=longLoca_?r.u32(loca_+4u*g):2u*r.u16(loca_+2u*g);
    auto end=longLoca_?r.u32(loca_+4u*(g+1)):2u*r.u16(loca_+2u*(g+1));
    if(start==end)return {};
    auto at=glyf_+start;return {r.i16(at+2),r.i16(at+4),r.i16(at+6),r.i16(at+8)};
}
void Font::rasterizeGlyphs(const std::vector<uint16_t>& indices,const std::vector<float>& advances,const std::vector<Offset>& offsets,const Size& size,float x,float baseline,std::vector<float>& coverage,int width,int height) const {
    int stride=width+3;
    std::vector<float> acc(size_t(stride)*size_t(height)+4,0.f);
    float pen=x;
    for(size_t nGlyph=0;nGlyph<indices.size();++nGlyph) {
        auto g=indices[nGlyph];auto offset=nGlyph<offsets.size()?offsets[nGlyph]:Offset{};
        std::vector<std::vector<Point>> contours;
        outline(g,1,0,0,1,0,0,contours,0);
        auto map=[&](Point p){return Point{pen+offset.advance+p.x*size.scale,baseline-offset.ascender-p.y*size.scale,p.on};};
        for(auto const& raw:contours) {
            std::vector<Point> pts;for(auto const& p:raw)pts.push_back(map(p));
            size_t n=pts.size();
            Point start;size_t begin,count;
            if(pts[0].on){start=pts[0];begin=1;count=n-1;}
            else if(pts[n-1].on){start=pts[n-1];begin=0;count=n-1;}
            else{start={(pts[0].x+pts[n-1].x)/2,(pts[0].y+pts[n-1].y)/2,true};begin=0;count=n;}
            Point current=start,control{};bool pending=false;
            auto line=[&](Point to){edge(acc,stride,width,height,current.x,current.y,to.x,to.y);current=to;};
            auto curve=[&](Point ctl,Point to) {
                float length=std::hypot(ctl.x-current.x,ctl.y-current.y)+std::hypot(to.x-ctl.x,to.y-ctl.y);
                int steps=std::clamp(int(length/2)+1,1,32);
                Point from=current;
                for(int k=1;k<=steps;++k) {
                    float t=float(k)/float(steps),u=1-t;
                    line({u*u*from.x+2*u*t*ctl.x+t*t*to.x,u*u*from.y+2*u*t*ctl.y+t*t*to.y,true});
                }
            };
            for(size_t k=0;k<count;++k) {
                auto p=pts[(begin+k)%n];
                if(p.on){if(pending){curve(control,p);pending=false;}else line(p);}
                else {
                    if(pending){Point middle{(control.x+p.x)/2,(control.y+p.y)/2,true};curve(control,middle);}
                    control=p;pending=true;
                }
            }
            if(pending)curve(control,start);else line(start);
        }
        pen+=nGlyph<advances.size()?advances[nGlyph]:float(std::lround(advance(g)*size.scale));
    }
    for(int y=0;y<height;++y) {
        float sum=0;const float* row=acc.data()+size_t(y)*stride;float* out=coverage.data()+size_t(y)*width;
        for(int px=0;px<width;++px){sum+=row[px];out[px]=std::min(1.f,out[px]+std::fabs(sum));}
    }
}
}
