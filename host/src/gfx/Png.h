#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace azimuth::gfx {
// Writes 32-bit B,G,R,X pixels (top-down rows) as an 8-bit RGB PNG. The
// image data is stored uncompressed: no zlib on the console, and a frame
// is only a diagnostic. False when the file cannot be written.
bool savePng(const std::string& path,const std::vector<uint32_t>& pixels,int width,int height);
}
