// A fake Seam A data source backed by plain vectors — the smallest thing se_create()
// accepts, so a test can write VDP1 commands and VDP2 registers straight into memory and
// render them through the real core.
//
// Shared because the per-file copies of this mock had started to drift: the VDP1
// framebuffer and VDP1 register readers exist in se-render's source but were missing from
// the test mocks, so a test could not reach any code path that needs them without
// silently reading zeroes.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "saturnexplorer/SeHost.h"

namespace se_test
{

// Emulator memory as the core sees it. 'vdp1Bytes' sizes VDP1 VRAM — large enough for the
// command table plus whatever texture data the test writes beyond it.
struct State
{
    explicit State(size_t vdp1Bytes = 0x40)
        : vdp1(vdp1Bytes), vdp2(512 * 1024), cram(4 * 1024), vdp1Fb(0x40000)
    {
    }

    std::vector<uint8_t> vdp1;
    std::vector<uint8_t> vdp2;
    std::vector<uint8_t> cram;
    std::vector<uint8_t> vdp1Fb;
    uint16_t vdp1Regs[0x10] = {};
    uint16_t regs[0x90] = {};
};

// VDP registers and the VDP1 command table are big-endian, as on the hardware.
inline void PutBE16(std::vector<uint8_t>& memory, uint32_t address, uint16_t value)
{
    memory[address] = static_cast<uint8_t>(value >> 8);
    memory[address + 1] = static_cast<uint8_t>(value);
}

inline void SetReg(State& state, uint32_t offset, uint16_t value)
{
    state.regs[offset >> 1] = value;
}

// The VDP1 system-clip command at address 0. This is what fixes the composited frame
// size, so every scene needs one before its first sprite.
inline void WriteSystemClip(State& state, int width, int height)
{
    PutBE16(state.vdp1, 0x00, 0x0009);
    PutBE16(state.vdp1, 0x14, static_cast<uint16_t>(width - 1));
    PutBE16(state.vdp1, 0x16, static_cast<uint16_t>(height - 1));
}

namespace detail
{
inline size_t CopyOut(const std::vector<uint8_t>& source, uint32_t offset,
                      void* dst, size_t size)
{
    if (offset >= source.size()) return 0;
    const size_t count = std::min(size, source.size() - offset);
    std::memcpy(dst, source.data() + offset, count);
    return count;
}

inline uint16_t ReadReg(const uint16_t* regs, size_t count, uint32_t offset)
{
    return (offset >> 1) < count ? regs[offset >> 1] : 0;
}
}  // namespace detail

// Every reader wired to 'state', with the matching read capabilities. Returned by value so
// a test that needs more — a write_vram hook, a narrower capability set — can adjust it
// before calling se_create() itself.
inline se_data_source MakeSource(State& state)
{
    se_data_source source = {};
    source.abi_version = SE_ABI_VERSION;
    source.capabilities = SE_CAP_VDP1_VRAM | SE_CAP_VDP2_VRAM | SE_CAP_CRAM |
                          SE_CAP_VDP1_REGS | SE_CAP_VDP2_REGS | SE_CAP_VDP1_FB;
    source.user = &state;
    source.read_vdp1_vram = [](void* u, uint32_t o, void* d, size_t n)
    { return detail::CopyOut(static_cast<State*>(u)->vdp1, o, d, n); };
    source.read_vdp2_vram = [](void* u, uint32_t o, void* d, size_t n)
    { return detail::CopyOut(static_cast<State*>(u)->vdp2, o, d, n); };
    source.read_cram = [](void* u, uint32_t o, void* d, size_t n)
    { return detail::CopyOut(static_cast<State*>(u)->cram, o, d, n); };
    source.read_vdp1_fb = [](void* u, uint32_t o, void* d, size_t n)
    { return detail::CopyOut(static_cast<State*>(u)->vdp1Fb, o, d, n); };
    source.read_vdp1_reg = [](void* u, uint32_t o)
    { return detail::ReadReg(static_cast<State*>(u)->vdp1Regs, 0x10, o); };
    source.read_vdp2_reg = [](void* u, uint32_t o)
    { return detail::ReadReg(static_cast<State*>(u)->regs, 0x90, o); };
    return source;
}

// Returns the context, or null if se_create() rejected the source — callers assert.
inline se_context* CreateContext(const se_data_source& source)
{
    se_config config = {};
    config.abi_version = SE_ABI_VERSION;
    return se_create(&source, &config);
}

inline se_context* CreateContext(State& state)
{
    return CreateContext(MakeSource(state));
}

}  // namespace se_test
