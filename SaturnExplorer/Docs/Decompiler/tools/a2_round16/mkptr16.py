#!/usr/bin/env python3
# usage: mkptr16.py <Sh2GeneratedTests.cpp of the tree> <output .cpp>
# The memory mode of the printed-C differential (area 2 round 16). The harness skips every text that dereferences a pointer (the class 'skipped:memory'; the
# generated register-only functions never make one), so the value flow through memory (a load after a store, the order and the width of accesses, a
# post-increment, aliasing pointers) has had no by-value oracle in the printed text. With SE_GENERATED_PRINTEDC_PTR=<w> (w = 1, 2 or 4) and
# SE_GENERATED_PRINTEDC_WORDS the scratch copy this script writes runs such a function: r4 is a pointer into a 16 KiB arena (a work RAM address for the
# emulator, the harness's MAP_32BIT frame block for the compiled text), r5 is r4, a neighbour of it or a pointer to a second area, the arena is filled
# with a pattern that depends on the input row, and after the call the arena (a checksum) must equal what the emulator left, besides the return value.
# The machine is big-endian and the host is not: every access of a function must have the width w (the generator's families 91 to 93 do), and the arena's
# bytes are compared as w-byte units, so the endianness cancels.
import sys
src = open(sys.argv[1]).read()

def rep(old, new, count=1):
    global src
    assert src.count(old) == count, (old, src.count(old))
    src = src.replace(old, new)

rep("const uint32_t kStackTop = 0x06040000u;", "const uint32_t kPtrBase = 0x06080000u;   // round 16 memory mode: the arena of r4 and r5 (16 KiB)\nconst uint32_t kStackTop = 0x06040000u;")
rep("        const bool frameMode = UsesFrameMemory(c.words);", r'''        static const bool ptrMode = std::getenv("SE_GENERATED_PRINTEDC_PTR") != nullptr;
        static const uint32_t ptrW = ptrMode ? static_cast<uint32_t>(std::atoi(std::getenv("SE_GENERATED_PRINTEDC_PTR"))) : 4u;
        const bool frameMode = UsesFrameMemory(c.words) || ptrMode;''')
rep(r'''            for (int i = 0; i < 8; ++i) machine->Set(("r" + std::to_string(i)).c_str(), in[i]);
            for (int i = 8; i < 15; ++i) machine->Set(("r" + std::to_string(i)).c_str(), CalleeSaved(i));
            machine->Set("r15", kStackTop);
            machine->Set("pr", kSentinel);
            machine->Set("sr", in[8]);
            machine->Set("gbr", in[11]);''', r'''            if (ptrMode)
            {
                in[4] = kPtrBase + 0x1000 + 4 * ir.Below(64);
                const uint32_t al = ir.Below(6);
                in[5] = al == 0 ? in[4] : al == 1 ? in[4] + 4 * (1 + ir.Below(4)) : al == 2 ? in[4] - 4 * (1 + ir.Below(4)) : al == 3 ? in[4] + 8 : kPtrBase + 0x2000 + 4 * ir.Below(64);
                for (uint32_t u = 0; u < 0x4000u / ptrW; ++u)
                {
                    const uint32_t v = ((u * 2654435761u) ^ (static_cast<uint32_t>(kept) * 40503u)) >> 13;
                    for (uint32_t b = 0; b < ptrW; ++b) env.ram[((kPtrBase - 0x06000000u) + u * ptrW + b) & 0xFFFFFu] = static_cast<uint8_t>(v >> (8 * (ptrW - 1 - b)));
                }
            }
            for (int i = 0; i < 8; ++i) machine->Set(("r" + std::to_string(i)).c_str(), in[i]);
            for (int i = 8; i < 15; ++i) machine->Set(("r" + std::to_string(i)).c_str(), CalleeSaved(i));
            machine->Set("r15", kStackTop);
            machine->Set("pr", kSentinel);
            machine->Set("sr", in[8]);
            machine->Set("gbr", in[11]);''')
