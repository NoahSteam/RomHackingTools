#include "savestate_driver.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{

// Documented absolute bus addresses (Docs/Saturn/MemoryLayout.txt).
constexpr uint32_t kAddrVdp1Vram = 0x05C00000u;
constexpr uint32_t kAddrVdp1Regs = 0x05D00000u;
constexpr uint32_t kAddrVdp2Vram = 0x05E00000u;
constexpr uint32_t kAddrCram     = 0x05F00000u;
constexpr uint32_t kAddrVdp2Regs = 0x05F80000u;
constexpr uint32_t kAddrWramLow  = 0x00200000u;
constexpr uint32_t kAddrWramHigh = 0x06000000u;

constexpr uint32_t kSizeVdp1Vram = 512u * 1024u;
constexpr uint32_t kSizeVdp1Regs = 0x18u;
constexpr uint32_t kSizeVdp2Vram = 512u * 1024u;
constexpr uint32_t kSizeCram     = 4u * 1024u;
constexpr uint32_t kSizeVdp2Regs = 0x200u;
constexpr uint32_t kSizeWramLow  = 1024u * 1024u;
constexpr uint32_t kSizeWramHigh = 1024u * 1024u;

// The driver's owned state, referenced through se_data_source.user.
struct Savestate
{
    std::vector<uint8_t> mVdp1Vram;
    std::vector<uint8_t> mVdp2Vram;
    std::vector<uint8_t> mCram;
    std::vector<uint8_t> mWramLow;
    std::vector<uint8_t> mWramHigh;
    std::vector<uint8_t> mVdp1Regs;
    std::vector<uint8_t> mVdp2Regs;
};

// Copy from a region buffer with bounds clamping. Returns bytes copied.
size_t ReadRegion(const std::vector<uint8_t>& buffer, uint32_t offset,
                  void* dst, size_t size)
{
    if (offset >= buffer.size())
    {
        return 0;
    }

    size_t avail = buffer.size() - offset;
    size_t count = size < avail ? size : avail;
    std::memcpy(dst, buffer.data() + offset, count);
    return count;
}

// Big-endian 16-bit register read (Saturn is big-endian).
uint16_t ReadReg16(const std::vector<uint8_t>& regs, uint32_t reg)
{
    if (reg + 1 >= regs.size())
    {
        return 0;
    }
    return static_cast<uint16_t>((regs[reg] << 8) | regs[reg + 1]);
}

/* --- Seam A callbacks --- */
size_t CbVdp1Vram(void* user, uint32_t offset, void* dst, size_t size)
{
    return ReadRegion(static_cast<Savestate*>(user)->mVdp1Vram, offset, dst, size);
}

size_t CbVdp2Vram(void* user, uint32_t offset, void* dst, size_t size)
{
    return ReadRegion(static_cast<Savestate*>(user)->mVdp2Vram, offset, dst, size);
}

size_t CbCram(void* user, uint32_t offset, void* dst, size_t size)
{
    return ReadRegion(static_cast<Savestate*>(user)->mCram, offset, dst, size);
}

size_t CbMainRam(void* user, uint32_t address, void* dst, size_t size)
{
    Savestate* state = static_cast<Savestate*>(user);
    if (address >= kAddrWramHigh)
    {
        return ReadRegion(state->mWramHigh, address - kAddrWramHigh, dst, size);
    }
    if (address >= kAddrWramLow)
    {
        return ReadRegion(state->mWramLow, address - kAddrWramLow, dst, size);
    }
    return 0;
}

uint16_t CbVdp1Reg(void* user, uint32_t reg)
{
    return ReadReg16(static_cast<Savestate*>(user)->mVdp1Regs, reg);
}

uint16_t CbVdp2Reg(void* user, uint32_t reg)
{
    return ReadReg16(static_cast<Savestate*>(user)->mVdp2Regs, reg);
}

void CbClose(void* user)
{
    delete static_cast<Savestate*>(user);
}

