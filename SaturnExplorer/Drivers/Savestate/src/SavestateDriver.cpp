#include "SavestateDriver.h"

#include <algorithm>
#include <cstdio>
#include <new>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#if defined(SE_HAVE_ZLIB)
#include <zlib.h>   // Mednafen mcs/ savestates are gzip-compressed
#endif

#include "SaturnStateShared.h"
#include "saturnexplorer/SeGuard.h"

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

// The u16 normalizers refuse an odd length (see CopyMednafenU16BE / Bswap16). Every caller that
// passes one of these constants therefore cannot be refused, which is why those call sites do
// not test the result -- stated here so the property is checked rather than assumed.
static_assert(kSizeVdp2Vram % 2 == 0 && kSizeCram % 2 == 0 && kSizeVdp2Regs % 2 == 0 &&
                  kSizeWramLow % 2 == 0 && kSizeWramHigh % 2 == 0,
              "u16-array region sizes must be even");

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

    // True if the parse found anything worth opening a context over. Derived from the members
    // rather than tracked by a flag each parser sets, so a region added to this struct counts
    // without anyone remembering to update a second list.
    bool HasAnyRegion() const
    {
        return !mVdp1Vram.empty() || !mVdp2Vram.empty() || !mCram.empty() ||
               !mWramLow.empty() || !mWramHigh.empty() || !mVdp1Regs.empty() ||
               !mVdp2Regs.empty() || mHasSh2[0] || mHasSh2[1];
    }
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

// Copy 'size' big-endian bytes into 'buf' at 'off' (clamped). Returns bytes written.
size_t WriteRegion(std::vector<uint8_t>& buf, uint32_t off, const void* src, size_t size)
{
    if (off >= buf.size()) return 0;
    const size_t n = std::min<size_t>(size, buf.size() - off);
    std::memcpy(buf.data() + off, src, n);
    return n;
}

