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

uint32_t Read32LE(const std::vector<uint8_t>& d, size_t o)
{
    return static_cast<uint32_t>(d[o]) | (static_cast<uint32_t>(d[o + 1]) << 8) |
           (static_cast<uint32_t>(d[o + 2]) << 16) | (static_cast<uint32_t>(d[o + 3]) << 24);
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
                 size >= kVdp2RegSize + kVramSize + kYssCramSize)
        {
            const size_t vramOff = data + kVdp2RegSize;
            state->mVdp2Vram.assign(file.begin() + vramOff, file.begin() + vramOff + kVramSize);
            state->mCram.assign(file.begin() + vramOff + kVramSize,
                                file.begin() + vramOff + kVramSize + kYssCramSize);

            // Rebuild a hardware-offset, big-endian register image from the
            // packed little-endian struct, so the shared read_vdp2_reg (which
            // reads big-endian at hardware offsets) works unchanged. The struct
            // drops the reserved word at 0x0C, so struct = hw - 2 up to the first
            // u32 union field (hw 0x78: zoom/line-scroll/rotation regs). NOTE:
            // hw-2 is exact only below 0x78 — which covers every register the
            // core reads today (RAMCTL, and the NBG cell/scroll registers). The
            // post-union registers (priorities, color offsets) need the exact
            // struct-offset table; that lands with the VDP2 compositor (M4b pt2).
            state->mVdp2Regs.assign(kVdp2RegMax + 2, 0);
            for (uint32_t hw = 0; hw <= kVdp2RegMax; hw += 2)
            {
                const uint32_t so = (hw <= 0x0A) ? hw : hw - 2;
                const uint16_t val = static_cast<uint16_t>(file[data + so] |
                                                           (file[data + so + 1] << 8));
                state->mVdp2Regs[hw]     = static_cast<uint8_t>(val >> 8);
                state->mVdp2Regs[hw + 1] = static_cast<uint8_t>(val & 0xFF);
            }
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
