#include "Debug/MemoryBackend.h"

#include <cstring>

#include "SaturnRegions.h"   // one home for the Saturn region sizes

namespace sfe
{

namespace
{
// Saturn CPU memory map (the regions the snapshot captures). Bases are the CPU-
// visible addresses; sizes come from the shared SaturnRegions.h constants.
// Addresses are normalized to their canonical mirror first (the SH-2 sees
// WRAM/VDP at several cache/through mirrors that share the low 27 bits).
struct Region
{
    uint32_t     base;
    uint32_t     size;
    se_vram_kind kind;
};
constexpr Region kRegions[] = {
    { 0x00200000u, kWramSize,     SE_VRAM_KIND_WRAM_LOW  },  // Low work RAM
    { 0x06000000u, kWramSize,     SE_VRAM_KIND_WRAM_HIGH },  // High work RAM
    { 0x05A00000u, kSoundRamSize, SE_VRAM_KIND_SOUND_RAM },  // SCSP sound RAM (cached mirror)
    { 0x05C00000u, kVdp1VramSize, SE_VRAM_KIND_VDP1_VRAM },  // VDP1 VRAM
    { 0x05C80000u, kVdp1FbSize,   SE_VRAM_KIND_VDP1_FB   },  // VDP1 frame buffer
    { 0x05E00000u, kVdp2VramSize, SE_VRAM_KIND_VDP2_VRAM },  // VDP2 VRAM
    { 0x05F00000u, kCramSize,     SE_VRAM_KIND_CRAM      },  // Color RAM
};

// VDP register windows, served through the register getters (not se_read_vram).
constexpr uint32_t kVdp1RegBase = 0x05D00000u, kVdp1RegSize = kVdp1RegBytes;
constexpr uint32_t kVdp2RegBase = 0x05F80000u, kVdp2RegSize = kVdp2RegBytes;

uint32_t Canonical(uint32_t addr)
{
    return addr & 0x07FFFFFFu;   // fold cache/through mirrors onto the low 27 bits
}
}  // namespace

MemoryReadResult ContextBackend::ReadOne(uint32_t address, uint32_t size) const
{
    MemoryReadResult r;
    if (!Connected())
    {
        r.error = "Disconnected";
        return r;
    }
    if (size == 0 || size > 0x10000)
    {
        r.error = "Bad size";
        return r;
    }
    se_context* ctx = *mContext;
    const uint32_t a = Canonical(address);

    // VDP registers: assemble big-endian bytes from 16-bit register reads.
    auto readRegs = [&](uint32_t base, uint32_t regSize, bool vdp1) -> bool
    {
        if (a < base || a + size > base + regSize)
        {
            return false;
        }
        r.bytes.resize(size);
        for (uint32_t i = 0; i < size; ++i)
        {
            const uint32_t off = (a - base) + i;
            const uint16_t reg = vdp1 ? se_get_vdp1_register(ctx, off & ~1u)
                                      : se_get_vdp2_register(ctx, off & ~1u);
            r.bytes[i] = (off & 1u) ? static_cast<uint8_t>(reg & 0xFF)
                                    : static_cast<uint8_t>(reg >> 8);   // big-endian
        }
        r.success = true;
        return true;
    };
    if (readRegs(kVdp1RegBase, kVdp1RegSize, true))  return r;
    if (readRegs(kVdp2RegBase, kVdp2RegSize, false)) return r;

    // RAM/VRAM/CRAM regions via the raw byte reader.
    for (const Region& reg : kRegions)
    {
        if (a < reg.base || a + size > reg.base + reg.size)
        {
            continue;
        }
        r.bytes.resize(size);
        const size_t got = se_read_vram(ctx, reg.kind, a - reg.base, r.bytes.data(), size);
        if (got == size)
        {
            r.success = true;
        }
        else
        {
            r.bytes.clear();
            r.error = "Unavailable";
        }
        return r;
    }

    r.error = "Unmapped address";
    return r;
}

std::vector<MemoryReadResult> ContextBackend::ReadMemoryBatch(
    const std::vector<MemoryReadRequest>& requests)
{
    std::vector<MemoryReadResult> out;
    out.reserve(requests.size());
    for (const MemoryReadRequest& req : requests)
    {
        out.push_back(ReadOne(req.address, req.size));
    }
    return out;
}

// Every captured region is editable in the snapshot: work RAM, sound RAM, and the VDP1/VDP2
// VRAM, CRAM, and framebuffer (a VDP edit re-derives the reconstructed image). Every edit also
// pokes a live emulator when the driver supports it — work/sound RAM through write_main_ram/
// write_sound_ram, VDP regions through write_vram (a savestate keeps the edit in-memory). The
// address must fall entirely inside a region and the source must have a loaded snapshot.
static bool IsWritableKind(se_vram_kind kind)
{
    return kind == SE_VRAM_KIND_WRAM_LOW || kind == SE_VRAM_KIND_WRAM_HIGH ||
           kind == SE_VRAM_KIND_SOUND_RAM || kind == SE_VRAM_KIND_VDP1_VRAM ||
           kind == SE_VRAM_KIND_VDP2_VRAM || kind == SE_VRAM_KIND_CRAM ||
           kind == SE_VRAM_KIND_VDP1_FB;
}

uint64_t ContextBackend::SourceId() const
{
    if (!Connected()) return 0;
    // Pointer and generation folded together: either one moving is a different source.
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(*mContext)) * 0x9E3779B97F4A7C15ull +
           mGeneration + 1u;
}

