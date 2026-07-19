#include "Debug/ShiftJis.h"

#include "Debug/ShiftJisTable.h"   // kSjisToUni[] (sorted), kSjisToUniCount

namespace sfe
{

bool SjisIsLead(uint8_t b)  { return (b >= 0x81 && b <= 0x9F) || (b >= 0xE0 && b <= 0xEF); }
bool SjisIsTrail(uint8_t b) { return (b >= 0x40 && b <= 0x7E) || (b >= 0x80 && b <= 0xFC); }
bool SjisIsHalfKana(uint8_t b) { return b >= 0xA1 && b <= 0xDF; }

uint32_t SjisDecode(uint8_t lead, uint8_t trail)
{
    const uint16_t key = static_cast<uint16_t>((lead << 8) | trail);
    int lo = 0, hi = kSjisToUniCount - 1;   // binary search the sorted table
    while (lo <= hi)
    {
        const int mid = (lo + hi) / 2;
        const uint16_t k = kSjisToUni[mid].sjis;
        if (k == key) return kSjisToUni[mid].uni;
        if (k < key)  lo = mid + 1;
        else          hi = mid - 1;
    }
    return 0;
}

uint32_t SjisHalfKana(uint8_t b)
{
    // 0xA1..0xDF map linearly onto the half-width katakana block U+FF61..U+FF9F.
    return SjisIsHalfKana(b) ? (0xFF61u + (b - 0xA1u)) : 0u;
}

int Utf8Encode(uint32_t cp, char* out)
{
    int n;
    if (cp < 0x80)          { out[0] = (char)cp; n = 1; }
    else if (cp < 0x800)    { out[0] = (char)(0xC0 | (cp >> 6));
                              out[1] = (char)(0x80 | (cp & 0x3F)); n = 2; }
    else if (cp < 0x10000)  { out[0] = (char)(0xE0 | (cp >> 12));
                              out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                              out[2] = (char)(0x80 | (cp & 0x3F)); n = 3; }
    else                    { out[0] = (char)(0xF0 | (cp >> 18));
                              out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
                              out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
                              out[3] = (char)(0x80 | (cp & 0x3F)); n = 4; }
    out[n] = '\0';
    return n;
}

}  // namespace sfe
