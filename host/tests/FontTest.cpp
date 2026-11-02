// Renders lines of text with gfx::Font into a PNG, to check the rasterizer
// and the PNG writer by eye: FontTest <font.ttf> <bold.ttf> <out.png>
#include "../src/gfx/Font.h"
#include "../src/gfx/Png.h"
#include <cmath>
#include <cstdio>

using namespace azimuth::gfx;

int main(int argc,char** argv) {
    if(argc!=4){std::fprintf(stderr,"uso: FontTest fonte.ttf negrito.ttf saida.png\n");return 2;}
    auto regular=Font::load(argv[1]),bold=Font::load(argv[2]);
    if(!regular||!bold){std::fprintf(stderr,"fonte nao carregada\n");return 1;}
    const int width=640,height=220;
    std::vector<uint32_t> pixels(size_t(width)*height,0x202830);
    struct Line {std::shared_ptr<Font> font;int gdiHeight;std::u16string text;uint32_t color;};
    std::vector<Line> lines{
        {bold,-32,u"Forza Horizon 2",0xffffff},
        {regular,-16,u"Preparing graphics pipelines: 1234 of 2344",0xd0d0d0},
        {regular,-13,u"E:\\PC\\FH2\\FH2_Anthem_1.0.0.64_x64__8wekyb3d8bbwe",0x90c0ff},
        {regular,20,u"Iniciar  \u00c1\u00e7\u00e3o  \u00ea  \u00fc  @#%&",0xffd060}};
    int top=12;
    for(auto const& line:lines) {
        auto size=line.font->size(line.gdiHeight);
        int total=0;for(int a:line.font->advances(line.text,size))total+=a;
        std::vector<float> coverage(size_t(width)*size.cell,0.f);
        line.font->rasterize(line.text,size,16,float(size.ascent),coverage,width,size.cell);
        for(int y=0;y<size.cell&&top+y<height;++y)for(int x=0;x<width;++x) {
            float c=coverage[size_t(y)*width+x];if(c<=0)continue;
            uint32_t& p=pixels[size_t(top+y)*width+x];uint32_t out=0;
            for(int s=0;s<24;s+=8){int d=(p>>s)&0xff,t=(line.color>>s)&0xff;out|=uint32_t(d+int(std::lround((t-d)*c)))<<s;}
            p=out;
        }
        std::printf("altura %d: celula %d, ascent %d, largura %d\n",line.gdiHeight,size.cell,size.ascent,total);
        top+=size.cell+10;
    }
    if(!savePng(argv[3],pixels,width,height)){std::fprintf(stderr,"PNG nao gravado\n");return 1;}
    std::printf("PNG gravado: %s\n",argv[3]);
    return 0;
}
