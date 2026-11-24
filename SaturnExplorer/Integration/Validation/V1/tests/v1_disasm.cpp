// Prints one line per 16-bit big-endian word of a raw SH-2 binary, decoded by the repo's own
// decoder (FrontEnd/src/Debug/Sh2Disasm.cpp): "<addr> <opcode> <mnemonic> <operands>|VBCD" where the
// four flag digits are IsValid, IsBranch, IsCall, HasDelaySlot. Used by check_v1.py to prove the
// emitted bytes are the intended instructions.
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include "Debug/Sh2Disasm.h"

int main(int argc, char** argv)
{
    if (argc < 3) { std::fprintf(stderr, "usage: v1_disasm <file> <base-hex>\n"); return 2; }
    std::FILE* f = std::fopen(argv[1], "rb");
    if (!f) return 2;
    std::vector<uint8_t> b;
    int c;
    while ((c = std::fgetc(f)) != EOF) b.push_back((uint8_t)c);
    std::fclose(f);
    const uint32_t base = (uint32_t)std::strtoul(argv[2], nullptr, 16);
    for (size_t i = 0; i + 1 < b.size(); i += 2)
    {
        const uint16_t op = (uint16_t)((b[i] << 8) | b[i + 1]);
        const sfe::DisassembledInstruction d = sfe::Sh2Decode(base + (uint32_t)i, op);
        std::printf("%08X %04X %s %s|%d%d%d%d\n", base + (unsigned)i, op, d.Mnemonic.c_str(), d.Operands.c_str(),
                    d.IsValid ? 1 : 0, d.IsBranch ? 1 : 0, d.IsCall ? 1 : 0, d.HasDelaySlot ? 1 : 0);
    }
    return 0;
}
