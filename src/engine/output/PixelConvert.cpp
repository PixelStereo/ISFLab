#include "PixelConvert.h"
#include <cstddef>

namespace PixelConvert {
void rgbaBottomUpToBgra(const uint8_t *rgba, int width, int height, uint8_t *bgra) {
    for (int y = 0; y < height; ++y) {
        const uint8_t *src = rgba + size_t(height - 1 - y) * size_t(width) * 4;
        uint8_t *dst = bgra + size_t(y) * size_t(width) * 4;
        for (int x = 0; x < width; ++x, src += 4, dst += 4) {
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
            dst[3] = src[3];
        }
    }
}

void rgbaBottomUpToYuyv(const uint8_t *rgba, int width, int height, uint8_t *yuyv) {
    const int pairs = width / 2;
    for (int y = 0; y < height; ++y) {
        const uint8_t *src = rgba + size_t(height - 1 - y) * size_t(width) * 4;
        uint8_t *dst = yuyv + size_t(y) * size_t(pairs) * 4;
        for (int x = 0; x < pairs; ++x, src += 8, dst += 4) {
            const int r0 = src[0], g0 = src[1], b0 = src[2], r1 = src[4], g1 = src[5], b1 = src[6];
            const int r = (r0 + r1) / 2, g = (g0 + g1) / 2, b = (b0 + b1) / 2;
            dst[0] = uint8_t(((66 * r0 + 129 * g0 + 25 * b0 + 128) >> 8) + 16);
            dst[1] = uint8_t(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
            dst[2] = uint8_t(((66 * r1 + 129 * g1 + 25 * b1 + 128) >> 8) + 16);
            dst[3] = uint8_t(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
        }
    }
}
}
