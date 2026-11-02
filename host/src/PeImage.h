#pragma once
#include <cstdint>
#include "LargeBuffer.h"
#include <string>
#include <vector>
#include <stdexcept>

namespace azimuth {
struct Section {
    std::string name;
    uint32_t rva{}, virtualSize{}, fileOffset{}, fileSize{}, characteristics{};
};
struct Import { std::string library, symbol; uint16_t ordinal{}; uint32_t iatRva{}; bool byOrdinal{}; };
struct Directory { uint32_t rva{}, size{}; };
enum class PeReadMode { Strict, AuditUnmappedMetadata };

// A bounded reader of the original AMD64 image; parser copied unchanged from AzimuthUwp.
// Parsing or mapping an image does not make its desktop/Xbox APIs available.
class PeImage {
public:
    explicit PeImage(Bytes bytes,PeReadMode mode=PeReadMode::Strict);
    bool certificateTableInFile=true;
    uint64_t imageBase{};
    uint32_t imageSize{}, headerSize{}, entryPoint{}, sectionAlignment{};
    uint16_t subsystem{};
    Directory directories[16]{};
    std::vector<Section> sections;
    std::vector<Import> imports;
    std::vector<std::string> warnings;
    Bytes mappedBytes(uint64_t newBase) const;
    std::string report() const;
    static std::string headerReport(const Bytes& bytes);
    const Bytes& bytes() const { return bytes_; }
private:
    Bytes bytes_;
    size_t fileAt(uint32_t rva, size_t length) const;
    std::string stringAt(uint32_t rva) const;
    void parseImports();
};
}
