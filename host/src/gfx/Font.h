#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// A TrueType font (glyf outlines) read from a .ttf file and rasterized with
// grayscale coverage: the GDI text of the engine's launcher, without
// DirectWrite. No hinting and no kerning (GDI applies none by default);
// advances are rounded to whole pixels, as GDI's are.
namespace azimuth::gfx {
class Font {
public:
    static std::shared_ptr<Font> load(const std::string& path);
    // Metrics for a GDI height: negative is the em size in pixels, positive
    // the cell height (Windows ascent + descent).
    struct Size {float scale=0;int ascent=0,cell=0;};
    Size size(int gdiHeight) const;
    // Advance of each UTF-16 unit in whole pixels.
    std::vector<int> advances(const std::u16string& text,const Size& size) const;
    // Adds the coverage (0..1) of `text`, its baseline at y = baseline, its
    // pen starting at x, to a width x height buffer.
    void rasterize(const std::u16string& text,const Size& size,float x,float baseline,std::vector<float>& coverage,int width,int height) const;
    uint16_t glyphIndex(uint32_t value) const{return value<=65535?glyph(char16_t(value)):0;}
    int glyphAdvance(uint16_t value) const{return advance(value);}
    int unitsPerEm() const{return unitsPerEm_;}
    int glyphCount() const{return glyphs_;}
    int designAscent() const{return designAscent_;}
    int designDescent() const{return designDescent_;}
    int designLineGap() const{return designLineGap_;}
    struct GlyphBox {int left=0,bottom=0,right=0,top=0;};
    GlyphBox glyphBox(uint16_t glyph) const;
    struct Offset {float advance=0,ascender=0;};
    void rasterizeGlyphs(const std::vector<uint16_t>& glyphs,const std::vector<float>& advances,const std::vector<Offset>& offsets,const Size& size,float x,float baseline,std::vector<float>& coverage,int width,int height) const;
private:
    std::vector<uint8_t> data_;
    uint32_t glyf_=0,loca_=0,hmtx_=0,cmap_=0;
    int designAscent_=0,designDescent_=0,designLineGap_=0;
    int unitsPerEm_=2048,longLoca_=0,metrics_=0,glyphs_=0,winAscent_=0,winDescent_=0;
    uint16_t glyph(char16_t c) const;
    int advance(uint16_t glyph) const;
    struct Point {float x,y;bool on;};
    void outline(uint16_t glyph,float a,float b,float c,float d,float dx,float dy,std::vector<std::vector<Point>>& contours,int depth) const;
};
}
