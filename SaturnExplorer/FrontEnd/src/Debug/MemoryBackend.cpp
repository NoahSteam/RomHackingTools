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

// Regions the core can edit: work RAM and (live v13+) SCSP sound RAM. The address must
// fall entirely inside one and the source must have a loaded snapshot.
static bool IsWritableKind(se_vram_kind kind)
{
    return kind == SE_VRAM_KIND_WRAM_LOW || kind == SE_VRAM_KIND_WRAM_HIGH ||
           kind == SE_VRAM_KIND_SOUND_RAM;
}

bool ContextBackend::CanWrite(uint32_t address) const
{
    if (mForceReadOnly) return false;
    if (!Connected() || !se_can_write(*mContext)) return false;
    const uint32_t a = Canonical(address);
    for (const Region& reg : kRegions)
        if (IsWritableKind(reg.kind) && a >= reg.base && a < reg.base + reg.size)
            return true;
    return false;
}

size_t ContextBackend::WriteMemory(uint32_t address, const uint8_t* bytes, size_t size)
{
    if (!Connected() || !bytes || size == 0) return 0;
    const uint32_t a = Canonical(address);
    for (const Region& reg : kRegions)
    {
        if (a < reg.base || a + size > reg.base + reg.size) continue;
        if (!IsWritableKind(reg.kind))
            return 0;   // read-only region (VDP VRAM/CRAM/FB)
        return se_write_vram(*mContext, reg.kind, a - reg.base, bytes, size);
    }
    return 0;
}

}  // namespace sfe
