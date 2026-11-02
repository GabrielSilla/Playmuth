#include "Image.h"
#include "Platform.h"
#include <algorithm>
#include <cstring>

namespace azimuth {
namespace {
enum : uint32_t {SectionExecute=0x20000000,SectionRead=0x40000000,SectionWrite=0x80000000};
}
Image::Image(const PeImage& image,const Resolver& resolve,uint32_t staticTlsIndex,bool sharedBlocks,bool allowWritableExecutable) {
    if(!image.certificateTableInFile)throw std::runtime_error("Imagem de auditoria com metadados de certificado inconsistentes; execucao recusada");
    if(image.directories[13].size)throw std::runtime_error("Delay imports ainda nao integrados");
    if(image.directories[14].size)throw std::runtime_error("Imagem CLR nao suportada");
    if(image.sectionAlignment<4096||image.sectionAlignment%4096||image.imageSize%4096)
        throw std::runtime_error("Layout de secoes nao suportado pelo mapeador");
    for(auto const& s:image.sections)if(s.rva%4096)throw std::runtime_error("Secao nao alinhada em pagina");
    size_=image.imageSize;entry_=image.entryPoint;sections_=image.sections;exceptions_=image.directories[3];
    // The preferred base keeps engine+offset equal to the analysed addresses;
    // anywhere else works too, through the relocations.
    const size_t granule=platform::protectionGranularity();
    reserved_=(size_+granule-1)/granule*granule;
    memory_=platform::reserve(image.imageBase,reserved_);
    try {
        auto mapped=image.mappedBytes(base());
        if(image.directories[9].size) {
            auto d=image.directories[9];if(d.size<40)throw std::runtime_error("Diretorio TLS64 truncado");
            uint64_t fields[4];uint32_t tail[2];std::memcpy(fields,mapped.data()+d.rva,32);std::memcpy(tail,mapped.data()+d.rva+32,8);
            auto rva=[&](uint64_t va,size_t length) {
                if(va<base()||va-base()>size_||length>size_-(va-base()))throw std::runtime_error("Ponteiro TLS fora da imagem");
                return uint32_t(va-base());
            };
            if(fields[1]<fields[0]||fields[1]-fields[0]>4096||tail[0]>4096-(fields[1]-fields[0]))throw std::runtime_error("Template TLS excede o slot do motor");
            tlsBytes_=uint32_t(fields[1]-fields[0]);tlsZero_=tail[0];tlsStart_=rva(fields[0],tlsBytes_);
            auto indexRva=rva(fields[2],4);bool writable=false;
            for(auto const& s:image.sections)
                if(indexRva>=s.rva&&uint64_t(indexRva)+4<=uint64_t(s.rva)+std::max(s.virtualSize,s.fileSize)&&(s.characteristics&SectionWrite))writable=true;
            if(!writable)throw std::runtime_error("Indice TLS nao esta em secao gravavel");
            std::memcpy(mapped.data()+indexRva,&staticTlsIndex,4);
            if(fields[3])tlsCallbacksRva_=rva(fields[3],8);
        }
        for(unsigned n=0;n<image.imports.size();++n) {
            auto const& item=image.imports[n];
            auto address=resolve?resolve(item,n):nullptr;
            if(!address)throw std::runtime_error("Import sem destino: "+item.library+"!"+(item.byOrdinal?"#"+std::to_string(item.ordinal):item.symbol));
            auto value=reinterpret_cast<uint64_t>(address);std::memcpy(mapped.data()+item.iatRva,&value,8);
        }
        std::memcpy(memory_,mapped.data(),mapped.size());
        // Per-page permissions from the sections; ranges sharing a page get both.
        std::vector<unsigned char> pages(size_/4096,0);
        for(size_t n=0;n<(image.headerSize+4095ull)/4096;++n)pages[n]|=platform::Read;
        for(auto const& s:image.sections) {
            unsigned char bits=0;
            if(s.characteristics&SectionRead)bits|=platform::Read;
            if(s.characteristics&SectionWrite)bits|=platform::Read|platform::Write;
            if(s.characteristics&SectionExecute)bits|=platform::Execute;
            auto extent=std::max(s.virtualSize,s.fileSize);
            for(size_t n=s.rva/4096;n<(s.rva+uint64_t(extent)+4095)/4096;++n)pages.at(n)|=bits;
        }
        for(auto bits:pages)
            if((bits&(platform::Write|platform::Execute))==(platform::Write|platform::Execute))
                throw std::runtime_error("Imagem requer pagina gravavel e executavel; recusada");
        // The protection is set by blocks of `granule` bytes; pages past the
        // image's end (its last block) are not accessible.
        const size_t perBlock=granule/4096;
        std::vector<unsigned char> blocks((pages.size()+perBlock-1)/perBlock,0);
        for(size_t b=0;b<blocks.size();++b) {
            bool different=false;
            for(size_t n=b*perBlock;n<pages.size()&&n<(b+1)*perBlock;++n) {
                blocks[b]|=pages[n];
                if(pages[n]!=pages[b*perBlock])different=true;
            }
            if(!different)continue;
            if(!sharedBlocks)throw std::runtime_error("Bloco de protecao com secoes de protecoes diferentes; recusado");
            mixedBlocks_.push_back(uint32_t(b*granule));
            if((blocks[b]&(platform::Write|platform::Execute))==(platform::Write|platform::Execute)) {
                if(!allowWritableExecutable)throw std::runtime_error("Bloco de protecao ficaria gravavel e executavel; recusado");
                ++wxBlocks_;
            }
        }
        for(size_t n=0;n<blocks.size();) {
            size_t run=n;while(run<blocks.size()&&blocks[run]==blocks[n])++run;
            platform::protect(static_cast<uint8_t*>(memory_)+n*granule,(run-n)*granule,blocks[n]);
            n=run;
        }
        // A DLL without DllMain has no entry point (0), as Windows allows.
        if(entry_&&!(pages.at(entry_/4096)&platform::Execute))throw std::runtime_error("Entry point nao esta em pagina executavel");
    }catch(...){platform::release(memory_,reserved_);memory_=nullptr;throw;}
}
Image::~Image(){platform::release(memory_,reserved_);}
void Image::initializeStaticTls(void* block,size_t capacity) const {
    if(!tlsBytes_&&!tlsZero_)return;
    if(!block||capacity<tlsBytes_+tlsZero_)throw std::runtime_error("Bloco TLS insuficiente");
    std::memset(block,0,capacity);
    for(auto const& s:sections_)if(tlsStart_>=s.rva&&uint64_t(tlsStart_)+tlsBytes_<=uint64_t(s.rva)+std::max(s.virtualSize,s.fileSize)) {
        if(!(s.characteristics&SectionRead))throw std::runtime_error("Template TLS nao legivel");
        std::memcpy(block,static_cast<uint8_t*>(memory_)+tlsStart_,tlsBytes_);return;
    }
    throw std::runtime_error("Template TLS sem secao correspondente");
}
std::vector<void*> Image::tlsCallbacks() const {
    std::vector<void*> callbacks;if(!tlsCallbacksRva_)return callbacks;
    for(unsigned n=0;n<64;++n) {
        uint64_t at=uint64_t(tlsCallbacksRva_)+n*8;
        if(at>size_||8>size_-at)throw std::runtime_error("Array de callbacks TLS fora da imagem");
        bool readable=false;
        for(auto const& s:sections_)if(at>=s.rva&&at+8<=uint64_t(s.rva)+std::max(s.virtualSize,s.fileSize)&&(s.characteristics&SectionRead))readable=true;
        if(!readable)throw std::runtime_error("Array de callbacks TLS nao legivel");
        uint64_t address;std::memcpy(&address,static_cast<uint8_t*>(memory_)+at,8);if(!address)return callbacks;
        bool executable=false;
        for(auto const& s:sections_)if(address>=base()+s.rva&&address<base()+s.rva+std::max(s.virtualSize,s.fileSize)&&(s.characteristics&SectionExecute))executable=true;
        if(!executable)throw std::runtime_error("Callback TLS fora do codigo do motor");
        callbacks.push_back(reinterpret_cast<void*>(address));
    }
    throw std::runtime_error("Array de callbacks TLS sem terminador");
}
}