// File helpers.
bool LoadFile(const std::string& path, std::vector<uint8_t>& out)
{
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file)
    {
        return false;
    }

    std::fseek(file, 0, SEEK_END);
    long length = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (length < 0)
    {
        std::fclose(file);
        return false;
    }

    out.resize(static_cast<size_t>(length));
    size_t got = length ? std::fread(out.data(), 1, out.size(), file) : 0;
    std::fclose(file);
    out.resize(got);
    return true;
}

// Extract [abs, abs+size) from a linear dump based at 'base' into 'out'.
void SliceRegion(const std::vector<uint8_t>& dump, uint32_t base,
                 uint32_t abs, uint32_t size, std::vector<uint8_t>& out)
{
    if (abs < base)
    {
        return;
    }

    uint32_t start = abs - base;
    if (start >= dump.size())
    {
        return;
    }

    uint32_t end = start + size;
    if (end > dump.size())
    {
        end = static_cast<uint32_t>(dump.size());
    }
    out.assign(dump.begin() + start, dump.begin() + end);
}

// Fill an se_data_source from a populated Savestate. Takes ownership of 'state'.
void BuildDataSource(Savestate* state, se_data_source* out)
{
    std::memset(out, 0, sizeof(*out));
    out->abi_version = SE_ABI_VERSION;
    out->user = state;

    uint32_t caps = 0;
    if (!state->mVdp1Vram.empty())
    {
        caps |= SE_CAP_VDP1_VRAM;
        out->read_vdp1_vram = CbVdp1Vram;
    }
    if (!state->mVdp2Vram.empty())
    {
        caps |= SE_CAP_VDP2_VRAM;
        out->read_vdp2_vram = CbVdp2Vram;
    }
    if (!state->mCram.empty())
    {
        caps |= SE_CAP_CRAM;
        out->read_cram = CbCram;
    }
    if (!state->mWramLow.empty() || !state->mWramHigh.empty())
    {
        caps |= SE_CAP_MAIN_RAM;
        out->read_main_ram = CbMainRam;
    }
    if (!state->mVdp1Regs.empty())
    {
        caps |= SE_CAP_VDP1_REGS;
        out->read_vdp1_reg = CbVdp1Reg;
    }
    if (!state->mVdp2Regs.empty())
    {
        caps |= SE_CAP_VDP2_REGS;
        out->read_vdp2_reg = CbVdp2Reg;
    }

    out->capabilities = caps;
    out->close = CbClose;
}

}  // namespace

