// A9-7 on a 32-bit size_t: a .yss whose MSH2 section declares 0xFFFFFFF4 bytes in a file of a few dozen.
// `data + size > file.size()` wraps in 32 bits, so the section is taken as present; ParseSh2Regs then reads 92
// bytes at `data`, past the end of the buffer, and the new position wraps back into the file (here onto the same header: the
// walk never ends). Built for wasm32 (the web build) by wasm32_yss_wrap.sh: before the fix the program does not return
// (the script's timeout reports it), after it the file is refused.
#include "SavestateDriver.h"

#include <cstdio>
#include <cstring>
#include <vector>

int main()
{
    std::vector<unsigned char> f = { 'Y', 'S', 'S', 1 };
    f.resize(0x14, 0);
    auto p32 = [&](unsigned v) { for (int i = 0; i < 4; ++i) f.push_back((unsigned char)(v >> (8 * i))); };
    // a plausible first section so the file has something in it: a VDP1 tag with a small size is skipped by the parser
    f.insert(f.end(), { 'J', 'U', 'N', 'K' });
    p32(1);
    p32(4);
    p32(0);
    // The MSH2 section declares 0xFFFFFFF4 bytes, which is -12: data + size wraps to the section's own header, the check
    // `data + size > file.size()` passes, and the walk lands on the same header again for ever. (A size that wraps to some
    // other earlier header would go round the chain the same way; one that wraps to the end of the file would take the
    // register copy from beyond the buffer.)
    f.insert(f.end(), { 'M', 'S', 'H', '2' });
    p32(1);
    p32(0xFFFFFFF4u);
    f.resize(f.size() + 8, 0xAB);
    std::printf("sizeof(size_t)=%zu file=%zu bytes\n", sizeof(size_t), f.size());
    se_data_source ds;
    std::memset(&ds, 0, sizeof ds);
    const se_result r = se_savestate_open_buffer(f.data(), f.size(), &ds);
    std::printf("se_savestate_open_buffer -> %d (%s)\n", (int)r, r == SE_OK ? "opened: the section of 4 GiB was taken as present" : "refused");
    if (r == SE_OK && ds.read_sh2_regs)
    {
        se_sh2_regs regs;
        std::memset(&regs, 0, sizeof regs);
        const int has = ds.read_sh2_regs(ds.user, 0, &regs);
        std::printf("master SH-2 registers offered: %d (r0=0x%08X pc=0x%08X, read from beyond the %zu-byte file)\n", has, (unsigned)regs.r[0], (unsigned)regs.pc, f.size());
    }
    if (ds.close) ds.close(ds.user);
    return r == SE_OK ? 1 : 0;
}