bool ContextBackend::CanWrite(uint32_t address) const
{
    if (mForceReadOnly) return false;
    if (!Connected() || !se_can_write(*mContext)) return false;
    const uint32_t a = Canonical(address);
    for (const Region& reg : kRegions)
        if (IsWritableKind(reg.kind) && a >= reg.base && a < reg.base + reg.size)
            return true;
    // VDP register windows are served through the register getters/setters, not se_read/write_vram.
    if (!mRegistersReadOnly)
    {
        if (a >= kVdp1RegBase && a < kVdp1RegBase + kVdp1RegSize) return true;
        if (a >= kVdp2RegBase && a < kVdp2RegBase + kVdp2RegSize) return true;
    }
    return false;
}

static bool InRegisterWindow(uint32_t a)
{
    return (a >= kVdp1RegBase && a < kVdp1RegBase + kVdp1RegSize) ||
           (a >= kVdp2RegBase && a < kVdp2RegBase + kVdp2RegSize);
}

static const Region* RegionAt(uint32_t a)
{
    for (const Region& reg : kRegions)
        if (a >= reg.base && a < reg.base + reg.size) return &reg;
    return nullptr;
}

std::string ContextBackend::WriteRefusal(uint32_t address) const
{
    if (!Connected()) return "No source is loaded.";
    if (mForceReadOnly) return mReadOnlyWhy ? mReadOnlyWhy : "Editing is off right now.";
    if (!se_can_write(*mContext)) return "This source has no editable snapshot.";
    const uint32_t a = Canonical(address);
    if (InRegisterWindow(a))
        return mRegistersReadOnly
            ? "VDP registers can only be edited in a loaded dump or savestate. On a live emulator or "
              "a recorded frame the edit would change this view and nothing else, and the next "
              "capture would undo it."
            : std::string();
    const Region* reg = RegionAt(a);
    if (!reg) return "That address is not in a captured region.";
    if (reg->kind == SE_VRAM_KIND_CRAM || reg->kind == SE_VRAM_KIND_VDP1_FB)
        return "CRAM and the VDP1 frame buffer can only be written to an emulator patched with the VDP "
               "writer (Integration/Mednafen), which this one does not have.";
    return "The source did not take the edit (it is not connected, or its poke queue is full).";
}

IMemoryBackend::WriteDest ContextBackend::WriteDestination(uint32_t address) const
{
    if (!Connected()) return WriteDest::ViewOnly;
    const uint32_t a = Canonical(address);
    if (InRegisterWindow(a)) return WriteDest::ViewOnly;   // register setters touch the snapshot only
    const Region* reg = RegionAt(a);
    if (!reg || !se_has_write_sink(*mContext, reg->kind)) return WriteDest::ViewOnly;
    return mEditsStaged ? WriteDest::Staged : WriteDest::Emulator;
}

size_t ContextBackend::WriteMemory(uint32_t address, const uint8_t* bytes, size_t size)
{
    if (!Connected() || !bytes || size == 0) return 0;
    // The same policy CanWrite offers edits under, enforced where the write happens: an edit
    // already in flight when the backend turned read-only must not slip through.
    if (mForceReadOnly || !se_can_write(*mContext)) return 0;
    se_context* ctx = *mContext;
    const uint32_t a = Canonical(address);
    if (mRegistersReadOnly && InRegisterWindow(a)) return 0;

    // VDP register windows: rebuild each 16-bit big-endian register from the byte(s) being
    // written and push it through the register setter (which re-derives the image).
    auto writeRegs = [&](uint32_t base, uint32_t regSize, bool vdp1) -> size_t
    {
        if (a < base || a + size > base + regSize) return 0;   // must fall entirely in the window
        size_t done = 0;
        for (size_t i = 0; i < size; ++i)
        {
            const uint32_t off = (a - base) + static_cast<uint32_t>(i);
            const uint32_t regOff = off & ~1u;
            const bool hi = (off & 1u) == 0u;   // big-endian: the even byte is the high byte
            const uint16_t cur = vdp1 ? se_get_vdp1_register(ctx, regOff)
                                      : se_get_vdp2_register(ctx, regOff);
            const uint16_t nv = hi ? static_cast<uint16_t>((cur & 0x00FFu) | (bytes[i] << 8))
                                   : static_cast<uint16_t>((cur & 0xFF00u) | bytes[i]);
            const int ok = vdp1 ? se_set_vdp1_register(ctx, regOff, nv)
                                : se_set_vdp2_register(ctx, regOff, nv);
            if (!ok) break;
            ++done;
        }
        return done;
    };
    if (a >= kVdp1RegBase && a < kVdp1RegBase + kVdp1RegSize) return writeRegs(kVdp1RegBase, kVdp1RegSize, true);
    if (a >= kVdp2RegBase && a < kVdp2RegBase + kVdp2RegSize) return writeRegs(kVdp2RegBase, kVdp2RegSize, false);

    for (const Region& reg : kRegions)
    {
        if (a < reg.base || a + size > reg.base + reg.size) continue;
        if (!IsWritableKind(reg.kind))
            return 0;   // (all captured regions are writable now; kept as a guard)
        return se_write_vram(ctx, reg.kind, a - reg.base, bytes, size);
    }
    return 0;
}

}  // namespace sfe