size_t CbWriteMainRam(void* user, uint32_t address, const void* src, size_t size)
{
    Savestate* state = static_cast<Savestate*>(user);
    if (address >= kAddrWramHigh)
    {
        return WriteRegion(state->mWramHigh, address - kAddrWramHigh, src, size);
    }
    if (address >= kAddrWramLow)
    {
        return WriteRegion(state->mWramLow, address - kAddrWramLow, src, size);
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

// Largest file we will accept as a savestate. A Saturn state is a few megabytes (2 MiB work
// RAM, VRAM, framebuffers, sound RAM); 64 MiB is far above any real one, and having a limit at
// all is what keeps a mistyped path at some huge unrelated file from being pulled entirely into
// memory before anything looks at it.
const size_t kMaxSavestateBytes = 64u * 1024u * 1024u;

// The buffer entry points take the same cap as LoadFile (which stops short of it), checked
// before the input is copied: the copy is the allocation the cap exists to bound.
bool OverSavestateCap(size_t size) { return size >= kMaxSavestateBytes; }

// File helpers.
//
// Read to EOF in chunks rather than sizing the file with fseek/ftell first. ftell returns a
// long, which is 32 bits on Windows, so a file past 2 GiB reported a negative or truncated
// length there and the load either failed or silently read the wrong amount. Reading until EOF
// needs no file-offset type at all, so there is nothing left to be the wrong width on any
// target -- including the 32-bit wasm build.
//
// This also stops treating a read error as success: the old form resized to whatever fread
// returned, so a failure partway through produced a short buffer that then parsed as a
// truncated-but-valid state.
bool LoadFile(const std::string& path, std::vector<uint8_t>& out)
{
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file)
    {
        return false;
    }
    // Read straight into the output, growing it a block at a time. Two things this avoids:
    // a stack scratch buffer -- 64 KiB of it would have been the whole default stack on the
    // wasm build this comment is about -- and copying every byte twice, once into scratch and
    // once into 'out'.
    const size_t kBlock = 64u * 1024u;
    out.clear();
    size_t filled = 0;
    for (;;)
    {
        if (filled > kMaxSavestateBytes - kBlock)   // no wrap: filled <= cap throughout
        {
            std::fclose(file);
            out.clear();
            return false;   // the next block could not fit under the cap
        }
        out.resize(filled + kBlock);
        const size_t got = std::fread(out.data() + filled, 1, kBlock, file);
        filled += got;
        if (got < kBlock)
        {
            break;   // short read: EOF or an error, sorted out below
        }
    }
    out.resize(filled);
    const bool readFailed = std::ferror(file) != 0;
    std::fclose(file);
    if (readFailed)
    {
        out.clear();
        return false;
    }
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
        caps |= SE_CAP_MAIN_RAM | SE_CAP_MEM_WRITE;
        out->read_main_ram = CbMainRam;
        out->write_main_ram = CbWriteMainRam;   // in-memory edits (not saved to disk)
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

// Bounds that separate "the layout we decode" from "a section of the same name in a layout we
// do not". Both are generous against what real states hold (measured on Yabause 0.9.x: a
// 52-byte VDP1 register prefix, 4 bytes of VDP2 trailing state), because the case they exist to
// catch is not a few extra fields -- it is a fork with a larger VRAM, which moves everything by
// hundreds of kilobytes and would otherwise still satisfy a bare `size >= kVramSize`.
constexpr uint32_t kYssMaxVdp1RegBytes = 4096;
constexpr uint32_t kYssMaxVdp2Trailing = 64 * 1024;

// Section tags are four printable ASCII characters. Anything else means the chain has
// desynchronized, and every offset after it is meaningless.
bool IsPrintableTag(const uint8_t* tag)
{
    for (int i = 0; i < 4; ++i)
    {
        if (tag[i] < 0x20 || tag[i] > 0x7E)
        {
            return false;
        }
    }
    return true;
}

// Does a recovered VDP2 register image actually look like VDP2 registers?
//
// This is the check the structural match cannot make on size alone, and the case OFF-01 names:
// a fork that keeps the section size but moves fields within it. The register file has bits the
// hardware does not define, and they read as zero in any real state, so a struct that has moved
// shows up as a reserved bit set or a mode field holding its one invalid value:
//
//   TVMD   (0x000) bit 3 and bits 9-14 are unused.
//   RAMCTL (0x00E) CRMD (bits 12-13) selects the CRAM mode; 3 is not one of them.
//
// Deliberately not checked: TVMD's DISP bit. A state captured with the display blanked has TVMD
// zero -- the Yabause states in this repo do -- so requiring it would reject real files.
//
// A failure degrades to VDP1-only rather than refusing the file, which is the documented
// behaviour for a fork whose VDP2 layout we do not share (see SavestateDriver.h): the sprites
// still render, and the backgrounds that would have been composited from a bogus register file
// are simply absent.
bool Vdp2RegImageLooksReal(const std::vector<uint8_t>& regs)
{
    const uint16_t tvmd = ReadReg16(regs, 0x000);
    if ((tvmd & 0x7E08u) != 0)
    {
        return false;
    }
    const uint16_t ramctl = ReadReg16(regs, 0x00E);
    return ((ramctl >> 12) & 0x3u) != 0x3u;
}

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
// host-order work RAM to Saturn big-endian (shared with the live driver). 'len' is always one of
// the even kSize* constants here, so Bswap16 cannot refuse; the live driver is where that
// return value matters, because there the length comes off the wire.
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
    // Every bound is a subtraction from what remains, so no declared length can wrap the check.
    // A malformed chain simply stops the search here; ValidateMednafenFields() is what tells
    // "absent" from "malformed", and runs over every section before any field is looked up.
    const size_t nameLen = std::strlen(name);
    size_t p = dataOff;
    const size_t end = dataOff + dataSize;
    while (end - p >= 5)
    {
        const uint8_t fieldNameLen = file[p];
        if (end - p < size_t(1) + fieldNameLen + 4)
        {
            break;
        }
        const size_t sizePos = p + 1 + fieldNameLen;
        const uint32_t fieldSize = Read32LE(file, sizePos);
        const size_t payload = sizePos + 4;
        if (fieldSize > end - payload)
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

// True if the section's field chain is well formed: every field header and payload fits inside
// the section and the chain ends exactly at its end. A field the section simply lacks is fine
// (callers treat that as absent); one that is cut short or overruns the section means the
// state is damaged, and FindMednafenField would otherwise read it as "not there" and let a
// partly recovered state open.
bool ValidateMednafenFields(const std::vector<uint8_t>& file, size_t dataOff, uint32_t dataSize)
{
    size_t p = dataOff;
    const size_t end = dataOff + dataSize;
    while (p != end)
    {
        if (end - p < 5)
        {
            return false;
        }
        const size_t nameLen = file[p];
        if (end - p < size_t(1) + nameLen + 4)
        {
            return false;
        }
        const size_t sizePos = p + 1 + nameLen;
        const uint32_t fieldSize = Read32LE(file, sizePos);
        const size_t payload = sizePos + 4;
        if (fieldSize > end - payload)
        {
            return false;
        }
        p = payload + fieldSize;
    }
    return true;
}

// Copy a uint16 array field, byte-swapping little-endian words to Saturn-native
// big-endian when 'swap' is set (a state written on a little-endian host). This
// makes VRAM/CRAM/registers match what the core expects (big-endian words, so a
// texture byte at address A lands where a big-endian read finds it).
// False if 'size' is not a whole number of u16s. The length comes from the field header in the
// file, so an odd one means the field is not the uint16 array the format declares -- and the
// round-down loop below would then copy every pair and silently leave the final byte at 0,
// producing a region that is correct up to that point and wrong after it. That reads as
// plausible memory: the load succeeds and every 16-bit value from the seam on is garbage with
// nothing to point at. 'out' is cleared on refusal so a caller that ignores the result cannot
// serve the half-converted copy either.
bool CopyMednafenU16BE(const std::vector<uint8_t>& file, size_t off, uint32_t size,
                       std::vector<uint8_t>& out, bool swap)
{
    if ((size & 1u) != 0)
    {
        out.clear();
        return false;
    }
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
    return true;
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
    // Byte 3 is the writer's host endianness (0 big, 1 little), and every multi-byte read
    // below -- section sizes, the register struct, CRAM -- assumes little. A state written on a
    // big-endian host would parse into plausible-looking nonsense rather than fail, so refuse it
    // instead of guessing; supporting one means byte-swapping the whole parse, not this one flag.
    // Anything other than 0 or 1 is not a header in this family's layout at all.
    if (file[3] > 1)
    {
        return SE_ERR_UNSUPPORTED;
    }
    if (file[3] == 0)
    {
        return SE_ERR_UNSUPPORTED;   // big-endian .yss: recognized, not supported
    }

    std::unique_ptr<Savestate> state(new (std::nothrow) Savestate());
    if (!state)
    {
        return SE_ERR_NO_DATA;
    }

    // Walk the section chain: each section is tag(4) + version(4) + size(4) + data.
    size_t pos = kYssHeaderSize;
    while (pos + 12 <= file.size())
    {
        const uint8_t* tag = &file[pos];
        const uint32_t size = Read32LE(file, pos + 8);
        const size_t data = pos + 12;
        // Section tags are four printable ASCII characters ("VDP1", "CS2 ", "OTHR"). Once the
        // chain desynchronizes -- a size field that is not the size, a layout with a different
        // section header -- what lands here is arbitrary bytes, and continuing would keep
        // walking a chain of nonsense until some stretch of it happened to match a tag we decode
        // at an offset that means nothing. That is the misdecode OFF-01 describes, so stop.
        if (!IsPrintableTag(tag))
        {
            return SE_ERR_UNSUPPORTED;
        }
        if (data + size > file.size())
        {
            // Truncated: the file was cut short, or these are not really section sizes. Either
            // way the chain is not trustworthy. This used to keep whatever had decoded before
            // the bad section, which reports a partial state as a complete one.
            return SE_ERR_UNSUPPORTED;
        }

        if (std::memcmp(tag, "VDP1", 4) == 0 && size >= kVramSize &&
            size - kVramSize <= kYssMaxVdp1RegBytes)
        {
            // Layout: registers (size - VRAM) then VRAM. Taking the trailing 512 KiB
            // as VRAM is version-agnostic across the Yabause family (Vdp1SaveState
            // always writes its registers first, then Vdp1Ram) -- but only while the part
            // before it really is a register block. A fork with a larger VDP1 VRAM keeps a
            // section size that still passes `size >= kVramSize`, and the trailing 512 KiB is
            // then the wrong half of its VRAM. The bound below is what distinguishes the two:
            // the real prefix is 52 bytes (measured on Yabause 0.9.x states), so a few KiB
            // leaves room for a fork that grew its register struct while refusing one that
            // grew VRAM.
            const uint32_t regBytes = size - kVramSize;
            state->mVdp1Vram.assign(file.begin() + data + regBytes,
                                    file.begin() + data + regBytes + kVramSize);
        }
        else if (std::memcmp(tag, "VDP2", 4) == 0 && size >= kVdp2SectionBase &&
                 size - kVdp2SectionBase <= kYssMaxVdp2Trailing)
        {
            // Structural match for the classic 288-byte Vdp2 struct followed by VRAM
            // then CRAM (see kVdp2SectionBase). Covers Yabause 0.9.x, Yaba Sanshiro,
            // and Kronos (identical struct), plus any fork that kept the layout; the
            // trailing internal state is ignored. Only a fork that changed the struct
            // or VRAM size ahead of CRAM would misdecode — verify such a case with a
            // sample before trusting it.
            // Rebuild the hardware-offset, big-endian register image via the exact
            // struct-offset map (correct through the priority/color-offset registers
            // the compositor needs), then judge it before trusting anything that depends on
            // it -- the CRAM byte order comes out of RAMCTL, so a register image from a moved
            // struct would byte-swap the palette at the wrong width as well.
            std::vector<uint8_t> regs;
            BuildVdp2RegImage(file, data, regs);
            if (Vdp2RegImageLooksReal(regs))
            {
                const size_t vramOff = data + kVdp2RegSize;
                state->mVdp2Vram.assign(file.begin() + vramOff,
                                        file.begin() + vramOff + kVramSize);
                state->mCram.assign(file.begin() + vramOff + kVramSize,
                                    file.begin() + vramOff + kVramSize + kYssCramSize);
                state->mVdp2Regs = regs;
                const uint16_t ramctl = ReadReg16(state->mVdp2Regs, 0x0E);
                NormalizeCramToBigEndian(state->mCram, (ramctl >> 12) & 0x3);
            }
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

    // Anything at all, not VDP1 specifically. The core stopped tying validity to VDP1 VRAM
    // (SNAP-02), but that relaxation was unreachable from a real file while the parsers kept
    // the old rule here: a VDP2-only state still failed to open, and only a hand-built
    // se_data_source could reach the new behaviour. A parser's job is to report what it found
    // and let the core judge usability.
    if (!state->HasAnyRegion())
    {
        return SE_ERR_NO_DATA;
    }
    BuildDataSource(state.release(), out);
    return SE_OK;
}

// Fill se_sh2_regs from a Mednafen "SH2-M"/"SH2-S" state section. Unlike Yabause's
// single contiguous sh2regs_struct, Mednafen stores the SH-2 state as separate
// SFORMAT fields (see SH7095::StateAction): "R" (16 u32 GP regs), "PC", "CtrlRegs"
// (the [SR,GBR,VBR] union), and "SysRegs" (the [MACH,MACL,PR] union) — all host-order
// u32. Returns true if at least the GP regs + PC were found.
bool ParseMednafenSh2(const std::vector<uint8_t>& file, size_t secData, uint32_t secSize,
                      bool hostBigEndian, se_sh2_regs& out)
{
    auto u32 = [&](size_t o) -> uint32_t
    {
        return hostBigEndian
            ? (uint32_t)((file[o] << 24) | (file[o + 1] << 16) | (file[o + 2] << 8) | file[o + 3])
            : Read32LE(file, o);
    };
    size_t off; uint32_t sz;
    bool haveR = false, havePc = false;
    if (FindMednafenField(file, secData, secSize, "R", off, sz) && sz >= 16 * 4)
    {
        for (int i = 0; i < 16; ++i) out.r[i] = u32(off + i * 4);
        haveR = true;
    }
    if (FindMednafenField(file, secData, secSize, "PC", off, sz) && sz >= 4)
    {
        out.pc = u32(off);
        havePc = true;
    }
    if (FindMednafenField(file, secData, secSize, "CtrlRegs", off, sz) && sz >= 12)
    {
        out.sr = u32(off); out.gbr = u32(off + 4); out.vbr = u32(off + 8);
    }
    if (FindMednafenField(file, secData, secSize, "SysRegs", off, sz) && sz >= 12)
    {
        out.mach = u32(off); out.macl = u32(off + 4); out.pr = u32(off + 8);
    }
    return haveR && havePc;
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
    if (pos > file.size())
    {
        return SE_ERR_UNSUPPORTED;   // the preview alone runs past the end of the file
    }

    std::unique_ptr<Savestate> state(new (std::nothrow) Savestate());
    if (!state)
    {
        return SE_ERR_NO_DATA;
    }

    // Walk the section chain: 32-byte zero-padded name + u32 LE data size + data.
    while (pos != file.size())
    {
        if (file.size() - pos < kMdfnSectionHdr)
        {
            // Bytes left over that cannot hold a section header: the chain was cut mid-header.
            return SE_ERR_UNSUPPORTED;
        }
        char name[33];
        std::memcpy(name, &file[pos], 32);
        name[32] = '\0';
        const uint32_t secSize = Read32LE(file, pos + 32);
        const size_t secData = pos + kMdfnSectionHdr;
        if (secSize > file.size() - secData)
        {
            // A section whose header promises more than the file holds is a damaged state;
            // accepting the regions parsed so far would load a partial machine as if whole.
            return SE_ERR_UNSUPPORTED;
        }
        // The sections read below are all field chains; refuse the state if any is malformed
        // rather than letting a bad field read as an absent one.
        if ((std::strcmp(name, "VDP1") == 0 || std::strcmp(name, "VDP2") == 0 ||
             std::strcmp(name, "MAIN") == 0 || std::strcmp(name, "SH2-M") == 0 ||
             std::strcmp(name, "SH2-S") == 0) &&
            !ValidateMednafenFields(file, secData, secSize))
        {
            return SE_ERR_UNSUPPORTED;
        }

        if (std::strcmp(name, "VDP1") == 0)
        {
            size_t off; uint32_t sz;
            if (FindMednafenField(file, secData, secSize, "VRAM", off, sz) &&
                sz >= kVramSize)
            {
                CopyMednafenU16BE(file, off, kVramSize, state->mVdp1Vram, swap);
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
                // read_vdp2_reg reads directly. 'sz' is the field header's own length -- the
                // only length here that the file chooses -- so this is the call that can
                // actually refuse.
                if (!CopyMednafenU16BE(file, off, sz, state->mVdp2Regs, swap))
                {
                    return SE_ERR_UNSUPPORTED;
                }
            }
            if (FindMednafenField(file, secData, secSize, "CRAM", off, sz) && sz >= kYssCramSize)
            {
                // Mednafen's CRAM is uint16[2048] in the file's byte order, in its own
                // layout rather than the bus's: in RGB888 (CRMD 2, and the illegal 3, which
                // vdp2.cpp's RW() treats alike) the first half holds every entry's high word
                // and the second half every low word. Decode the words, then put each where
                // the CPU sees it at 0x05F00000 -- the same mapping the live tap's
                // SsDbgCramWire uses, so a savestate and a live capture agree. Reversing
                // 4-byte groups instead paired the halves of two different colors.
                std::vector<uint8_t> words;
                CopyMednafenU16BE(file, off, kYssCramSize, words, swap);
                const uint16_t ramctl = ReadReg16(state->mVdp2Regs, 0x0E);
                if (((ramctl >> 12) & 0x3) >= 2)
                {
                    state->mCram.assign(kYssCramSize, 0);
                    for (uint32_t cri = 0; cri < kYssCramSize / 2; ++cri)
                    {
                        const uint32_t raw = ((cri >> 1) & 0x3FF) | ((cri & 1) << 10);
                        state->mCram[cri * 2]     = words[raw * 2];
                        state->mCram[cri * 2 + 1] = words[raw * 2 + 1];
                    }
                }
                else
                {
                    state->mCram = std::move(words);
                }
            }
        }
        else if (std::strcmp(name, "MAIN") == 0)
        {
            // Work RAM lives in the top-level "MAIN" section as SFVAR(WorkRAML) /
            // SFVAR(WorkRAMH) — host-order uint16, so swap to Saturn big-endian like
            // VRAM (the panels + Watch/Hex read work RAM big-endian).
            size_t off; uint32_t sz;
            if (FindMednafenField(file, secData, secSize, "WorkRAML", off, sz) && sz >= kSizeWramLow)
            {
                CopyMednafenU16BE(file, off, kSizeWramLow, state->mWramLow, swap);
            }
            if (FindMednafenField(file, secData, secSize, "WorkRAMH", off, sz) && sz >= kSizeWramHigh)
            {
                CopyMednafenU16BE(file, off, kSizeWramHigh, state->mWramHigh, swap);
            }
        }
        else if (std::strcmp(name, "SH2-M") == 0)
        {
            state->mHasSh2[0] = ParseMednafenSh2(file, secData, secSize, hostBigEndian, state->mSh2[0]);
        }
        else if (std::strcmp(name, "SH2-S") == 0)
        {
            state->mHasSh2[1] = ParseMednafenSh2(file, secData, secSize, hostBigEndian, state->mSh2[1]);
        }
        pos = secData + secSize;
    }

    // Anything at all, not VDP1 specifically. The core stopped tying validity to VDP1 VRAM
    // (SNAP-02), but that relaxation was unreachable from a real file while the parsers kept
    // the old rule here: a VDP2-only state still failed to open, and only a hand-built
    // se_data_source could reach the new behaviour. A parser's job is to report what it found
    // and let the core judge usability.
    if (!state->HasAnyRegion())
    {
        return SE_ERR_NO_DATA;
    }
    BuildDataSource(state.release(), out);
    return SE_OK;
}

// True if 'buf' starts with the gzip magic (1F 8B). Mednafen writes its mcs/ save
// states gzip-compressed, so any real Mednafen state comes in this way.
bool IsGzip(const std::vector<uint8_t>& buf)
{
    return buf.size() >= 2 && buf[0] == 0x1F && buf[1] == 0x8B;
}

// Inflate a gzip stream into 'out'. Returns false on a corrupt stream (or when built
// without zlib — gzipped states are then reported as unsupported rather than
// misparsed).
#if defined(SE_HAVE_ZLIB)
// Ceilings for an imported gzip state. Real Mednafen states are a few MiB compressed and
// tens of MiB inflated; anything past these is a decompression bomb or not a state at all.
constexpr size_t kMaxGzipInput  = 128u * 1024u * 1024u;
constexpr size_t kMaxGzipOutput = 256u * 1024u * 1024u;

bool Gunzip(const std::vector<uint8_t>& in, std::vector<uint8_t>& out)
{
    if (in.size() > kMaxGzipInput)
    {
        return false;
    }
    z_stream zs;
    std::memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK)   // 16 => decode a gzip header
    {
        return false;
    }
    zs.next_in = const_cast<Bytef*>(in.data());
    zs.avail_in = static_cast<uInt>(in.size());
    bool ok = false;
    try
    {
        // States inflate ~3x; start there (bounded) and double up to the ceiling.
        out.assign(std::min<size_t>(in.size() * 4 + 4096, kMaxGzipOutput), 0);
        for (;;)
        {
            if (zs.total_out == out.size())
            {
                if (out.size() >= kMaxGzipOutput)
                {
                    break;   // would exceed the ceiling
                }
                out.resize(std::min<size_t>(out.size() * 2, kMaxGzipOutput));
            }
            zs.next_out = out.data() + zs.total_out;
            zs.avail_out = static_cast<uInt>(out.size() - zs.total_out);
            const int ret = inflate(&zs, Z_NO_FLUSH);
            if (ret == Z_STREAM_END)
            {
                out.resize(zs.total_out);
                ok = true;
                break;
            }
            if (ret != Z_OK)
            {
                break;
            }
        }
    }
    catch (const std::bad_alloc&)
    {
        ok = false;
    }
    inflateEnd(&zs);
    if (!ok)
    {
        out.clear();
    }
    return ok;
}
#else
bool Gunzip(const std::vector<uint8_t>&, std::vector<uint8_t>&) { return false; }
#endif

// Sniff an already-loaded savestate buffer's magic and dispatch to the matching
// parser. Shared by the path-based se_savestate_open (after LoadFile) and the
// buffer-based se_savestate_open_buffer (for hosts that supply bytes directly,
// e.g. a browser reading a File into WASM memory). Transparently gunzips a gzip-
// compressed input first (Mednafen mcs/ states).
se_result DispatchBuffer(const std::vector<uint8_t>& file, se_data_source* out)
{
    std::vector<uint8_t> inflated;
    const std::vector<uint8_t>* eff = &file;
    if (IsGzip(file))
    {
        if (!Gunzip(file, inflated))
        {
            return SE_ERR_UNSUPPORTED;   // corrupt stream, or built without zlib
        }
        eff = &inflated;
    }
    const std::vector<uint8_t>& f = *eff;

    if (f.size() >= 3 && f[0] == 'Y' && f[1] == 'S' && f[2] == 'S')
    {
        return ParseYssBuffer(f, out);        // Yabause family (.yss)
    }
    if (f.size() >= kMdfnMagicSize &&
        std::memcmp(f.data(), "MDFNSVST", kMdfnMagicSize) == 0)
    {
        return ParseMednafenBuffer(f, out);   // Mednafen / Beetle Saturn
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
    std::unique_ptr<Savestate> state(new (std::nothrow) Savestate());
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

    if (!state->HasAnyRegion())
    {
        return SE_ERR_NO_DATA;
    }
    BuildDataSource(state.release(), out);
    return SE_OK;
}
}  // namespace

// Opening a state allocates the whole file and every region in it, so std::bad_alloc is the
// ordinary failure here, not an exotic one -- a 64 MiB state on a memory-tight web build gets
// there without any bug. It must not cross the seam: see SeGuard.h. The parsers hold their
// Savestate in a unique_ptr for the same reason, so a throw part-way through filling one frees
// it instead of leaking it.
using se::Guard;

extern "C" {

se_result se_savestate_open_region_dir(const char* dir, se_data_source* out)
{
    if (!dir || !out)
    {
        return SE_ERR_INVALID_ARG;
    }
    std::memset(out, 0, sizeof(*out));

    return Guard(SE_ERR_NO_MEMORY, [&]
    {
        std::unique_ptr<Savestate> state(new (std::nothrow) Savestate());
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

        if (!state->HasAnyRegion())   // see ParseYssBuffer: anything at all, not VDP1 specifically
        {
            return SE_ERR_NO_DATA;
        }

        BuildDataSource(state.release(), out);
        return SE_OK;
    });
}

se_result se_savestate_open_yss(const char* path, se_data_source* out)
{
    if (!path || !out)
    {
        return SE_ERR_INVALID_ARG;
    }
    std::memset(out, 0, sizeof(*out));

    return Guard(SE_ERR_NO_MEMORY, [&]
    {
        std::vector<uint8_t> file;
        if (!LoadFile(path, file))
        {
            return SE_ERR_IO;
        }
        return ParseYssBuffer(file, out);
    });
}

se_result se_savestate_open_mednafen(const char* path, se_data_source* out)
{
    if (!path || !out)
    {
        return SE_ERR_INVALID_ARG;
    }
    std::memset(out, 0, sizeof(*out));

    return Guard(SE_ERR_NO_MEMORY, [&]
    {
        std::vector<uint8_t> file;
        if (!LoadFile(path, file))
        {
            return SE_ERR_IO;
        }
        return ParseMednafenBuffer(file, out);
    });
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
    return Guard(SE_ERR_NO_MEMORY, [&]
    {
        std::vector<uint8_t> file;
        if (!LoadFile(path, file))
        {
            return SE_ERR_IO;
        }
        // Room for other emulators (Kronos, SSF, Yaba Sanshiro, ...) inside DispatchBuffer.
        return DispatchBuffer(file, out);
    });
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
    if (OverSavestateCap(size))
    {
        return SE_ERR_IO;   // what se_savestate_open returns for the same bytes on disk
    }
    return Guard(SE_ERR_NO_MEMORY, [&]
    {
        std::vector<uint8_t> file(data, data + size);
        return DispatchBuffer(file, out);
    });
}

se_result se_savestate_open_full_dump(const char* path, uint32_t base_address,
                                se_data_source* out)
{
    if (!path || !out)
    {
        return SE_ERR_INVALID_ARG;
    }
    std::memset(out, 0, sizeof(*out));

    return Guard(SE_ERR_NO_MEMORY, [&]
    {
        std::vector<uint8_t> dump;
        if (!LoadFile(path, dump) || dump.empty())
        {
            return SE_ERR_IO;
        }
        return ParseFullDumpBuffer(dump, base_address, out);
    });
}

se_result se_savestate_open_full_dump_buffer(const uint8_t* data, size_t size,
                                             uint32_t base_address, se_data_source* out)
{
    if (!data || !out)
    {
        return SE_ERR_INVALID_ARG;
    }
    std::memset(out, 0, sizeof(*out));
    if (OverSavestateCap(size))
    {
        return SE_ERR_IO;
    }
    return Guard(SE_ERR_NO_MEMORY, [&]
    {
        std::vector<uint8_t> dump(data, data + size);
        return ParseFullDumpBuffer(dump, base_address, out);
    });
}

}  // extern "C"
