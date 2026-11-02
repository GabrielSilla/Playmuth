#pragma once
#include <cstdint>

// The console's screen. The host only draws into memory (the launcher's GDI
// picture, later the D3D12 swap chain); VideoOut is what puts a picture on
// the TV. Title 0.29 ran the engine to its launcher with the screen black:
// nothing was presented.
namespace azimuth::video {
// Shows a picture (pixels B,G,R,X, top-down, `width` x `height`) on the screen:
// copied into the next of two VideoOut framebuffers in the 64 KiB tiled
// layout the display scans out (the one PS5_Vulkan's demo renderer writes),
// then flipped at the next vblank. Pictures larger than 1920x1080 are
// cropped, smaller ones fill the top left. The first call opens VideoOut and
// hides the launch splash screen. False, with the reason logged once, when
// there is no display (a payload without it, or Linux).
bool present(const uint32_t* pixels,int width,int height);
// Closes the port present() opened and makes present() a no-op from then on:
// the driver's WSI opens its own port for the game's swap chain (title 0.93).
void release();
}
