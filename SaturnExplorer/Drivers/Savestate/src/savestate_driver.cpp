#include "savestate_driver.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

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
struct Savestate {
    std::vector<uint8_t> vdp1_vram;
    std::vector<uint8_t> vdp2_vram;
    std::vector<uint8_t> cram;
    std::vector<uint8_t> wram_low;
    std::vector<uint8_t> wram_high;
    std::vector<uint8_t> vdp1_regs;
    std::vector<uint8_t> vdp2_regs;
};

// Copy from a region buffer with bounds clamping. Returns bytes copied.
size_t ReadRegion(const std::vector<uint8_t>& buf, uint32_t offset,
                  void* dst, size_t size) {
    if (offset >= buf.size()) return 0;
    size_t avail = buf.size() - offset;
    size_t n = size < avail ? size : avail;
    std::memcpy(dst, buf.data() + offset, n);
    return n;
}

// Big-endian 16-bit register read (Saturn is big-endian).
uint16_t ReadReg16(const std::vector<uint8_t>& regs, uint32_t reg) {
    if (reg + 1 >= regs.size()) return 0;
    return static_cast<uint16_t>((regs[reg] << 8) | regs[reg + 1]);
}

/* --- Seam A callbacks --- */
size_t CbVdp1Vram(void* u, uint32_t o, void* d, size_t s) {
    return ReadRegion(static_cast<Savestate*>(u)->vdp1_vram, o, d, s);
}
size_t CbVdp2Vram(void* u, uint32_t o, void* d, size_t s) {
    return ReadRegion(static_cast<Savestate*>(u)->vdp2_vram, o, d, s);
}
size_t CbCram(void* u, uint32_t o, void* d, size_t s) {
    return ReadRegion(static_cast<Savestate*>(u)->cram, o, d, s);
}
size_t CbMainRam(void* u, uint32_t address, void* d, size_t s) {
    Savestate* ss = static_cast<Savestate*>(u);
    if (address >= kAddrWramHigh)
        return ReadRegion(ss->wram_high, address - kAddrWramHigh, d, s);
    if (address >= kAddrWramLow)
        return ReadRegion(ss->wram_low, address - kAddrWramLow, d, s);
    return 0;
}
uint16_t CbVdp1Reg(void* u, uint32_t r) {
    return ReadReg16(static_cast<Savestate*>(u)->vdp1_regs, r);
}
uint16_t CbVdp2Reg(void* u, uint32_t r) {
    return ReadReg16(static_cast<Savestate*>(u)->vdp2_regs, r);
}
void CbClose(void* u) { delete static_cast<Savestate*>(u); }

// File helpers.
bool LoadFile(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long len = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (len < 0) { std::fclose(f); return false; }
    out.resize(static_cast<size_t>(len));
    size_t got = len ? std::fread(out.data(), 1, out.size(), f) : 0;
    std::fclose(f);
    out.resize(got);
    return true;
}

// Extract [abs, abs+size) from a linear dump based at 'base' into 'out'.
void SliceRegion(const std::vector<uint8_t>& dump, uint32_t base,
                 uint32_t abs, uint32_t size, std::vector<uint8_t>& out) {
    if (abs < base) return;
    uint32_t start = abs - base;
    if (start >= dump.size()) return;
    uint32_t end = start + size;
    if (end > dump.size()) end = static_cast<uint32_t>(dump.size());
    out.assign(dump.begin() + start, dump.begin() + end);
}

// Fill an se_data_source from a populated Savestate. Takes ownership of 'ss'.
void BuildDataSource(Savestate* ss, se_data_source* out) {
    std::memset(out, 0, sizeof(*out));
    out->abi_version = SE_ABI_VERSION;
    out->user = ss;

    uint32_t caps = 0;
    if (!ss->vdp1_vram.empty()) { caps |= SE_CAP_VDP1_VRAM; out->read_vdp1_vram = CbVdp1Vram; }
    if (!ss->vdp2_vram.empty()) { caps |= SE_CAP_VDP2_VRAM; out->read_vdp2_vram = CbVdp2Vram; }
    if (!ss->cram.empty())      { caps |= SE_CAP_CRAM;      out->read_cram = CbCram; }
    if (!ss->wram_low.empty() || !ss->wram_high.empty()) {
        caps |= SE_CAP_MAIN_RAM; out->read_main_ram = CbMainRam;
    }
    if (!ss->vdp1_regs.empty()) { caps |= SE_CAP_VDP1_REGS; out->read_vdp1_reg = CbVdp1Reg; }
    if (!ss->vdp2_regs.empty()) { caps |= SE_CAP_VDP2_REGS; out->read_vdp2_reg = CbVdp2Reg; }

    out->capabilities = caps;
    out->close = CbClose;
}

}  // namespace

extern "C" {

int se_savestate_open_region_dir(const char* dir, se_data_source* out) {
    if (!dir || !out) return 1;
    std::memset(out, 0, sizeof(*out));
    Savestate* ss = new (std::nothrow) Savestate();
    if (!ss) return 2;

    std::string base(dir);
    if (!base.empty() && base.back() != '/' && base.back() != '\\') base += '/';

    LoadFile(base + "vdp1_vram.bin", ss->vdp1_vram);
    LoadFile(base + "vdp2_vram.bin", ss->vdp2_vram);
    LoadFile(base + "cram.bin",      ss->cram);
    LoadFile(base + "wram_low.bin",  ss->wram_low);
    LoadFile(base + "wram_high.bin", ss->wram_high);
    LoadFile(base + "vdp1_regs.bin", ss->vdp1_regs);
    LoadFile(base + "vdp2_regs.bin", ss->vdp2_regs);

    if (ss->vdp1_vram.empty()) { delete ss; return 3; }  // need at least VDP1 VRAM
    BuildDataSource(ss, out);
    return 0;
}

int se_savestate_open_full_dump(const char* path, uint32_t base_address,
                                se_data_source* out) {
    if (!path || !out) return 1;
    std::memset(out, 0, sizeof(*out));

    std::vector<uint8_t> dump;
    if (!LoadFile(path, dump) || dump.empty()) return 4;

    Savestate* ss = new (std::nothrow) Savestate();
    if (!ss) return 2;

    SliceRegion(dump, base_address, kAddrVdp1Vram, kSizeVdp1Vram, ss->vdp1_vram);
    SliceRegion(dump, base_address, kAddrVdp1Regs, kSizeVdp1Regs, ss->vdp1_regs);
    SliceRegion(dump, base_address, kAddrVdp2Vram, kSizeVdp2Vram, ss->vdp2_vram);
    SliceRegion(dump, base_address, kAddrCram,     kSizeCram,     ss->cram);
    SliceRegion(dump, base_address, kAddrVdp2Regs, kSizeVdp2Regs, ss->vdp2_regs);
    SliceRegion(dump, base_address, kAddrWramLow,  kSizeWramLow,  ss->wram_low);
    SliceRegion(dump, base_address, kAddrWramHigh, kSizeWramHigh, ss->wram_high);

    if (ss->vdp1_vram.empty()) { delete ss; return 3; }
    BuildDataSource(ss, out);
    return 0;
}

}  // extern "C"
