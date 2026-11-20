#pragma once
#include <cstdint>

// Conversions of a picture read back from OpenGL (RGBA, bottom row first) to what the outputs send.
// Run in the send thread, into a buffer the caller keeps between frames.
namespace PixelConvert {
// BGRA, rows top to bottom (OMT)
void rgbaBottomUpToBgra(const uint8_t *rgba, int width, int height, uint8_t *bgra);
// YUYV 4:2:2, rows top to bottom, BT.601 limited range (v4l2). Only the first `width & ~1` pixels of a row are used:
// out must hold (width & ~1) * height * 2 bytes.
void rgbaBottomUpToYuyv(const uint8_t *rgba, int width, int height, uint8_t *yuyv);
}
