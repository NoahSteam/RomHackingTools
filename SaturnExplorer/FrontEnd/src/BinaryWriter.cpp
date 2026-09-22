#include "BinaryWriter.h"

#include <unordered_map>
#include <utility>

namespace sfe
{

namespace
{

// BITMAPFILEHEADER + BITMAPINFOHEADER for a w*h image at 'bpp', with 'paletteCount' color
// table entries following. Shared by the BMP flavours below.
void PushBmpHeader(std::vector<uint8_t>& bmp, int w, int h, int bpp, int paletteCount,
                   uint32_t imageSize)
{
    const uint32_t dataOff = 14 + 40 + static_cast<uint32_t>(paletteCount) * 4;
    bmp.push_back('B');
    bmp.push_back('M');
    PushU32(bmp, dataOff + imageSize);
    PushU32(bmp, 0);
    PushU32(bmp, dataOff);
    PushU32(bmp, 40);
    PushU32(bmp, static_cast<uint32_t>(w));
    PushU32(bmp, static_cast<uint32_t>(h));
    PushU16(bmp, 1);
    PushU16(bmp, static_cast<uint16_t>(bpp));
    PushU32(bmp, 0);
    PushU32(bmp, imageSize);
    PushU32(bmp, 0);
    PushU32(bmp, 0);
    PushU32(bmp, static_cast<uint32_t>(paletteCount));
    PushU32(bmp, 0);
}

}  // namespace

void PushU16(std::vector<uint8_t>& v, uint16_t x)
{
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>(x >> 8));
}

void PushU32(std::vector<uint8_t>& v, uint32_t x)
{
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
}

std::vector<uint8_t> BuildBmp(int w, int h, const std::vector<uint8_t>& pixels,
                              int paletteCount, const uint8_t* palBGRA)
{
    const bool indexed = paletteCount > 0;
    const int bpp = indexed ? 8 : 24;
    const int rowBytes = indexed ? w : w * 3;
    const int pad = (4 - (rowBytes & 3)) & 3;
    const uint32_t imageSize = static_cast<uint32_t>((rowBytes + pad) * h);

    std::vector<uint8_t> bmp;
    bmp.reserve(54 + static_cast<size_t>(paletteCount) * 4 + imageSize);
    PushBmpHeader(bmp, w, h, bpp, paletteCount, imageSize);
    if (indexed) bmp.insert(bmp.end(), palBGRA, palBGRA + paletteCount * 4);

    for (int y = h - 1; y >= 0; --y)                             // bottom-up rows
    {
        if (indexed)
        {
            for (int x = 0; x < w; ++x)
                bmp.push_back(pixels[static_cast<size_t>(y) * w + x]);
        }
        else
        {
            for (int x = 0; x < w; ++x)
            {
                const uint8_t* p = &pixels[(static_cast<size_t>(y) * w + x) * 4];
                bmp.push_back(p[2]); bmp.push_back(p[1]); bmp.push_back(p[0]);   // BGR
            }
        }
        for (int i = 0; i < pad; ++i) bmp.push_back(0);
    }
    return bmp;
}

std::vector<uint8_t> BuildBmpRgba(int w, int h, const std::vector<uint8_t>& rgba)
{
    // 32 bpp rows are inherently 4-byte aligned, so there is never any padding.
    const uint32_t imageSize = static_cast<uint32_t>(w) * static_cast<uint32_t>(h) * 4u;
    std::vector<uint8_t> bmp;
    bmp.reserve(54 + imageSize);
    PushBmpHeader(bmp, w, h, 32, 0, imageSize);
    for (int y = h - 1; y >= 0; --y)
    {
        const uint8_t* row = &rgba[static_cast<size_t>(y) * w * 4];
        for (int x = 0; x < w; ++x)
        {
            const uint8_t* p = &row[x * 4];
            bmp.push_back(p[2]); bmp.push_back(p[1]); bmp.push_back(p[0]); bmp.push_back(p[3]);
        }
    }
    return bmp;
}

std::vector<uint8_t> BuildIndexedBmp(int w, int h, const std::vector<uint8_t>& rgba,
                                     const se_palette& palette)
{
    std::vector<uint8_t> table(static_cast<size_t>(palette.count) * 4);
    std::unordered_map<uint32_t, uint8_t> toIndex;
    toIndex.reserve(palette.count * 2);
    // Keying on RGBA (not just RGB) keeps transparent/opaque duplicates distinct.
    for (int i = 0; i < palette.count; ++i)
    {
        const se_palette_entry& e = palette.entries[i];
        table[i * 4 + 0] = e.b;
        table[i * 4 + 1] = e.g;
        table[i * 4 + 2] = e.r;
        table[i * 4 + 3] = 0;
        const uint32_t key = (static_cast<uint32_t>(e.r) << 24) |
                             (static_cast<uint32_t>(e.g) << 16) |
                             (static_cast<uint32_t>(e.b) << 8) | e.a;
        toIndex.insert(std::make_pair(key, static_cast<uint8_t>(i)));
    }
    std::vector<uint8_t> idx(static_cast<size_t>(w) * h, 0);
    for (size_t i = 0; i < idx.size(); ++i)
    {
        const uint8_t* p = &rgba[i * 4];
        const uint32_t key = (static_cast<uint32_t>(p[0]) << 24) |
                             (static_cast<uint32_t>(p[1]) << 16) |
                             (static_cast<uint32_t>(p[2]) << 8) | p[3];
        const std::unordered_map<uint32_t, uint8_t>::const_iterator it = toIndex.find(key);
        if (it != toIndex.end()) idx[i] = it->second;
    }
    return BuildBmp(w, h, idx, palette.count, table.data());
}

}  // namespace sfe