namespace
{
// Yabause 0.9.15 section layout constants.
constexpr uint32_t kVramSize    = 0x80000;   // VDP1/VDP2 VRAM
constexpr uint32_t kYssCramSize = 0x1000;    // VDP2 color RAM
constexpr uint32_t kVdp2RegSize = 288;       // sizeof(Vdp2): 286 regs + 2 padding to u32 align (0.9.15)
constexpr uint32_t kVdp2RegMax  = 0x11E;     // highest VDP2 register (COBB)
constexpr size_t   kYssHeaderSize = 0x14;    // file header before the first section
constexpr uint32_t kYssVdp2Version = 1;      // VDP2 section version whose struct we decode (0.9.15)

uint32_t Read32LE(const std::vector<uint8_t>& d, size_t o)
{
    return static_cast<uint32_t>(d[o]) | (static_cast<uint32_t>(d[o + 1]) << 8) |
           (static_cast<uint32_t>(d[o + 2]) << 16) | (static_cast<uint32_t>(d[o + 3]) << 24);
}

// Exact hardware-offset -> Yabause 0.9.15 Vdp2 struct byte-offset map, indexed
// by (hw_reg >> 1). 0xFFFF marks a reserved/unmapped hardware slot. Generated
// from the struct in vdp2.h and validated against a real 0.9.15 savestate
// (RAMCTL, priorities, scroll, color offsets all read correctly). The three
// regions: hw < 0x0C maps 1:1; 0x0E..0x76 is hw-2 (the struct omits the
// reserved word at 0x0C); 0x78..0x11E maps 1:1 again (the u32 zoom/address
// unions insert padding that re-absorbs the shift), but each u32's two 16-bit
// halves are byte-swapped for the little-endian host that wrote the state.
const uint16_t kVdp2RegStructOffset[144] =
{
    0x000, 0x002, 0x004, 0x006, 0x008, 0x00A, 0xFFFF, 0x00C,   // hw 0x000
    0x00E, 0x010, 0x012, 0x014, 0x016, 0x018, 0x01A, 0x01C,   // hw 0x010
    0x01E, 0x020, 0x022, 0x024, 0x026, 0x028, 0x02A, 0x02C,   // hw 0x020
    0x02E, 0x030, 0x032, 0x034, 0x036, 0x038, 0x03A, 0x03C,   // hw 0x030
    0x03E, 0x040, 0x042, 0x044, 0x046, 0x048, 0x04A, 0x04C,   // hw 0x040
    0x04E, 0x050, 0x052, 0x054, 0x056, 0x058, 0x05A, 0x05C,   // hw 0x050
    0x05E, 0x060, 0x062, 0x064, 0x066, 0x068, 0x06A, 0x06C,   // hw 0x060
    0x06E, 0x070, 0x072, 0x074, 0x07A, 0x078, 0x07E, 0x07C,   // hw 0x070
    0x080, 0x082, 0x084, 0x086, 0x08A, 0x088, 0x08E, 0x08C,   // hw 0x080
    0x090, 0x092, 0x094, 0x096, 0x098, 0x09A, 0x09E, 0x09C,   // hw 0x090
    0x0A2, 0x0A0, 0x0A6, 0x0A4, 0x0AA, 0x0A8, 0x0AC, 0x0AE,   // hw 0x0A0
    0x0B0, 0x0B2, 0x0B4, 0x0B6, 0x0B8, 0x0BA, 0x0BE, 0x0BC,   // hw 0x0B0
    0x0C0, 0x0C2, 0x0C4, 0x0C6, 0x0C8, 0x0CA, 0x0CC, 0x0CE,   // hw 0x0C0
    0x0D0, 0x0D2, 0x0D4, 0x0D6, 0x0DA, 0x0D8, 0x0DE, 0x0DC,   // hw 0x0D0
    0x0E0, 0x0E2, 0x0E4, 0x0E6, 0x0E8, 0x0EA, 0x0EC, 0x0EE,   // hw 0x0E0
    0x0F0, 0x0F2, 0x0F4, 0x0F6, 0x0F8, 0x0FA, 0x0FC, 0xFFFF,   // hw 0x0F0
    0x0FE, 0x100, 0x102, 0x104, 0x106, 0x108, 0x10A, 0x10C,   // hw 0x100
    0x10E, 0x110, 0x112, 0x114, 0x116, 0x118, 0x11A, 0x11C,   // hw 0x110
};

// Rebuild a hardware-offset, big-endian VDP2 register image from the packed
// little-endian Vdp2 struct that starts at 'structBase' in 'file'. The shared
// read_vdp2_reg reads big-endian at hardware offsets, so this lets the core use
// it unchanged. 'out' is sized to cover every register up to kVdp2RegMax.
void BuildVdp2RegImage(const std::vector<uint8_t>& file, size_t structBase,
                       std::vector<uint8_t>& out)
{
    out.assign(kVdp2RegMax + 2, 0);
    for (uint32_t hw = 0; hw <= kVdp2RegMax; hw += 2)
    {
        const uint16_t so = kVdp2RegStructOffset[hw >> 1];
        if (so == 0xFFFF || structBase + so + 1 >= file.size())
        {
            continue;  // reserved slot, or beyond the section
        }
        const uint16_t val = static_cast<uint16_t>(file[structBase + so] |
                                                   (file[structBase + so + 1] << 8));
        out[hw]     = static_cast<uint8_t>(val >> 8);
        out[hw + 1] = static_cast<uint8_t>(val & 0xFF);
    }
}

// Yabause keeps VDP2 color RAM in host-native byte order (T2 access), so a
// savestate written on a little-endian host stores it little-endian, whereas
// the core (like real Saturn hardware and VRAM) expects big-endian. Normalize
// in place: swap 16-bit entries for the RGB555 modes, 32-bit for RGB888. 'crmd'
// is RAMCTL bits 12-13.
void NormalizeCramToBigEndian(std::vector<uint8_t>& cram, unsigned crmd)
{
    const size_t step = (crmd == 2) ? 4 : 2;   // mode 2 = RGB888 (32-bit entries)
    for (size_t i = 0; i + step <= cram.size(); i += step)
    {
        for (size_t a = 0, b = step - 1; a < b; ++a, --b)
        {
            const uint8_t t = cram[i + a];
            cram[i + a] = cram[i + b];
            cram[i + b] = t;
        }
    }
}
}  // namespace

