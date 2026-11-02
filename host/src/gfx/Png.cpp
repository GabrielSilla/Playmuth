#include "Png.h"
#include <cstdio>

namespace azimuth::gfx {
namespace {
uint32_t crcTable[256];
bool crcReady=false;
uint32_t crc(const uint8_t* data,size_t size,uint32_t value=0xffffffff) {
    if(!crcReady) {
        for(uint32_t n=0;n<256;++n){uint32_t c=n;for(int k=0;k<8;++k)c=(c&1)?0xedb88320^(c>>1):c>>1;crcTable[n]=c;}
        crcReady=true;
    }
    for(size_t n=0;n<size;++n)value=crcTable[(value^data[n])&0xff]^(value>>8);
    return value;
}
void be32(std::vector<uint8_t>& out,uint32_t v){out.push_back(uint8_t(v>>24));out.push_back(uint8_t(v>>16));out.push_back(uint8_t(v>>8));out.push_back(uint8_t(v));}
void chunk(std::vector<uint8_t>& out,const char* type,const std::vector<uint8_t>& body) {
    be32(out,uint32_t(body.size()));
    size_t start=out.size();out.insert(out.end(),type,type+4);out.insert(out.end(),body.begin(),body.end());
    be32(out,crc(out.data()+start,out.size()-start)^0xffffffff);
}
}
bool savePng(const std::string& path,const std::vector<uint32_t>& pixels,int width,int height) {
    if(width<=0||height<=0||pixels.size()<size_t(width)*size_t(height))return false;
    // Scanlines: filter byte 0, then R,G,B.
    std::vector<uint8_t> raw;raw.reserve(size_t(height)*(size_t(width)*3+1));
    for(int y=0;y<height;++y) {
        raw.push_back(0);
        for(int x=0;x<width;++x){uint32_t p=pixels[size_t(y)*width+x];raw.push_back(uint8_t(p>>16));raw.push_back(uint8_t(p>>8));raw.push_back(uint8_t(p));}
    }
    // zlib stream of stored deflate blocks (at most 65535 bytes each), Adler-32 at the end.
    std::vector<uint8_t> z{0x78,0x01};
    uint32_t a=1,b=0;
    for(size_t at=0;at<raw.size()||raw.empty();) {
        size_t n=raw.size()-at;if(n>65535)n=65535;
        bool last=at+n==raw.size();
        z.push_back(last?1:0);
        z.push_back(uint8_t(n));z.push_back(uint8_t(n>>8));z.push_back(uint8_t(~n));z.push_back(uint8_t(~n>>8));
        z.insert(z.end(),raw.begin()+long(at),raw.begin()+long(at+n));
        for(size_t k=at;k<at+n;++k){a=(a+raw[k])%65521;b=(b+a)%65521;}
        at+=n;if(last)break;
    }
    be32(z,(b<<16)|a);
    std::vector<uint8_t> file{0x89,'P','N','G','\r','\n',0x1a,'\n'};
    std::vector<uint8_t> header;be32(header,uint32_t(width));be32(header,uint32_t(height));
    header.insert(header.end(),{8,2,0,0,0});   // 8-bit RGB, deflate, no filter set, no interlace
    chunk(file,"IHDR",header);chunk(file,"IDAT",z);chunk(file,"IEND",{});
    FILE* out=std::fopen(path.c_str(),"wb");if(!out)return false;
    bool ok=std::fwrite(file.data(),1,file.size(),out)==file.size();
    return std::fclose(out)==0&&ok;
}
}
