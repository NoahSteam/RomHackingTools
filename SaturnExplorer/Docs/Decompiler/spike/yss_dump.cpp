// Extracts both SH-2 work RAM regions and both CPUs' register files from a Yabause .yss
// through SaturnExplorer's own savestate driver, so the spike decompiles exactly the
// bytes SaturnExplorer's ContextBackend would hand a panel.
#include "SavestateDriver.h"
#include <cstdio>
#include <vector>
#include <string>
int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: yss_dump <file.yss> <out-prefix>\n"); return 2; }
    se_data_source src{};
    se_result r = se_savestate_open_yss(argv[1], &src);
    if (r != SE_OK) { fprintf(stderr, "open failed: %d\n", (int)r); return 1; }
    struct { const char* name; uint32_t base; } regions[] = { {"lwram", 0x00200000u}, {"hwram", 0x06000000u} };
    for (auto& rg : regions) {
        std::vector<uint8_t> buf(0x100000);
        size_t n = src.read_main_ram(src.user, rg.base, buf.data(), buf.size());
        std::string path = std::string(argv[2]) + "_" + rg.name + ".bin";
        FILE* f = fopen(path.c_str(), "wb"); fwrite(buf.data(), 1, n, f); fclose(f);
        printf("%s @%08X: %zu bytes -> %s\n", rg.name, rg.base, n, path.c_str());
    }
    for (int cpu = 0; cpu < 2; ++cpu) {
        se_sh2_regs regs{};
        if (src.read_sh2_regs && src.read_sh2_regs(src.user, cpu, &regs)) {
            printf("%s: pc=%08X pr=%08X sr=%08X r15=%08X r14=%08X r4=%08X\n",
                   cpu ? "slave " : "master", regs.pc, regs.pr, regs.sr, regs.r[15], regs.r[14], regs.r[4]);
        } else printf("%s: no regs\n", cpu ? "slave" : "master");
    }
    if (src.close) src.close(src.user);
    return 0;
}