rep("            rows << buf;\n", r'''            rows << buf;
            if (ptrMode)
            {
                uint32_t cs = 2166136261u;
                for (uint32_t u = 0; u < 0x4000u / ptrW; ++u)
                {
                    uint32_t unit = 0;
                    for (uint32_t b = 0; b < ptrW; ++b) unit = (unit << 8) | env.ram[((kPtrBase - 0x06000000u) + u * ptrW + b) & 0xFFFFFu];
                    cs = (cs * 16777619u) ^ unit;
                }
                mcs << cs << "u,";
            }
''')
rep("        std::ostringstream rows;\n        int kept = 0;", "        std::ostringstream rows, mcs;\n        int kept = 0;")
rep('''        for (int i = 0; i < nParams && i < 4; ++i) call << (i ? ", " : "") << "(int)g_in[" << 4 + i << "]";''', r'''        for (int i = 0; i < nParams && i < 4; ++i)
        {
            if (ptrMode && i < 2)
            {
                size_t b = 0;
                for (int j = 0; j < i; ++j) b = plist.find(',', b) + 1;
                const size_t e = plist.find(',', b);
                const std::string decl = plist.substr(b, e == std::string::npos ? std::string::npos : e - b);
                const std::string addr = "(g_frame_base + 0x8000 + (g_in[" + std::to_string(4 + i) + "] - 0x06080000u))";
                call << (i ? ", " : "") << (decl.find('*') != std::string::npos ? "(void *)(uintptr_t)" + addr : "(int)" + addr);
            }
            else call << (i ? ", " : "") << "(int)g_in[" << 4 + i << "]";
        }''')
rep('''        m << "  { /* " << pr.name << " */\\n    static const unsigned int D[][18] = {\\n" << rows.str() << "    };\\n    for (unsigned k = 0; k < " << kept << "; ++k) {\\n";''', r'''        const std::string ptrU = ptrW == 1 ? "unsigned char" : ptrW == 2 ? "unsigned short" : "unsigned int";
        m << "#ifndef PTRPAT\n#define PTRPAT(k, a) ((((unsigned)(a) * 2654435761u) ^ ((unsigned)(k) * 40503u)) >> 13)\n#endif\n";
        m << "  { /* " << pr.name << " */\n    static const unsigned int D[][18] = {\n" << rows.str() << "    };\n";
        if (ptrMode) m << "    static const unsigned int MC[] = { " << mcs.str() << " 0 };\n";
        m << "    for (unsigned k = 0; k < " << kept << "; ++k) {\n";''')
rep('''        if (frameMode) m << "      memset((void *)(uintptr_t)g_frame_base, 0, 0x10000);\\n";
        if (v.voidReturn)''', r'''        if (frameMode) m << "      memset((void *)(uintptr_t)g_frame_base, 0, 0x10000);\n";
        if (ptrMode) m << "      { " << ptrU << " *ar = (" << ptrU << " *)(uintptr_t)(g_frame_base + 0x8000); for (unsigned a = 0; a < 0x4000 / " << ptrW << "; ++a) ar[a] = (" << ptrU << ")PTRPAT(k, a); }\n";
        if (v.voidReturn)''')
rep('''            m << ctl;
        }
        m << "    }\\n";''', r'''            m << ctl;
        }
        if (ptrMode)
            m << "      { " << ptrU << " *ar = (" << ptrU << " *)(uintptr_t)(g_frame_base + 0x8000); unsigned cs = 2166136261u; for (unsigned a = 0; a < 0x4000 / " << ptrW
              << "; ++a) cs = (cs * 16777619u) ^ ar[a];\n        if (cs != MC[k]) { printf(\"MISMATCH " << pr.name
              << " memory r4=%08X r5=%08X r6=%08X r7=%08X after the call\\n\", g_in[4], g_in[5], g_in[6], g_in[7]); ++bad_" << pr.name << "; } }\n";
        m << "    }\n";''')
rep("        call << \")\";\n", r'''        call << ")";
        if (ptrMode && ret.find('*') != std::string::npos)
        {
            // a pointer result is compared as the 32-bit number it stands for (the host's pointer is 64 bits wide)
            const std::string t = "(unsigned int)(uintptr_t)" + call.str();
            call.str("");
            call << t;
        }
''')
open(sys.argv[2], 'w').write(src)