extern "C" {

se_result se_savestate_open_region_dir(const char* dir, se_data_source* out)
{
    if (!dir || !out)
    {
        return SE_ERR_INVALID_ARG;
    }
    std::memset(out, 0, sizeof(*out));

    Savestate* state = new (std::nothrow) Savestate();
    if (!state)
    {
        return SE_ERR_NO_DATA;
    }

    std::string base(dir);
    if (!base.empty() && base.back() != '/' && base.back() != '\\')
    {
        base += '/';
    }

    LoadFile(base + "vdp1_vram.bin", state->mVdp1Vram);
    LoadFile(base + "vdp2_vram.bin", state->mVdp2Vram);
    LoadFile(base + "cram.bin",      state->mCram);
    LoadFile(base + "wram_low.bin",  state->mWramLow);
    LoadFile(base + "wram_high.bin", state->mWramHigh);
    LoadFile(base + "vdp1_regs.bin", state->mVdp1Regs);
    LoadFile(base + "vdp2_regs.bin", state->mVdp2Regs);

    if (state->mVdp1Vram.empty())  // need at least VDP1 VRAM
    {
        delete state;
        return SE_ERR_NO_DATA;
    }

    BuildDataSource(state, out);
    return SE_OK;
}

se_result se_savestate_open_yss(const char* path, se_data_source* out)
{
    if (!path || !out)
    {
        return SE_ERR_INVALID_ARG;
    }
    std::memset(out, 0, sizeof(*out));

    std::vector<uint8_t> file;
    if (!LoadFile(path, file) || file.size() < kYssHeaderSize + 12)
    {
        return SE_ERR_IO;
    }
    if (!(file[0] == 'Y' && file[1] == 'S' && file[2] == 'S'))
    {
        return SE_ERR_UNSUPPORTED;
    }

    Savestate* state = new (std::nothrow) Savestate();
    if (!state)
    {
        return SE_ERR_NO_DATA;
    }

    // Walk the section chain: each section is tag(4) + version(4) + size(4) + data.
    bool haveVdp1 = false;
    size_t pos = kYssHeaderSize;
    while (pos + 12 <= file.size())
    {
        const uint8_t* tag = &file[pos];
        const uint32_t version = Read32LE(file, pos + 4);
        const uint32_t size = Read32LE(file, pos + 8);
        const size_t data = pos + 12;
        if (data + size > file.size())
        {
            break;  // corrupt / truncated
        }

        if (std::memcmp(tag, "VDP1", 4) == 0 && size >= kVramSize)
        {
            // Layout: registers (size - VRAM) then VRAM.
            const uint32_t regBytes = size - kVramSize;
            state->mVdp1Vram.assign(file.begin() + data + regBytes,
                                    file.begin() + data + regBytes + kVramSize);
            haveVdp1 = true;
        }
        else if (std::memcmp(tag, "VDP2", 4) == 0 &&
                 version == kYssVdp2Version &&
                 size >= kVdp2RegSize + kVramSize + kYssCramSize)
        {
            // The VRAM/CRAM offsets below depend on sizeof(Vdp2) for this exact
            // struct version, so gate the whole VDP2 parse on the section version.
            // A newer Yabause / a fork with a different struct is skipped rather
            // than misdecoded — the context degrades to VDP1-only (no NBG).
            const size_t vramOff = data + kVdp2RegSize;
            state->mVdp2Vram.assign(file.begin() + vramOff, file.begin() + vramOff + kVramSize);
            state->mCram.assign(file.begin() + vramOff + kVramSize,
                                file.begin() + vramOff + kVramSize + kYssCramSize);

            // Rebuild the hardware-offset, big-endian register image via the exact
            // struct-offset map (correct through the priority/color-offset registers
            // the compositor needs), then normalize CRAM byte order using RAMCTL's
            // CRAM mode.
            BuildVdp2RegImage(file, data, state->mVdp2Regs);
            const uint16_t ramctl = ReadReg16(state->mVdp2Regs, 0x0E);
            NormalizeCramToBigEndian(state->mCram, (ramctl >> 12) & 0x3);
        }
        pos = data + size;
    }

    if (!haveVdp1)
    {
        delete state;
        return SE_ERR_NO_DATA;
    }
    BuildDataSource(state, out);
    return SE_OK;
}

se_result se_savestate_open(const char* path, se_data_source* out)
{
    if (!path || !out)
    {
        return SE_ERR_INVALID_ARG;
    }
    std::memset(out, 0, sizeof(*out));

    // Sniff enough of the header to pick the emulator's format, then dispatch to
    // the matching parser. Each parser fills the same internal buffers, so the
    // core is identical regardless of which emulator wrote the state. Add new
    // families here as their layouts are reverse-engineered (see savestate_driver.h).
    uint8_t magic[8] = { 0 };
    FILE* file = std::fopen(path, "rb");
    if (!file)
    {
        return SE_ERR_IO;
    }
    const size_t got = std::fread(magic, 1, sizeof(magic), file);
    std::fclose(file);
    if (got < 4)
    {
        return SE_ERR_IO;
    }

    if (magic[0] == 'Y' && magic[1] == 'S' && magic[2] == 'S')
    {
        return se_savestate_open_yss(path, out);   // Yabause family (.yss)
    }
    // Room for other emulators (Mednafen/Beetle "MDFNSVST", Kronos, SSF, ...).
    return SE_ERR_UNSUPPORTED;
}

se_result se_savestate_open_full_dump(const char* path, uint32_t base_address,
                                se_data_source* out)
{
    if (!path || !out)
    {
        return SE_ERR_INVALID_ARG;
    }
    std::memset(out, 0, sizeof(*out));

    std::vector<uint8_t> dump;
    if (!LoadFile(path, dump) || dump.empty())
    {
        return SE_ERR_IO;
    }

    Savestate* state = new (std::nothrow) Savestate();
    if (!state)
    {
        return SE_ERR_NO_DATA;
    }

    SliceRegion(dump, base_address, kAddrVdp1Vram, kSizeVdp1Vram, state->mVdp1Vram);
    SliceRegion(dump, base_address, kAddrVdp1Regs, kSizeVdp1Regs, state->mVdp1Regs);
    SliceRegion(dump, base_address, kAddrVdp2Vram, kSizeVdp2Vram, state->mVdp2Vram);
    SliceRegion(dump, base_address, kAddrCram,     kSizeCram,     state->mCram);
    SliceRegion(dump, base_address, kAddrVdp2Regs, kSizeVdp2Regs, state->mVdp2Regs);
    SliceRegion(dump, base_address, kAddrWramLow,  kSizeWramLow,  state->mWramLow);
    SliceRegion(dump, base_address, kAddrWramHigh, kSizeWramHigh, state->mWramHigh);

    if (state->mVdp1Vram.empty())
    {
        delete state;
        return SE_ERR_NO_DATA;
    }

    BuildDataSource(state, out);
    return SE_OK;
}

}  // extern "C"
