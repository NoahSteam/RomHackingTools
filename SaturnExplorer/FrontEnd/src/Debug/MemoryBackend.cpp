#include "Debug/MemoryBackend.h"

#include <cstring>

namespace sfe
{

namespace
{
// Saturn CPU memory map (the regions the snapshot captures). Addresses are
// normalized to their canonical mirror first (the SH-2 sees WRAM/VDP at several
// cache/through mirrors that share the low 27 bits).
struct Region
{
    uint32_t     base;
    uint32_t     size;
    se_vram_kind kind;
};
constexpr Region kRegions[] = {
    { 0x00200000u, 0x00100000u, SE_VRAM_KIND_WRAM_LOW  },  // Low work RAM (1 MiB)
    { 0x06000000u, 0x00100000u, SE_VRAM_KIND_WRAM_HIGH },  // High work RAM (1 MiB)
    { 0x05C00000u, 0x00080000u, SE_VRAM_KIND_VDP1_VRAM },  // VDP1 VRAM (512 KiB)
    { 0x05E00000u, 0x00080000u, SE_VRAM_KIND_VDP2_VRAM },  // VDP2 VRAM (512 KiB)
    { 0x05F00000u, 0x00001000u, SE_VRAM_KIND_CRAM      },  // Color RAM (4 KiB)
};

// VDP register windows, served through the register getters (not se_read_vram).
constexpr uint32_t kVdp1RegBase = 0x05D00000u, kVdp1RegSize = 0x18u;
constexpr uint32_t kVdp2RegBase = 0x05F80000u, kVdp2RegSize = 0x120u;

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
    if (size == 0 || size > 4)
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

}  // namespace sfe
