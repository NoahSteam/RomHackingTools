#include "SavestateDriver.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "SaturnStateShared.h"

namespace
{
using sedrv::BuildVdp2RegImage;
using sedrv::NormalizeCramToBigEndian;
using sedrv::ReadReg16;

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
    se_sh2_regs          mSh2[2] = {};       // [0] master, [1] slave
    bool                 mHasSh2[2] = { false, false };
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

int CbSh2Regs(void* user, int cpu, se_sh2_regs* out)
{
    Savestate* s = static_cast<Savestate*>(user);
    if (cpu < 0 || cpu > 1 || !s->mHasSh2[cpu]) return 0;
    *out = s->mSh2[cpu];
    return 1;
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
    if (state->mHasSh2[0] || state->mHasSh2[1])
    {
        caps |= SE_CAP_SH2_REGS;
        out->read_sh2_regs = CbSh2Regs;
    }

    out->capabilities = caps;
    out->close = CbClose;
}

}  // namespace

namespace
{
// Yabause-family (.yss) section layout constants.
constexpr uint32_t kVramSize    = 0x80000;   // VDP1/VDP2 VRAM
constexpr uint32_t kYssCramSize = 0x1000;    // VDP2 color RAM
constexpr uint32_t kVdp2RegSize = 288;       // sizeof(Vdp2): 286 regs + 2 padding to u32 align
constexpr size_t   kYssHeaderSize = 0x14;    // file header before the first section

// The classic Yabause Vdp2 register struct is a fixed hardware mirror (TVMD..COBB,
// sizeof 288) shared byte-for-byte across the lineage — verified identical in
// Yabause 0.9.15, Yaba Sanshiro, and Kronos. Vdp2SaveState writes that struct,
// then VRAM, then CRAM, then a tail of internal state, so a VDP2 section is
// recognized structurally (it must be at least the struct + VRAM + CRAM) rather
// than by version number, which forks bump freely. We only ever read the fixed
// leading base bytes, so any amount of trailing internal state is ignored.
constexpr uint32_t kVdp2SectionBase = kVdp2RegSize + kVramSize + kYssCramSize;

uint32_t Read32LE(const std::vector<uint8_t>& d, size_t o)
{
    return static_cast<uint32_t>(d[o]) | (static_cast<uint32_t>(d[o + 1]) << 8) |
           (static_cast<uint32_t>(d[o + 2]) << 16) | (static_cast<uint32_t>(d[o + 3]) << 24);
}

// Parse a Yabause SH-2 section: it opens with the sh2regs_struct (23 host-order
// u32). Thin wrapper over the shared parser so savestate + live can't drift.
void ParseSh2Regs(const std::vector<uint8_t>& d, size_t data, se_sh2_regs& out)
{
    sedrv::ParseSh2Regs(d.data() + data, out);
}

// Copy 'len' bytes from 'src', swapping each 16-bit word to normalize Yabause's
// host-order work RAM to Saturn big-endian (shared with the live driver).
void CopyBswap16(const std::vector<uint8_t>& d, size_t src, size_t len,
                 std::vector<uint8_t>& out)
{
    out.assign(d.begin() + src, d.begin() + src + len);
    sedrv::Bswap16(out.data(), out.size());
}

/* --- Mednafen MDFNSVST (Saturn 'ss' module) savestate --- */

constexpr size_t kMdfnHeaderSize   = 32;    // "MDFNSVST" + timestamp/version/size/dims
constexpr size_t kMdfnMagicSize    = 8;
constexpr size_t kMdfnEndianOff    = 20;    // u32; bit 31 set => written big-endian
constexpr size_t kMdfnPreviewWOff  = 24;    // u32 preview width
constexpr size_t kMdfnPreviewHOff  = 28;    // u32 preview height
constexpr size_t kMdfnSectionHdr   = 36;    // 32-byte name + u32 size

// Locate a length-prefixed SFORMAT field by name inside a section's data block
// [dataOff, dataOff+dataSize). Fields are: name-length(1) + name + size(4 LE) +
// payload. Returns the payload offset and size on success.
bool FindMednafenField(const std::vector<uint8_t>& file, size_t dataOff, uint32_t dataSize,
                       const char* name, size_t& outOff, uint32_t& outSize)
{
    const size_t nameLen = std::strlen(name);
    size_t p = dataOff;
    const size_t end = dataOff + dataSize;
    while (p + 5 <= end)
    {
        const uint8_t fieldNameLen = file[p];
        const size_t sizePos = p + 1 + fieldNameLen;
        if (sizePos + 4 > end)
        {
            break;
        }
        const uint32_t fieldSize = Read32LE(file, sizePos);
        const size_t payload = sizePos + 4;
        if (payload + fieldSize > end)
        {
            break;
        }
        if (fieldNameLen == nameLen &&
            std::memcmp(&file[p + 1], name, nameLen) == 0)
        {
            outOff = payload;
            outSize = fieldSize;
            return true;
        }
        p = payload + fieldSize;
    }
    return false;
}

// Copy a uint16 array field, byte-swapping little-endian words to Saturn-native
// big-endian when 'swap' is set (a state written on a little-endian host). This
// makes VRAM/CRAM/registers match what the core expects (big-endian words, so a
// texture byte at address A lands where a big-endian read finds it).
void CopyMednafenU16BE(const std::vector<uint8_t>& file, size_t off, uint32_t size,
                       std::vector<uint8_t>& out, bool swap)
{
    out.resize(size);
    for (uint32_t i = 0; i + 1 < size; i += 2)
    {
        if (swap)
        {
            out[i]     = file[off + i + 1];
            out[i + 1] = file[off + i];
        }
        else
        {
            out[i]     = file[off + i];
            out[i + 1] = file[off + i + 1];
        }
    }
}

// Mednafen stores VDP1's control/status registers as individual named scalar
// fields inside the "VDP1" section (TVMR/FBCR/PTMR/EDSR are uint8; EWDR/EWLR/
// EWRR/LOPR are uint16), not as a contiguous register file like VDP2's RawRegs.
// Reassemble them into a hardware-offset big-endian image so the shared
// read_vdp1_reg serves them like any other register file. ENDR is write-only
// and COPR/MODR are computed, so those hardware slots stay zero. Returns true if
// at least one field was found (leaving 'out' empty otherwise, so the driver
// reports no VDP1 registers rather than a table of zeros).
bool BuildVdp1RegImageFromMednafen(const std::vector<uint8_t>& file, size_t secData,
                                   uint32_t secSize, bool hostBigEndian,
                                   std::vector<uint8_t>& out)
{
    struct Field { const char* name; uint32_t hw; };
    static const Field kFields[] = {
        {"TVMR", 0x00}, {"FBCR", 0x02}, {"PTMR", 0x04},
        {"EWDR", 0x06}, {"EWLR", 0x08}, {"EWRR", 0x0A},
        {"EDSR", 0x10}, {"LOPR", 0x12},
    };
    out.assign(0x18, 0);   // covers hw 0x00..0x16
    int found = 0;
    for (const Field& f : kFields)
    {
        size_t off; uint32_t sz;
        if (!FindMednafenField(file, secData, secSize, f.name, off, sz) || sz == 0)
        {
            continue;
        }
        uint16_t val;
        if (sz == 1)
        {
            val = file[off];
        }
        else if (hostBigEndian)
        {
            val = static_cast<uint16_t>((file[off] << 8) | file[off + 1]);
        }
        else
        {
            val = static_cast<uint16_t>(file[off] | (file[off + 1] << 8));
        }
        out[f.hw]     = static_cast<uint8_t>(val >> 8);
        out[f.hw + 1] = static_cast<uint8_t>(val & 0xFF);
        ++found;
    }
    if (!found)
    {
        out.clear();
    }
    return found != 0;
}

// Parse an already-loaded .yss buffer into '*out' (zeroed by the caller). Kept
// separate from the path entry point so the dispatcher can reuse a single read.
se_result ParseYssBuffer(const std::vector<uint8_t>& file, se_data_source* out)
{
    if (file.size() < kYssHeaderSize + 12)
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
            // Layout: registers (size - VRAM) then VRAM. Taking the trailing 512 KiB
            // as VRAM is version-agnostic across the Yabause family (Vdp1SaveState
            // always writes its registers first, then Vdp1Ram).
            const uint32_t regBytes = size - kVramSize;
            state->mVdp1Vram.assign(file.begin() + data + regBytes,
                                    file.begin() + data + regBytes + kVramSize);
            haveVdp1 = true;
        }
        else if (std::memcmp(tag, "VDP2", 4) == 0 && size >= kVdp2SectionBase)
        {
            // Structural match for the classic 288-byte Vdp2 struct followed by VRAM
            // then CRAM (see kVdp2SectionBase). Covers Yabause 0.9.x, Yaba Sanshiro,
            // and Kronos (identical struct), plus any fork that kept the layout; the
            // trailing internal state is ignored. Only a fork that changed the struct
            // or VRAM size ahead of CRAM would misdecode — verify such a case with a
            // sample before trusting it.
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
        else if (std::memcmp(tag, "MSH2", 4) == 0 && size >= 92)
        {
            ParseSh2Regs(file, data, state->mSh2[0]);
            state->mHasSh2[0] = true;
        }
        else if (std::memcmp(tag, "SSH2", 4) == 0 && size >= 92)
        {
            ParseSh2Regs(file, data, state->mSh2[1]);
            state->mHasSh2[1] = true;
        }
        else if (std::memcmp(tag, "OTHR", 4) == 0 &&
                 size >= 0x10000 + kSizeWramHigh + kSizeWramLow)
        {
            // OTHR = BupRam(0x10000) + HighWram(1 MiB) + LowWram(1 MiB) + internal
            // state. Work RAM is stored 16-bit byte-swapped; normalize to big-endian.
            CopyBswap16(file, data + 0x10000, kSizeWramHigh, state->mWramHigh);
            CopyBswap16(file, data + 0x10000 + kSizeWramHigh, kSizeWramLow, state->mWramLow);
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

// Parse an already-loaded MDFNSVST buffer into '*out' (zeroed by the caller).
se_result ParseMednafenBuffer(const std::vector<uint8_t>& file, se_data_source* out)
{
    if (file.size() < kMdfnHeaderSize + kMdfnSectionHdr)
    {
        return SE_ERR_IO;
    }
    if (std::memcmp(file.data(), "MDFNSVST", kMdfnMagicSize) != 0)
    {
        return SE_ERR_UNSUPPORTED;
    }

    // A little-endian host (the usual case) stores every uint16 field LSB-first;
    // bit 31 of the header word at kMdfnEndianOff is set only for big-endian.
    const bool hostBigEndian = (Read32LE(file, kMdfnEndianOff) & 0x80000000u) != 0;
    const bool swap = !hostBigEndian;

    // Skip the RGB preview image (width*height*3) that follows the header.
    const uint32_t previewW = Read32LE(file, kMdfnPreviewWOff);
    const uint32_t previewH = Read32LE(file, kMdfnPreviewHOff);
    if (previewW > 4096 || previewH > 4096)
    {
        return SE_ERR_UNSUPPORTED;   // implausible dims => wrong header layout
    }
    size_t pos = kMdfnHeaderSize + static_cast<size_t>(previewW) * previewH * 3;

    Savestate* state = new (std::nothrow) Savestate();
    if (!state)
    {
        return SE_ERR_NO_DATA;
    }

    // Walk the section chain: 32-byte zero-padded name + u32 LE data size + data.
    bool haveVdp1 = false;
    while (pos + kMdfnSectionHdr <= file.size())
    {
        char name[33];
        std::memcpy(name, &file[pos], 32);
        name[32] = '\0';
        const uint32_t secSize = Read32LE(file, pos + 32);
        const size_t secData = pos + kMdfnSectionHdr;
        if (secData + secSize > file.size())
        {
            break;  // truncated
        }

        if (std::strcmp(name, "VDP1") == 0)
        {
            size_t off; uint32_t sz;
            if (FindMednafenField(file, secData, secSize, "VRAM", off, sz) &&
                sz >= kVramSize)
            {
                CopyMednafenU16BE(file, off, kVramSize, state->mVdp1Vram, swap);
                haveVdp1 = true;
            }
            // VDP1 control/status registers live as individual named fields.
            BuildVdp1RegImageFromMednafen(file, secData, secSize, hostBigEndian,
                                          state->mVdp1Regs);
        }
        else if (std::strcmp(name, "VDP2") == 0)
        {
            size_t off; uint32_t sz;
            if (FindMednafenField(file, secData, secSize, "VRAM", off, sz) && sz >= kVramSize)
            {
                CopyMednafenU16BE(file, off, kVramSize, state->mVdp2Vram, swap);
            }
            // Parse RawRegs before CRAM: CRAM normalization needs RAMCTL's CRAM mode.
            if (FindMednafenField(file, secData, secSize, "RawRegs", off, sz))
            {
                // RawRegs is uint16[0x100] indexed by (hw offset >> 1); swapping
                // to big-endian yields a hardware-offset register image the shared
                // read_vdp2_reg reads directly.
                CopyMednafenU16BE(file, off, sz, state->mVdp2Regs, swap);
            }
            if (FindMednafenField(file, secData, secSize, "CRAM", off, sz) && sz >= kYssCramSize)
            {
                // CRAM entries are host-endian: 16-bit words in the RGB555 modes,
                // 32-bit in RGB888 (mode 2). Copy raw, then byte-swap to Saturn-native
                // big-endian at the width the CRAM mode dictates (from RAMCTL). A
                // fixed 16-bit swap would corrupt RGB888 colors.
                state->mCram.assign(file.begin() + off, file.begin() + off + kYssCramSize);
                if (swap)
                {
                    const uint16_t ramctl = ReadReg16(state->mVdp2Regs, 0x0E);
                    NormalizeCramToBigEndian(state->mCram, (ramctl >> 12) & 0x3);
                }
            }
        }
        pos = secData + secSize;
    }

    if (!haveVdp1)
    {
        delete state;
        return SE_ERR_NO_DATA;
    }
    BuildDataSource(state, out);
    return SE_OK;
}

// Sniff an already-loaded savestate buffer's magic and dispatch to the matching
// parser. Shared by the path-based se_savestate_open (after LoadFile) and the
// buffer-based se_savestate_open_buffer (for hosts that supply bytes directly,
// e.g. a browser reading a File into WASM memory).
se_result DispatchBuffer(const std::vector<uint8_t>& file, se_data_source* out)
{
    if (file.size() >= 3 && file[0] == 'Y' && file[1] == 'S' && file[2] == 'S')
    {
        return ParseYssBuffer(file, out);        // Yabause family (.yss)
    }
    if (file.size() >= kMdfnMagicSize &&
        std::memcmp(file.data(), "MDFNSVST", kMdfnMagicSize) == 0)
    {
        return ParseMednafenBuffer(file, out);   // Mednafen / Beetle Saturn
    }
    return SE_ERR_UNSUPPORTED;
}

// Slice an already-loaded linear dump into regions by the Saturn memory map.
// Shared by the path-based se_savestate_open_full_dump (after LoadFile) and the
// buffer-based se_savestate_open_full_dump_buffer.
se_result ParseFullDumpBuffer(const std::vector<uint8_t>& dump, uint32_t base_address,
                              se_data_source* out)
{
    if (dump.empty())
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
    if (!LoadFile(path, file))
    {
        return SE_ERR_IO;
    }
    return ParseYssBuffer(file, out);
}

se_result se_savestate_open_mednafen(const char* path, se_data_source* out)
{
    if (!path || !out)
    {
        return SE_ERR_INVALID_ARG;
    }
    std::memset(out, 0, sizeof(*out));

    std::vector<uint8_t> file;
    if (!LoadFile(path, file))
    {
        return SE_ERR_IO;
    }
    return ParseMednafenBuffer(file, out);
}

se_result se_savestate_open(const char* path, se_data_source* out)
{
    if (!path || !out)
    {
        return SE_ERR_INVALID_ARG;
    }
    std::memset(out, 0, sizeof(*out));

    // Read the file once, then pick the parser by its magic. Each parser fills the
    // same internal buffers, so the core is identical regardless of which emulator
    // wrote the state. Add new families here as their layouts are reverse-engineered
    // (see SavestateDriver.h).
    std::vector<uint8_t> file;
    if (!LoadFile(path, file))
    {
        return SE_ERR_IO;
    }
    // Room for other emulators (Kronos, SSF, Yaba Sanshiro, ...) inside DispatchBuffer.
    return DispatchBuffer(file, out);
}

se_result se_savestate_open_buffer(const uint8_t* data, size_t size, se_data_source* out)
{
    if (!data || !out)
    {
        return SE_ERR_INVALID_ARG;
    }
    std::memset(out, 0, sizeof(*out));
    // Copy into an owned buffer so the parsers (which retain slices) don't depend
    // on the caller's memory outliving the context. The host may free 'data' as
    // soon as this returns.
    std::vector<uint8_t> file(data, data + size);
    return DispatchBuffer(file, out);
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
    return ParseFullDumpBuffer(dump, base_address, out);
}

se_result se_savestate_open_full_dump_buffer(const uint8_t* data, size_t size,
                                             uint32_t base_address, se_data_source* out)
{
    if (!data || !out)
    {
        return SE_ERR_INVALID_ARG;
    }
    std::memset(out, 0, sizeof(*out));
    std::vector<uint8_t> dump(data, data + size);
    return ParseFullDumpBuffer(dump, base_address, out);
}

}  // extern "C"
