#include "PeImage.h"
#include <algorithm>
#include <cstring>
#include <sstream>
#include <limits>
#include <iomanip>

namespace azimuth {
namespace {
template<class T> T read(const Bytes& b, size_t at) {
    if (at > b.size() || sizeof(T) > b.size() - at) throw std::runtime_error("PE truncado");
    T v; std::memcpy(&v, b.data() + at, sizeof v); return v;
}
void range(uint64_t at, uint64_t length, uint64_t limit, const char* why) {
    if (at > limit || length > limit - at) throw std::runtime_error(why);
}
uint32_t add(uint32_t a, uint64_t b) {
    if (b > UINT32_MAX - a) throw std::runtime_error("Overflow de RVA");
    return a + static_cast<uint32_t>(b);
}
}
PeImage::PeImage(Bytes bytes,PeReadMode mode) : bytes_(std::move(bytes)) {
    if (read<uint16_t>(bytes_,0) != 0x5a4d) throw std::runtime_error("Assinatura DOS invalida");
    size_t pe=read<uint32_t>(bytes_,0x3c);
    range(pe,24,bytes_.size(),"Cabecalho PE ausente");
    if (read<uint32_t>(bytes_,pe)!=0x4550) throw std::runtime_error("Assinatura PE invalida");
    if (read<uint16_t>(bytes_,pe+4)!=0x8664) throw std::runtime_error("Somente AMD64/x64 e aceito");
    auto count=read<uint16_t>(bytes_,pe+6), optionalSize=read<uint16_t>(bytes_,pe+20);
    if (!count || count>96 || optionalSize<112) throw std::runtime_error("Cabecalho opcional invalido");
    size_t o=pe+24;
    range(o,optionalSize,bytes_.size(),"Cabecalho opcional truncado");
    if (read<uint16_t>(bytes_,o)!=0x20b) throw std::runtime_error("PE32+ obrigatorio");
    entryPoint=read<uint32_t>(bytes_,o+16); imageBase=read<uint64_t>(bytes_,o+24);
    sectionAlignment=read<uint32_t>(bytes_,o+32);
    imageSize=read<uint32_t>(bytes_,o+56); headerSize=read<uint32_t>(bytes_,o+60);
    subsystem=read<uint16_t>(bytes_,o+68);
    if (!imageSize || imageSize>512u*1024*1024 || !headerSize || headerSize>imageSize || !sectionAlignment)
        throw std::runtime_error("Tamanho de imagem fora do limite de auditoria (512 MiB)");
    range(0,headerSize,bytes_.size(),"Cabecalhos fora do arquivo");
    range(entryPoint,1,imageSize,"Entry point fora da imagem");
    auto nd=read<uint32_t>(bytes_,o+108);
    if (nd>16 || 112ull+nd*8ull>optionalSize) throw std::runtime_error("Diretorios PE invalidos");
    for (uint32_t n=0;n<nd;++n) {
        auto& d=directories[n]; d={read<uint32_t>(bytes_,o+112+n*8),read<uint32_t>(bytes_,o+116+n*8)};
        // The security directory contains a file offset, not an RVA.
        if (d.size) {
            uint64_t limit=n==4?bytes_.size():imageSize;
            if(d.rva>limit || d.size>limit-d.rva) {
                std::ostringstream why;
                why<<"Diretorio fora da imagem: indice="<<n<<" endereco=0x"<<std::hex<<d.rva
                   <<" tamanho=0x"<<d.size<<" limite=0x"<<limit<<(n==4?" (offset de arquivo)":" (RVA)");
                if(n==4 && mode==PeReadMode::AuditUnmappedMetadata) {
                    certificateTableInFile=false;
                    warnings.push_back(why.str());
                    warnings.push_back("Tabela de certificados nao interpretada. Apenas auditoria de secoes/imports; nao autoriza execucao nem verifica assinatura.");
                    continue;
                }
                throw std::runtime_error(why.str());
            }
        }
    }
    size_t st=o+optionalSize; range(st,count*40ull,bytes_.size(),"Tabela de secoes truncada");
    for (unsigned n=0;n<count;++n) {
        size_t s=st+n*40; Section v;
        for (unsigned j=0;j<8 && bytes_[s+j];++j) v.name+=static_cast<char>(bytes_[s+j]);
        v.virtualSize=read<uint32_t>(bytes_,s+8); v.rva=read<uint32_t>(bytes_,s+12);
        v.fileSize=read<uint32_t>(bytes_,s+16); v.fileOffset=read<uint32_t>(bytes_,s+20);
        v.characteristics=read<uint32_t>(bytes_,s+36);
        auto extent=std::max(v.virtualSize,v.fileSize);
        range(v.rva,extent,imageSize,"Secao fora da imagem");
        range(v.fileOffset,v.fileSize,bytes_.size(),"Secao fora do arquivo");
        if (extent && v.rva<headerSize) throw std::runtime_error("Secao sobrepoe cabecalhos");
        for (const auto& prior: sections) {
            uint64_t e=uint64_t(prior.rva)+std::max(prior.virtualSize,prior.fileSize);
            if (extent && e>v.rva && uint64_t(v.rva)+extent>prior.rva)
                throw std::runtime_error("Secoes virtuais sobrepostas");
        }
        if ((v.characteristics&0xa0000000)==0xa0000000) warnings.push_back("Secao RWX: "+v.name);
        sections.push_back(v);
    }
    parseImports();
    if (directories[9].size) warnings.push_back("TLS requer integracao com threads e callbacks do host");
    if (directories[3].size) warnings.push_back("Unwind x64 requer registro e remocao da tabela de funcoes");
    if (directories[13].size) warnings.push_back("Delay imports ainda requerem resolucao no runtime");
    if (directories[10].size) warnings.push_back("Load config/CFG precisam ser auditados");
}
size_t PeImage::fileAt(uint32_t rva,size_t length) const {
    if (rva<headerSize) {range(rva,length,headerSize,"RVA cruza os cabecalhos");return rva;}
    for (const auto& s:sections) if(rva>=s.rva && uint64_t(rva)-s.rva<s.fileSize) {
        range(uint64_t(rva)-s.rva,length,s.fileSize,"RVA sem bytes no arquivo");
        return s.fileOffset+size_t(rva-s.rva);
    }
    throw std::runtime_error("RVA nao pertence a dados de arquivo");
}
std::string PeImage::stringAt(uint32_t rva) const {
    std::string out;
    for(unsigned n=0;n<4096;++n) {
        char c=static_cast<char>(bytes_[fileAt(add(rva,n),1)]);
        if (!c) return out;
        out+=c;
    }
    throw std::runtime_error("String PE sem terminador");
}
void PeImage::parseImports() {
    const auto d=directories[1]; if(!d.size)return;
    bool ended=false;
    for(uint32_t pos=0;uint64_t(pos)+20<=d.size;pos+=20) {
        size_t p=fileAt(add(d.rva,pos),20);
        auto names=read<uint32_t>(bytes_,p), name=read<uint32_t>(bytes_,p+12), iat=read<uint32_t>(bytes_,p+16);
        if(!names&&!name&&!iat && !read<uint32_t>(bytes_,p+4) && !read<uint32_t>(bytes_,p+8)){ended=true;break;}
        if(!name||!iat)throw std::runtime_error("Descritor de importacao incompleto");
        auto dll=stringAt(name); uint32_t table=names?names:iat; bool thunkEnd=false;
        for(uint32_t n=0;n<65536;++n) {
            auto thunk=read<uint64_t>(bytes_,fileAt(add(table,n*8ull),8));
            if(!thunk){thunkEnd=true;break;}
            Import item;item.library=dll;item.iatRva=add(iat,n*8ull);range(item.iatRva,8,imageSize,"IAT fora da imagem");
            item.byOrdinal=(thunk>>63)!=0;
            if(item.byOrdinal)item.ordinal=static_cast<uint16_t>(thunk);
            else {if(thunk>UINT32_MAX)throw std::runtime_error("Import name RVA invalido");item.symbol=stringAt(add(uint32_t(thunk),2));}
            imports.push_back(std::move(item));
        }
        if(!thunkEnd)throw std::runtime_error("Tabela de imports sem terminador");
    }
    if(!ended)throw std::runtime_error("Diretorio de imports sem terminador");
}
Bytes PeImage::mappedBytes(uint64_t newBase) const {
    Bytes mapped(imageSize,0);
    std::copy_n(bytes_.begin(),headerSize,mapped.begin());
    for(const auto& s:sections)std::copy_n(bytes_.begin()+s.fileOffset,s.fileSize,mapped.begin()+s.rva);
    if(newBase==imageBase)return mapped;
    auto d=directories[5];if(!d.size)throw std::runtime_error("Imagem sem relocacoes; base preferida obrigatoria");
    uint32_t consumed=0;
    while(consumed<d.size) {
        range(consumed,8,d.size,"Bloco de relocacao truncado");
        size_t p=add(d.rva,consumed);
        auto page=read<uint32_t>(mapped,p),size=read<uint32_t>(mapped,p+4);
        if(size<8 || size%2)throw std::runtime_error("Bloco de relocacao invalido");
        range(consumed,size,d.size,"Bloco de relocacao fora do diretorio");
        for(uint32_t k=8;k<size;k+=2) {
            auto reloc=read<uint16_t>(mapped,p+k); auto type=reloc>>12;
            if(!type)continue;
            if(type!=10)throw std::runtime_error("Relocacao diferente de AMD64 DIR64");
            auto target=add(page,reloc&4095);range(target,8,mapped.size(),"Relocacao fora da imagem");
            uint64_t value=read<uint64_t>(mapped,target)+newBase-imageBase;
            std::memcpy(mapped.data()+target,&value,8);
        }
        consumed+=size;
    }
    return mapped;
}
std::string PeImage::headerReport(const Bytes& b) {
    std::ostringstream out;out<<"[Cabecalhos brutos / auditoria estrutural]\nArquivo: "<<b.size()<<" bytes\n";
    try {
        if(read<uint16_t>(b,0)!=0x5a4d)throw std::runtime_error("Assinatura DOS invalida");
        size_t pe=read<uint32_t>(b,0x3c);range(pe,24,b.size(),"Cabecalho PE truncado");
        if(read<uint32_t>(b,pe)!=0x4550)throw std::runtime_error("Assinatura PE invalida");
        size_t o=pe+24;auto optionalSize=read<uint16_t>(b,pe+20);
        range(o,optionalSize,b.size(),"Cabecalho opcional truncado");
        if(optionalSize<112 || read<uint16_t>(b,o)!=0x20b)throw std::runtime_error("Cabecalho PE32+ obrigatorio");
        auto count=read<uint16_t>(b,pe+6),machine=read<uint16_t>(b,pe+4);
        auto nd=read<uint32_t>(b,o+108);
        out<<"PE offset=0x"<<std::hex<<pe<<" machine=0x"<<machine<<" characteristics=0x"<<read<uint16_t>(b,pe+22)<<"\n";
        out<<"ImageBase=0x"<<read<uint64_t>(b,o+24)<<" SizeOfImage=0x"<<read<uint32_t>(b,o+56)
           <<" SizeOfHeaders=0x"<<read<uint32_t>(b,o+60)<<" EntryPoint=0x"<<read<uint32_t>(b,o+16)<<"\n";
        out<<"SectionAlignment=0x"<<read<uint32_t>(b,o+32)<<" FileAlignment=0x"<<read<uint32_t>(b,o+36)
           <<" Subsystem=0x"<<read<uint16_t>(b,o+68)<<" DllCharacteristics=0x"<<read<uint16_t>(b,o+70)<<std::dec<<"\n";
        out<<"Diretorios declarados="<<nd<<" Secoes="<<count<<" OptionalHeaderBytes="<<optionalSize<<"\n";
        const char* names[]={"EXPORT","IMPORT","RESOURCE","EXCEPTION","SECURITY/file offset","BASERELOC","DEBUG","ARCHITECTURE","GLOBALPTR","TLS","LOAD_CONFIG","BOUND_IMPORT","IAT","DELAY_IMPORT","CLR","RESERVED"};
        auto available=(optionalSize-112)/8;
        for(uint32_t n=0;n<std::min<uint32_t>({nd,16u,uint32_t(available)});++n) {
            auto rva=read<uint32_t>(b,o+112+n*8),size=read<uint32_t>(b,o+116+n*8);
            uint64_t limit=n==4?b.size():read<uint32_t>(b,o+56);
            bool valid=!size||(rva<=limit&&size<=limit-rva);
            out<<"DIR["<<std::dec<<n<<"] "<<names[n]<<" address=0x"<<std::hex<<rva<<" size=0x"<<size
               <<" bound="<<(valid?"OK":"OUTSIDE")<<"\n";
        }
        if(count>96)throw std::runtime_error("Numero de secoes acima do limite de auditoria");
        size_t st=o+optionalSize;range(st,count*40ull,b.size(),"Tabela de secoes truncada");
        for(unsigned n=0;n<count;++n) {
            size_t at=st+n*40;std::string name;
            for(unsigned j=0;j<8&&b[at+j];++j)name+=char(b[at+j]);
            out<<"SECTION "<<name<<" RVA=0x"<<std::hex<<read<uint32_t>(b,at+12)
               <<" virtual=0x"<<read<uint32_t>(b,at+8)<<" file-offset=0x"<<read<uint32_t>(b,at+20)
               <<" raw=0x"<<read<uint32_t>(b,at+16)<<" flags=0x"<<read<uint32_t>(b,at+36)<<"\n";
        }
    }catch(std::exception const& e){out<<"Cabecalho parcial: "<<e.what()<<"\n";}
    return out.str();
}
std::string PeImage::report() const {
    std::ostringstream o;
    o<<"AMD64 / PE32+\nImagem: "<<imageSize<<" bytes\nBase: 0x"<<std::hex<<imageBase
     <<"\nEntry RVA: 0x"<<entryPoint<<std::dec<<"\nSecoes: "<<sections.size()<<"\nImports: "<<imports.size()<<"\n";
    for(const auto& w:warnings)o<<"ATENCAO: "<<w<<"\n";
    // Preserve the small loader structures now so further reverse engineering
    // does not require another console deployment just to read these bytes.
    for(unsigned index:{9u,10u}) {
        auto d=directories[index];if(!d.size)continue;
        o<<"\n"<<(index==9?"TLS_DIRECTORY64":"LOAD_CONFIG")<<" RVA=0x"<<std::hex<<d.rva<<" size=0x"<<d.size<<"\n";
        try {
            size_t count=std::min<uint32_t>(d.size,index==9?40u:256u),at=fileAt(d.rva,count);
            o<<"Bytes:";
            for(size_t n=0;n<count;++n)o<<" "<<std::setw(2)<<std::setfill('0')<<unsigned(bytes_[at+n]);
            o<<std::setfill(' ')<<"\n";
            if(index==9&&d.size>=40) {
                o<<"RawDataStartVA=0x"<<read<uint64_t>(bytes_,at)<<" RawDataEndVA=0x"<<read<uint64_t>(bytes_,at+8)
                 <<" IndexVA=0x"<<read<uint64_t>(bytes_,at+16)<<" CallbacksVA=0x"<<read<uint64_t>(bytes_,at+24)
                 <<" ZeroFill=0x"<<read<uint32_t>(bytes_,at+32)<<" Characteristics=0x"<<read<uint32_t>(bytes_,at+36)<<"\n";
            }
        }catch(std::exception const& e){o<<"Metadados nao lidos: "<<e.what()<<"\n";}
        o<<std::dec;
    }
    for(const auto& i:imports)o<<i.library<<"!"<<(i.byOrdinal?"#"+std::to_string(i.ordinal):i.symbol)<<"\n";
    o<<"\nLeitura de secoes/imports concluida. O codigo do jogo nao foi executado.\n";
    if(!certificateTableInFile)o<<"Validacao estrita: REJEITA metadados de certificado. Resultado apenas de auditoria.\n";
    return o.str();
}
}
