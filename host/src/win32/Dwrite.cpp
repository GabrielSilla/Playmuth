#include "Win32.h"
#include "DwriteAbi.h"
#include "../gfx/Font.h"
#include "../guest_abi.h"
#include <atomic>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <limits>
#include <algorithm>
using namespace azimuth::win32;
namespace {
constexpr int32_t Invalid=int32_t(0x80070057),NoInterface=int32_t(0x80004002),NotImpl=int32_t(0x80004001),NoFont=int32_t(0x88985002),Small=int32_t(0x8007007a);
enum class Kind {Factory,File,Builder,FontSet,Collection,Family,Font,Face,FallbackBuilder,Fallback,Format,Layout,Typography,Analysis,Strings,Sign};
struct Obj;
void add(Obj*);void drop(Obj*);
struct Ref {
    Obj* p=nullptr;
    Ref()=default;explicit Ref(Obj* value):p(value){add(p);}
    Ref(const Ref& r):Ref(r.p){}~Ref(){drop(p);}
    Ref& operator=(const Ref& r){auto next=r.p;add(next);drop(p);p=next;return *this;}
};
struct FontData {std::string path;std::u16string family;uint32_t weight=400,style=0;std::shared_ptr<azimuth::gfx::Font> font;Obj* face=nullptr;};
struct Props {std::u16string family,locale=u"en-US";uint32_t weight=400,style=0,stretch=5,align=0,paragraph=0,wrap=0,reading=0,flow=0,spacing=0;float em=16,lineSpacing=0,lineBaseline=0,tabStop=0;Ref collection,fallback;};
struct Attr {float em=16,leading=0,trailing=0,minimum=0;uint32_t weight=400,style=0;std::u16string family;std::shared_ptr<void> effect;bool tabular=false;};
struct Mapping {std::vector<std::pair<uint32_t,uint32_t>> ranges;std::vector<std::u16string> families;Ref collection;};
struct Offset {float advance,ascender;};
struct GlyphRun {void* face;float em;uint32_t count;const uint16_t* indices;const float* advances;const Offset* offsets;int32_t sideways;uint32_t bidi;};
static_assert(sizeof(GlyphRun)==48);
struct TextMetrics {float left,top,width,widthTrailing,height,layoutWidth,layoutHeight;uint32_t bidi,lines;};
struct Line {size_t first,last;float width,height,baseline;};
struct Obj {
    const void* const* vtable;std::atomic<uint32_t> refs{1};Kind kind;
    std::vector<std::shared_ptr<FontData>> fonts;std::shared_ptr<FontData> font;
    Props props;std::u16string text;std::vector<Attr> attrs;std::vector<Mapping> mappings;
    std::vector<uint64_t> features;float width=0,height=0;
    int32_t bounds[4]{};std::vector<float> coverage;std::u16string string;
    explicit Obj(Kind k):vtable(nullptr),kind(k){
        switch(k){
#define TABLE(n) case Kind::n:vtable=dw##n##Vtable;break;
        TABLE(Factory) TABLE(File) TABLE(Builder) TABLE(FontSet) TABLE(Collection) TABLE(Family) TABLE(Font) TABLE(Face)
        TABLE(FallbackBuilder) TABLE(Fallback) TABLE(Format) TABLE(Layout) TABLE(Typography) TABLE(Analysis) TABLE(Strings) TABLE(Sign)
#undef TABLE
        }
    }
};
void add(Obj* o){if(o)++o->refs;}void drop(Obj* o){if(o&&!--o->refs)delete o;}
Obj* object(void* p){return static_cast<Obj*>(p);}
std::u16string wide(const uint16_t* p){return p?std::u16string(reinterpret_cast<const char16_t*>(p)):std::u16string();}
bool equal(std::u16string a,std::u16string b){for(auto& c:a)if(c>=u'A'&&c<=u'Z')c+=32;for(auto& c:b)if(c>=u'A'&&c<=u'Z')c+=32;return a==b;}
int32_t result(Obj* value,void** out){if(!out){drop(value);return Invalid;}*out=value;return 0;}
std::mutex fontsLock;
std::map<std::string,std::shared_ptr<FontData>> fontsCache;
std::shared_ptr<FontData> load(const uint16_t* path){
    auto windows=wide(path);auto host=hostPath(windows);std::lock_guard<std::mutex> guard(fontsLock);
    if(auto found=fontsCache.find(host);found!=fontsCache.end())return found->second;
    auto raster=azimuth::gfx::Font::load(host);if(!raster)return {};
    auto f=std::make_shared<FontData>();f->path=host;f->font=raster;
    auto file=host.substr(host.find_last_of("/\\")+1);f->family=file.find("Montserrat")!=std::string::npos?u"Montserrat":file.find("DejaVu")!=std::string::npos?u"DejaVu Sans":utf16(file.substr(0,file.find('.')));
    auto dash=file.find('-');if(dash!=std::string::npos&&dash+1<file.size()&&file[dash+1]>='0'&&file[dash+1]<='9')f->weight=uint32_t(std::strtoul(file.c_str()+dash+1,nullptr,10));
    f->style=file.find("Italic")!=std::string::npos?2:0;
    f->face=new Obj(Kind::Face);f->face->font=f;fontsCache[host]=f;
    log("DirectWrite: fonte TrueType carregada: "+host);return f;
}
std::vector<std::u16string> families(Obj* o){std::vector<std::u16string> out;for(auto& f:o->fonts)if(std::none_of(out.begin(),out.end(),[&](auto& n){return equal(n,f->family);}))out.push_back(f->family);return out;}
std::shared_ptr<FontData> select(Obj* collection,const std::u16string& family,uint32_t weight,uint32_t style){
    std::shared_ptr<FontData> best;int score=std::numeric_limits<int>::max();
    if(collection)for(auto& f:collection->fonts)if(equal(f->family,family)){
        int n=std::abs(int(f->weight)-int(weight))+(style==f->style?0:1000);if(n<score){best=f;score=n;}
    }
    return best;
}
std::shared_ptr<FontData> pick(Obj* o,size_t index){
    auto& a=o->attrs[index];auto selected=select(o->props.collection.p,a.family,a.weight,a.style);if(!selected)return {};
    if(selected->font->glyphIndex(o->text[index]))return selected;
    if(auto fb=o->props.fallback.p)for(auto& m:fb->mappings){
        uint32_t ch=o->text[index];if(!m.ranges.empty()&&std::none_of(m.ranges.begin(),m.ranges.end(),[&](auto r){return ch>=r.first&&ch<=r.second;}))continue;
        for(auto& name:m.families){auto f=select(m.collection.p?m.collection.p:o->props.collection.p,name,a.weight,a.style);if(f&&f->font->glyphIndex(ch))return f;}
    }
    return selected;
}
float advance(Obj* o,size_t index){
    auto f=pick(o,index);if(!f)return 0;auto& a=o->attrs[index];auto& font=*f->font;float n=float(font.glyphAdvance(font.glyphIndex(o->text[index])))*a.em/font.unitsPerEm();
    if(a.tabular&&o->text[index]>=u'0'&&o->text[index]<=u'9')for(char16_t c=u'0';c<=u'9';++c)n=std::max(n,float(font.glyphAdvance(font.glyphIndex(c)))*a.em/font.unitsPerEm());
    return std::max(a.minimum,n+a.leading+a.trailing);
}
std::vector<Line> lines(Obj* o){
    std::vector<Line> out;size_t begin=0,i=0,lastSpace=~size_t(0);float width=0;
    auto push=[&](size_t end){
        float w=0,h=0,b=0;for(size_t p=begin;p<end;++p){if(o->text[p]==u'\r'||o->text[p]==u'\n')continue;w+=advance(o,p);auto f=pick(o,p);if(f){float scale=o->attrs[p].em/f->font->unitsPerEm();h=std::max(h,(f->font->designAscent()+f->font->designDescent()+f->font->designLineGap())*scale);b=std::max(b,f->font->designAscent()*scale);}}
        if(!h){h=o->props.em*1.2f;b=o->props.em;}
        if(o->props.spacing==1){h=o->props.lineSpacing;b=o->props.lineBaseline;}else if(o->props.spacing==2){h*=o->props.lineSpacing;b*=o->props.lineBaseline;}   // uniform, proportional (title 1.43)
        out.push_back({begin,end,w,h,b});begin=end;width=0;lastSpace=~size_t(0);
    };
    while(i<o->text.size()){
        if(o->text[i]==u'\n'){push(i+1);++i;continue;}
        float a=advance(o,i);
        if(o->props.wrap!=1&&width+a>o->width&&i>begin){size_t end=lastSpace!=~size_t(0)&&lastSpace>=begin?lastSpace+1:i;push(end);i=end;continue;}
        width+=a;if(o->text[i]==u' ')lastSpace=i;++i;
    }
    if(begin<o->text.size()||out.empty())push(o->text.size());
    return out;
}
float topOffset(Obj* o,const std::vector<Line>& l){float total=0;for(auto& line:l)total+=line.height;return o->props.paragraph==1?std::max(0.f,o->height-total)/2:o->props.paragraph==2?std::max(0.f,o->height-total):0;}
template<class F>int32_t range(Obj* o,uint64_t span,F f){uint32_t start=uint32_t(span),count=uint32_t(span>>32);if(start>o->attrs.size())return Invalid;auto end=std::min<uint64_t>(o->attrs.size(),uint64_t(start)+count);for(auto p=start;p<end;++p)f(o->attrs[p]);return 0;}
void retainGuest(void* p){if(p)guestCall8((*static_cast<const void* const**>(p))[1],uint64_t(p),0,0,0,0,0,0,0);}
void releaseGuest(void* p){if(p)guestCall8((*static_cast<const void* const**>(p))[2],uint64_t(p),0,0,0,0,0,0,0);}
}
extern "C" int32_t dw_Missing(const char* kind,uint32_t slot){log(std::string("DirectWrite ainda sem metodo: ")+kind+" slot "+std::to_string(slot));return NotImpl;}
extern "C" int32_t dw_Query(void* self,const void* iid,void** out){
    if(!iid||!out)return Invalid;*out=nullptr;auto o=object(self);
    static const uint8_t unknown[]={0,0,0,0,0,0,0,0,0xc0,0,0,0,0,0,0,0x46};
    struct Id {Kind kind;uint8_t bytes[16];};static const Id ids[]={
#include "DwriteIids.inc"
    };
    bool supported=!std::memcmp(iid,unknown,16);
    for(auto& id:ids)if((id.kind==o->kind||(o->kind==Kind::Layout&&id.kind==Kind::Format&&!std::memcmp(id.bytes,"\x9a\x71\x18\x9c",4)))&&!std::memcmp(iid,id.bytes,16))supported=true;
    if(!supported)return NoInterface;add(o);*out=o;return 0;
}
extern "C" uint32_t dw_AddRef(void* s){return ++object(s)->refs;}
extern "C" uint32_t dw_Release(void* s){auto o=object(s);auto n=--o->refs;if(!n)delete o;return n;}
extern "C" int32_t win_DWriteCreateFactory(uint32_t type,const void* iid,void** out){
    if(type>1||!out)return Invalid;auto o=new Obj(Kind::Factory);auto hr=dw_Query(o,iid,out);drop(o);return hr;
}
extern "C" int32_t dw_CreateBuilder(void*,void** out){return result(new Obj(Kind::Builder),out);}
extern "C" int32_t dw_CreateFontSet(void* s,void** out){auto o=new Obj(Kind::FontSet);o->fonts=object(s)->fonts;return result(o,out);}
extern "C" int32_t dw_CreateSystemFallback(void* s,void** out){auto o=new Obj(Kind::Fallback);o->mappings=object(s)->mappings;return result(o,out);}
extern "C" int32_t dw_CreateFallbackBuilder(void*,void** out){return result(new Obj(Kind::FallbackBuilder),out);}
extern "C" int32_t dw_CreateTypography(void*,void** out){return result(new Obj(Kind::Typography),out);}
extern "C" int32_t dw_FontFile(void*,const uint16_t* path,const void*,void** out){
    if(!out||!path)return Invalid;*out=nullptr;
    try {auto f=load(path);if(!f)return int32_t(0x80070002);auto o=new Obj(Kind::File);o->font=f;return result(o,out);}catch(const std::exception& e){log(std::string("DirectWrite: ")+e.what());return NoFont;}
}
extern "C" int32_t dw_AddFile(void* s,void* file){if(!file||object(file)->kind!=Kind::File)return Invalid;object(s)->fonts.push_back(object(file)->font);return 0;}
extern "C" int32_t dw_AddSet(void* s,void* set){if(!set)return Invalid;auto& f=object(set)->fonts;object(s)->fonts.insert(object(s)->fonts.end(),f.begin(),f.end());return 0;}
extern "C" int32_t dw_Collection(void*,void* set,void** out){if(!set)return Invalid;auto o=new Obj(Kind::Collection);o->fonts=object(set)->fonts;return result(o,out);}
extern "C" uint32_t dw_FamilyCount(void* s){return uint32_t(families(object(s)).size());}
extern "C" int32_t dw_Family(void* s,uint32_t index,void** out){auto o=object(s);auto names=families(o);if(index>=names.size())return Invalid;auto family=new Obj(Kind::Family);for(auto& f:o->fonts)if(equal(f->family,names[index]))family->fonts.push_back(f);return result(family,out);}
extern "C" int32_t dw_FindFamily(void* s,const uint16_t* name,uint32_t* index,int32_t* exists){if(!name||!index||!exists)return Invalid;auto f=families(object(s));*index=~0u;*exists=0;for(unsigned n=0;n<f.size();++n)if(equal(f[n],wide(name))){*index=n;*exists=1;break;}return 0;}
extern "C" uint32_t dw_FontCount(void* s){return uint32_t(object(s)->fonts.size());}
extern "C" int32_t dw_FontAt(void* s,uint32_t index,void** out){auto o=object(s);if(index>=o->fonts.size())return Invalid;auto f=new Obj(Kind::Font);f->font=o->fonts[index];return result(f,out);}
extern "C" int32_t dw_MatchingFont(void* s,uint32_t weight,uint32_t,uint32_t style,void** out){auto o=object(s);if(o->fonts.empty())return NoFont;auto f=select(o,o->fonts[0]->family,weight,style);if(!f)return NoFont;auto font=new Obj(Kind::Font);font->font=f;return result(font,out);}
extern "C" int32_t dw_FamilyNames(void* s,void** out){auto o=object(s);if(o->fonts.empty())return NoFont;auto str=new Obj(Kind::Strings);str->string=o->fonts[0]->family;return result(str,out);}
extern "C" int32_t dw_FontFamily(void* s,void** out){auto o=new Obj(Kind::Family);o->fonts.push_back(object(s)->font);return result(o,out);}
extern "C" int32_t dw_Face(void* s,void** out){auto face=object(s)->font->face;add(face);return result(face,out);}
extern "C" uint32_t dw_FontWeight(void* s){auto o=object(s);return o->font?o->font->weight:o->props.weight;}
extern "C" uint32_t dw_FontStyle(void* s){auto o=object(s);return o->font?o->font->style:o->props.style;}
extern "C" uint32_t dw_FontStretch(void*){return 5;}
extern "C" uint32_t dw_Zero(void*){return 0;}
extern "C" uint32_t dw_FaceType(void*){return 1;}
extern "C" void dw_FontMetrics(void* s,void* out){
    auto& f=*object(s)->font->font;
    struct Metrics {uint16_t units,ascent,descent;int16_t gap;uint16_t cap,xHeight;int16_t underline;uint16_t underlineSize;int16_t strike;uint16_t strikeSize;};
    static_assert(sizeof(Metrics)==20);auto em=f.unitsPerEm();
    Metrics m{uint16_t(em),uint16_t(f.designAscent()),uint16_t(f.designDescent()),int16_t(f.designLineGap()),uint16_t(f.glyphBox(f.glyphIndex('H')).top),uint16_t(f.glyphBox(f.glyphIndex('x')).top),int16_t(-em/10),uint16_t(em/20),int16_t(em/3),uint16_t(em/20)};if(out)std::memcpy(out,&m,20);
}
extern "C" int32_t dw_HasChar(void* s,uint32_t ch,int32_t* exists){if(!exists)return Invalid;*exists=object(s)->font->font->glyphIndex(ch)!=0;return 0;}
extern "C" uint16_t dw_GlyphCount(void* s){return uint16_t(object(s)->font->font->glyphCount());}
extern "C" int32_t dw_GlyphIndices(void* s,const uint32_t* chars,uint32_t count,uint16_t* out){if(count&&(!chars||!out))return Invalid;for(unsigned n=0;n<count;++n)out[n]=object(s)->font->font->glyphIndex(chars[n]);return 0;}
extern "C" int32_t dw_GlyphMetrics(void* s,const uint16_t* glyphs,uint32_t count,void* output,int32_t sideways){
    if(sideways)return NotImpl;if(count&&(!glyphs||!output))return Invalid;auto& f=*object(s)->font->font;
    struct Metric {int32_t left;uint32_t advance;int32_t right,top;uint32_t advanceY;int32_t bottom,originY;};auto out=static_cast<Metric*>(output);
    for(unsigned n=0;n<count;++n){auto box=f.glyphBox(glyphs[n]);int advance=f.glyphAdvance(glyphs[n]);out[n]={box.left,uint32_t(advance),advance-box.right,f.designAscent()-box.top,uint32_t(f.designAscent()+f.designDescent()),box.bottom+f.designDescent(),f.designAscent()};}return 0;
}
extern "C" int32_t dw_AddMappings(void* s,void* fallback){if(!fallback)return Invalid;auto& maps=object(fallback)->mappings;object(s)->mappings.insert(object(s)->mappings.end(),maps.begin(),maps.end());return 0;}
extern "C" int32_t dw_AddMapping(void* s,const void* ranges,uint32_t count,const uint16_t* const* names,uint32_t nameCount,void* collection,const uint16_t*,const uint16_t*,float scale){
    if((count&&!ranges)||(nameCount&&!names)||!std::isfinite(scale)||scale<=0)return Invalid;if(scale!=1)return NotImpl;
    Mapping m;m.collection=Ref(object(collection));auto r=static_cast<const uint32_t*>(ranges);for(unsigned n=0;n<count;++n)m.ranges.emplace_back(r[n*2],r[n*2+1]);for(unsigned n=0;n<nameCount;++n)m.families.push_back(wide(names[n]));object(s)->mappings.push_back(std::move(m));return 0;
}
extern "C" int32_t dw_Format(void*,const uint16_t* family,void* collection,uint32_t weight,uint32_t style,uint32_t stretch,float size,const uint16_t* locale,void** out){
    if(!family||!collection||!std::isfinite(size)||size<=0)return Invalid;
    if(!select(object(collection),wide(family),weight,style))return NoFont;
    auto o=new Obj(Kind::Format);o->props.family=wide(family);o->props.collection=Ref(object(collection));o->props.weight=weight;o->props.style=style;o->props.stretch=stretch;o->props.em=size;o->props.locale=wide(locale);return result(o,out);
}
extern "C" int32_t dw_Layout(void*,const uint16_t* text,uint32_t count,void* format,float width,float height,void** out){
    if(!format||(count&&!text)||!std::isfinite(width)||!std::isfinite(height)||width<0||height<0)return Invalid;
    auto o=new Obj(Kind::Layout);o->props=object(format)->props;o->width=width;o->height=height;if(count)o->text.assign(reinterpret_cast<const char16_t*>(text),count);
    Attr attr;attr.em=o->props.em;attr.weight=o->props.weight;attr.style=o->props.style;attr.family=o->props.family;o->attrs.assign(count,attr);return result(o,out);
}
#define PROPERTY(n,field,max) extern "C" int32_t dw_Set##n(void* s,uint32_t v){if(v>max)return NotImpl;object(s)->props.field=v;return 0;} extern "C" uint32_t dw_Get##n(void* s){return object(s)->props.field;}
PROPERTY(TextAlign,align,2) PROPERTY(ParagraphAlign,paragraph,2) PROPERTY(Wrap,wrap,4) PROPERTY(Reading,reading,0) PROPERTY(Flow,flow,0)
#undef PROPERTY
// Line spacing (DWRITE_LINE_SPACING_METHOD default/uniform/proportional), the
// tab stop, and trimming, which is accepted but not drawn (the 1.0.2 engine, title 1.43).
extern "C" int32_t dw_SetLineSpacing(void* s,uint32_t method,float spacing,float baseline){if(method>2||!std::isfinite(spacing)||!std::isfinite(baseline)||spacing<0)return Invalid;auto& p=object(s)->props;p.spacing=method;p.lineSpacing=spacing;p.lineBaseline=baseline;return 0;}
extern "C" int32_t dw_GetLineSpacing(void* s,uint32_t* method,float* spacing,float* baseline){if(!method||!spacing||!baseline)return Invalid;auto& p=object(s)->props;*method=p.spacing;*spacing=p.lineSpacing;*baseline=p.lineBaseline;return 0;}
extern "C" int32_t dw_SetTabStop(void* s,float v){if(!std::isfinite(v)||v<=0)return Invalid;object(s)->props.tabStop=v;return 0;}
extern "C" float dw_GetTabStop(void* s){auto v=object(s)->props.tabStop;return v>0?v:object(s)->props.em*4;}
extern "C" int32_t dw_SetTrimming(void*,const void* options,void*){if(!options)return Invalid;uint32_t granularity;std::memcpy(&granularity,options,4);if(granularity>2)return Invalid;static std::atomic<bool> once{false};if(granularity&&!once.exchange(true))log("DirectWrite: SetTrimming aceito sem cortar o texto");return 0;}
extern "C" int32_t dw_GetTrimming(void*,void* options,void** sign){if(!options||!sign)return Invalid;std::memset(options,0,12);*sign=nullptr;return 0;}
// The 1.0.2 engine's other layout calls (title 1.45): the widest word, the
// locale, and the ellipsis sign trimming takes (an inline object that draws
// nothing, since trimming does not cut).
extern "C" int32_t dw_MinWidth(void* s,float* out){if(!out)return Invalid;auto o=object(s);float best=0,word=0;for(size_t i=0;i<o->text.size();++i){auto c=o->text[i];if(c==u' '||c==u'\n'||c==u'\r'||c==u'\t'){word=0;continue;}word+=advance(o,i);best=std::max(best,word);}*out=best;return 0;}
extern "C" int32_t dw_SetLocale(void* s,const uint16_t* v,uint64_t){if(!v)return Invalid;object(s)->props.locale=wide(v);return 0;}
extern "C" int32_t dw_EllipsisSign(void*,void* format,void** out){if(!format)return Invalid;return result(new Obj(Kind::Sign),out);}
extern "C" int32_t dw_SignMetrics(void*,void* out){if(!out)return Invalid;std::memset(out,0,16);return 0;}
extern "C" int32_t dw_SignOverhang(void*,void* out){if(!out)return Invalid;std::memset(out,0,16);return 0;}
extern "C" int32_t dw_SignBreaks(void*,uint32_t* before,uint32_t* after){if(!before||!after)return Invalid;*before=*after=0;return 0;}
extern "C" int32_t dw_SignDraw(void*){return 0;}
extern "C" int32_t dw_SetFallback(void* s,void* value){object(s)->props.fallback=Ref(object(value));return 0;}
extern "C" int32_t dw_GetFallback(void* s,void** out){if(!out)return Invalid;auto p=object(s)->props.fallback.p;add(p);*out=p;return 0;}
extern "C" int32_t dw_SetFontWeight(void* s,uint32_t v,uint64_t r){return range(object(s),r,[&](Attr& a){a.weight=v;});}
extern "C" int32_t dw_SetFontStyle(void* s,uint32_t v,uint64_t r){return range(object(s),r,[&](Attr& a){a.style=v;});}
extern "C" int32_t dw_SetFontSize(void* s,float v,uint64_t r){if(!std::isfinite(v)||v<=0)return Invalid;return range(object(s),r,[&](Attr& a){a.em=v;});}
extern "C" float dw_GetFontSize(void* s){return object(s)->props.em;}
extern "C" int32_t dw_SetFamily(void* s,const uint16_t* v,uint64_t r){if(!v)return Invalid;return range(object(s),r,[&](Attr& a){a.family=wide(v);});}
extern "C" int32_t dw_SetEffect(void* s,void* v,uint64_t r){retainGuest(v);std::shared_ptr<void> effect(v,releaseGuest);return range(object(s),r,[&](Attr& a){a.effect=effect;});}
extern "C" int32_t dw_Feature(void* s,uint64_t feature){uint32_t tag=uint32_t(feature),value=uint32_t(feature>>32);if(!(tag==0x6d756e74||(value==0&&(tag==0x6e72656b||tag==0x6167696c)))){log("DirectWrite: feature nao suportada "+std::to_string(tag)+"="+std::to_string(value));return NotImpl;}object(s)->features.push_back(feature);return 0;}
extern "C" int32_t dw_SetTypography(void* s,void* v,uint64_t r){if(!v)return range(object(s),r,[](Attr& a){a.tabular=false;});bool tabular=false;for(auto f:object(v)->features)if(uint32_t(f)==0x6d756e74)tabular=(f>>32)!=0;return range(object(s),r,[&](Attr& a){a.tabular=tabular;});}
extern "C" int32_t dw_PairKerning(void*,int32_t enabled,uint64_t){return enabled?NotImpl:0;}
extern "C" int32_t dw_Spacing(void* s,float lead,float trail,float minimum,uint64_t r){return range(object(s),r,[&](Attr& a){a.leading=lead;a.trailing=trail;a.minimum=minimum;});}
extern "C" int32_t dw_SetWidth(void* s,float v){if(v<0||!std::isfinite(v))return Invalid;object(s)->width=v;return 0;}
extern "C" int32_t dw_SetHeight(void* s,float v){if(v<0||!std::isfinite(v))return Invalid;object(s)->height=v;return 0;}
extern "C" float dw_GetWidth(void* s){return object(s)->width;}extern "C" float dw_GetHeight(void* s){return object(s)->height;}
extern "C" int32_t dw_TextMetrics(void* s,void* out){if(!out)return Invalid;auto o=object(s);auto l=lines(o);float width=0,trailing=0,height=0;for(auto& line:l){trailing=std::max(trailing,line.width);float n=line.width;for(auto i=line.last;i>line.first&&o->text[i-1]==u' ';--i)n-=advance(o,i-1);width=std::max(width,n);height+=line.height;}TextMetrics m{0,topOffset(o,l),width,trailing,height,o->width,o->height,1,uint32_t(l.size())};std::memcpy(out,&m,sizeof m);return 0;}
extern "C" int32_t dw_Overhang(void*,void* out){if(!out)return Invalid;std::memset(out,0,16);return 0;}
extern "C" int32_t dw_LineMetrics(void* s,void* out,uint32_t capacity,uint32_t* actual){if(!actual)return Invalid;auto o=object(s);auto l=lines(o);*actual=uint32_t(l.size());if(capacity<l.size())return Small;if(!out)return Invalid;struct Metric{uint32_t length,space,newline;float height,baseline;int32_t trimmed;};auto m=static_cast<Metric*>(out);for(size_t n=0;n<l.size();++n){auto& line=l[n];uint32_t spaces=0;for(size_t p=line.last;p>line.first&&o->text[p-1]==u' ';--p)++spaces;m[n]={uint32_t(line.last-line.first),spaces,uint32_t(line.last>line.first&&o->text[line.last-1]==u'\n'),line.height,line.baseline,0};}return 0;}
extern "C" int32_t dw_ClusterMetrics(void* s,void* out,uint32_t capacity,uint32_t* actual){if(!actual)return Invalid;auto o=object(s);*actual=uint32_t(o->text.size());if(capacity<o->text.size())return Small;if(!out&&capacity)return Invalid;struct Metric{float width;uint16_t length,flags;};auto m=static_cast<Metric*>(out);for(size_t i=0;i<o->text.size();++i)m[i]={advance(o,i),1,uint16_t(o->text[i]==u' '?5:o->text[i]==u'\n'?9:1)};return 0;}
extern "C" int32_t dw_Draw(void* s,void* context,void* renderer,float x,float y){
    if(!renderer)return Invalid;auto o=object(s);auto l=lines(o);float top=y+topOffset(o,l);
    for(auto& line:l){float pen=x+(o->props.align==2?(o->width-line.width)/2:o->props.align==1?o->width-line.width:0);
        for(size_t i=line.first;i<line.last;){if(o->text[i]==u'\r'||o->text[i]==u'\n'){++i;continue;}
            auto f=pick(o,i);if(!f)return NoFont;auto first=i;auto effect=o->attrs[i].effect;float em=o->attrs[i].em;std::vector<uint16_t> indices,clusters;std::vector<float> widths;std::vector<Offset> offsets;
            while(i<line.last&&o->text[i]!=u'\r'&&o->text[i]!=u'\n'&&pick(o,i)==f&&o->attrs[i].em==em&&o->attrs[i].effect==effect){indices.push_back(f->font->glyphIndex(o->text[i]));clusters.push_back(uint16_t(indices.size()-1));widths.push_back(advance(o,i));offsets.push_back({o->attrs[i].leading,0});++i;}
            GlyphRun run{f->face,em,uint32_t(indices.size()),indices.data(),widths.data(),offsets.data(),0,0};
            struct Description {const char16_t* locale;const char16_t* text;uint32_t count,pad;const uint16_t* clusters;uint32_t position,pad2;};Description d{o->props.locale.c_str(),o->text.data()+first,uint32_t(i-first),0,clusters.data(),uint32_t(first),0};
            auto hr=dwRenderGlyphRun(renderer,context,pen,top+line.baseline,&run,&d,effect.get());if(hr<0)return hr;for(auto w:widths)pen+=w;
        }
        top+=line.height;
    }
    return 0;
}
// IDWriteFactory4::ComputeGlyphOrigins: each glyph's pen position from the
// baseline origin, advancing left to right (right to left for odd bidi levels),
// with the run's offsets (the 1.0.2 engine's text drawing, title 1.44). The
// transform only matters for GDI-compatible rounding and is ignored.
extern "C" int32_t dw_GlyphOrigins(const void* value,float x,float y,float* origins){
    if(!value)return Invalid;auto& run=*static_cast<const GlyphRun*>(value);if(!run.count)return 0;
    if(!origins||!run.indices||(!run.advances&&!run.face))return Invalid;
    for(unsigned n=0;n<run.count;++n){
        float advance=run.advances?run.advances[n]:object(run.face)->font->font->glyphAdvance(run.indices[n])*run.em/object(run.face)->font->font->unitsPerEm();
        Offset off=run.offsets?run.offsets[n]:Offset{};bool rtl=run.bidi%2;
        if(rtl)x-=advance;
        origins[2*n]=x+(rtl?-off.advance:off.advance);origins[2*n+1]=y-off.ascender;
        if(!rtl)x+=advance;
    }
    return 0;
}
extern "C" int32_t dw_Analysis(void*,const void* value,float ppd,const void* matrix,uint32_t,uint32_t,float x,float y,void** out){
    if(!value||!out||!std::isfinite(ppd)||ppd<=0)return Invalid;*out=nullptr;auto& run=*static_cast<const GlyphRun*>(value);
    if(!run.face||(run.count&&!run.indices)||run.sideways||run.bidi%2)return NotImpl;
    float scaleX=ppd,scaleY=ppd,dx=0,dy=0;if(matrix){auto m=static_cast<const float*>(matrix);if(m[1]||m[2]||m[0]!=m[3])return NotImpl;scaleX*=m[0];scaleY*=m[3];dx=m[4];dy=m[5];}
    if(scaleX<=0)return Invalid;auto face=object(run.face);auto f=face->font;float scale=run.em*scaleX/f->font->unitsPerEm();x=x*scaleX+dx;y=y*scaleY+dy;
    float left=x,top=y,right=x,bottom=y,pen=x;std::vector<uint16_t> ids;std::vector<float> widths;std::vector<azimuth::gfx::Font::Offset> offsets;
    for(unsigned n=0;n<run.count;++n){auto g=run.indices[n];auto b=f->font->glyphBox(g);auto off=run.offsets?run.offsets[n]:Offset{};off.advance*=scaleX;off.ascender*=scaleY;
        left=std::min(left,pen+off.advance+b.left*scale);right=std::max(right,pen+off.advance+b.right*scale);top=std::min(top,y-off.ascender-b.top*scale);bottom=std::max(bottom,y-off.ascender-b.bottom*scale);
        float w=run.advances?run.advances[n]*scaleX:f->font->glyphAdvance(g)*scale;ids.push_back(g);widths.push_back(w);offsets.push_back({off.advance,off.ascender});pen+=w;}
    auto o=new Obj(Kind::Analysis);o->bounds[0]=int32_t(std::floor(left));o->bounds[1]=int32_t(std::floor(top));o->bounds[2]=int32_t(std::ceil(right));o->bounds[3]=int32_t(std::ceil(bottom));
    int width=o->bounds[2]-o->bounds[0],height=o->bounds[3]-o->bounds[1];if(width<0||height<0||width>8192||height>8192){drop(o);return Invalid;}
    o->coverage.resize(size_t(width)*height);azimuth::gfx::Font::Size size{scale,0,0};
    if(width&&height)f->font->rasterizeGlyphs(ids,widths,offsets,size,x-o->bounds[0],y-o->bounds[1],o->coverage,width,height);
    return result(o,out);
}
extern "C" int32_t dw_Analysis2(void* s,const void* run,const void* matrix,uint32_t rendering,uint32_t measuring,uint32_t,uint32_t,float x,float y,void** out){return dw_Analysis(s,run,1,matrix,rendering,measuring,x,y,out);}
extern "C" int32_t dw_TextureBounds(void* s,uint32_t type,int32_t* rect){if(!rect||type>1)return Invalid;std::memcpy(rect,object(s)->bounds,16);return 0;}
extern "C" int32_t dw_Texture(void* s,uint32_t type,const int32_t* rect,uint8_t* bytes,uint32_t size){
    if(!rect||!bytes||type>1||rect[2]<rect[0]||rect[3]<rect[1])return Invalid;uint64_t width=uint64_t(rect[2]-rect[0]),height=uint64_t(rect[3]-rect[1]),channels=type?3:1;if(width*height*channels>size)return Small;
    std::memset(bytes,0,size);auto o=object(s);int stride=o->bounds[2]-o->bounds[0];
    for(int y=std::max(rect[1],o->bounds[1]);y<std::min(rect[3],o->bounds[3]);++y)for(int x=std::max(rect[0],o->bounds[0]);x<std::min(rect[2],o->bounds[2]);++x){auto a=uint8_t(std::clamp(std::lround(o->coverage[size_t(y-o->bounds[1])*stride+x-o->bounds[0]]*255),0l,255l));auto offset=(uint64_t(y-rect[1])*width+x-rect[0])*channels;for(unsigned c=0;c<channels;++c)bytes[offset+c]=a;}return 0;
}
extern "C" uint32_t dw_StringCount(void*){return 1;}
extern "C" int32_t dw_FindLocale(void*,const uint16_t* name,uint32_t* index,int32_t* exists){if(!index||!exists)return Invalid;*index=0;*exists=equal(wide(name),u"en-US");return 0;}
extern "C" int32_t dw_LocaleLength(void*,uint32_t index,uint32_t* size){if(index||!size)return Invalid;*size=5;return 0;}
extern "C" int32_t dw_Locale(void*,uint32_t index,uint16_t* text,uint32_t size){if(index||!text)return Invalid;if(size<6)return Small;std::memcpy(text,u"en-US",12);return 0;}
extern "C" int32_t dw_StringLength(void* s,uint32_t index,uint32_t* size){if(index||!size)return Invalid;*size=uint32_t(object(s)->string.size());return 0;}
extern "C" int32_t dw_String(void* s,uint32_t index,uint16_t* text,uint32_t size){if(index||!text)return Invalid;auto& value=object(s)->string;if(size<=value.size())return Small;std::memcpy(text,value.c_str(),(value.size()+1)*2);return 0;}
