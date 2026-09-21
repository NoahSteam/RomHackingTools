// Sh2RegInfo: the SH-2 register table, value extraction and the SR flag decode that back
// the Registers panel's "SH-2" tab. Pure -- no context, no window.

#include <cstring>
#include <iostream>
#include <string>

#include "Debug/Sh2RegInfo.h"

using namespace sfe;

namespace
{
int gFailures = 0;

void Check(bool condition, const char* expression, int line)
{
    if (condition) return;
    std::cerr << "CHECK failed at line " << line << ": " << expression << '\n';
    ++gFailures;
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

void TestValueMapping()
{
    se_sh2_regs r = {};
    for (int i = 0; i < 16; ++i) r.r[i] = 0x1000u + static_cast<uint32_t>(i);
    r.pc = 0x06001234; r.pr = 0x06005678; r.sr = 0x000000F1;
    r.gbr = 0x20000000; r.vbr = 0x06000400;
    r.mach = 0xAAAAAAAA; r.macl = 0xBBBBBBBB;

    for (int i = 0; i < 16; ++i)
        CHECK(Sh2RegValue(r, i) == 0x1000u + static_cast<uint32_t>(i));
    CHECK(Sh2RegValue(r, kSh2RegPc) == 0x06001234u);
    CHECK(Sh2RegValue(r, kSh2RegPr) == 0x06005678u);
    CHECK(Sh2RegValue(r, kSh2RegSr) == 0x000000F1u);
    CHECK(Sh2RegValue(r, kSh2RegGbr) == 0x20000000u);
    CHECK(Sh2RegValue(r, kSh2RegVbr) == 0x06000400u);
    CHECK(Sh2RegValue(r, kSh2RegMach) == 0xAAAAAAAAu);
    CHECK(Sh2RegValue(r, kSh2RegMacl) == 0xBBBBBBBBu);

    // Out of range is clamped to 0 rather than reading past the struct.
    CHECK(Sh2RegValue(r, -1) == 0u);
    CHECK(Sh2RegValue(r, kSh2RegCount) == 0u);
    CHECK(Sh2RegValue(r, 9999) == 0u);
}

void TestTableShape()
{
    const Sh2RegField* t = Sh2RegTable();
    for (int i = 0; i < kSh2RegCount; ++i)
    {
        CHECK(t[i].name != nullptr && t[i].name[0] != '\0');
        CHECK(t[i].desc != nullptr && t[i].desc[0] != '\0');
    }
    // The named indices must line up with the table, or a tooltip would describe the wrong
    // register -- the one bug this table can have that still compiles.
    CHECK(std::strcmp(t[0].name, "R0") == 0);
    CHECK(std::strcmp(t[15].name, "R15") == 0);   // R15 is the stack pointer
    CHECK(std::strcmp(t[kSh2RegPc].name, "PC") == 0);
    CHECK(std::strcmp(t[kSh2RegPr].name, "PR") == 0);
    CHECK(std::strcmp(t[kSh2RegSr].name, "SR") == 0);
    CHECK(std::strcmp(t[kSh2RegGbr].name, "GBR") == 0);
    CHECK(std::strcmp(t[kSh2RegVbr].name, "VBR") == 0);
    CHECK(std::strcmp(t[kSh2RegMach].name, "MACH") == 0);
    CHECK(std::strcmp(t[kSh2RegMacl].name, "MACL") == 0);
}

void TestSrDecode()
{
    // All clear.
    CHECK(Sh2SrSummary(0x00000000) == "T=0 S=0 I=0 M=0 Q=0");
    // Each field alone, so a shifted-by-one mask can't hide behind a neighbour.
    CHECK(Sh2SrSummary(0x00000001) == "T=1 S=0 I=0 M=0 Q=0");   // T   = bit 0
    CHECK(Sh2SrSummary(0x00000002) == "T=0 S=1 I=0 M=0 Q=0");   // S   = bit 1
    CHECK(Sh2SrSummary(0x00000010) == "T=0 S=0 I=1 M=0 Q=0");   // I   = bits 4-7
    CHECK(Sh2SrSummary(0x000000F0) == "T=0 S=0 I=15 M=0 Q=0");
    CHECK(Sh2SrSummary(0x00000100) == "T=0 S=0 I=0 M=0 Q=1");   // Q   = bit 8
    CHECK(Sh2SrSummary(0x00000200) == "T=0 S=0 I=0 M=1 Q=0");   // M   = bit 9
    // The reserved/undefined bits must not bleed into any field.
    CHECK(Sh2SrSummary(0xFFFFFC0C) == "T=0 S=0 I=0 M=0 Q=0");
    // A realistic halted value: interrupts fully masked, T set.
    CHECK(Sh2SrSummary(0x000000F1) == "T=1 S=0 I=15 M=0 Q=0");

    // The detail text is what the SR row's tooltip shows; check it tracks the same fields
    // and annotates the two mask extremes.
    CHECK(Sh2SrDetail(0x00000000).find("I  = 0") != std::string::npos);
    CHECK(Sh2SrDetail(0x00000000).find("none") != std::string::npos);
    CHECK(Sh2SrDetail(0x000000F0).find("all but NMI") != std::string::npos);
    CHECK(Sh2SrDetail(0x00000070).find("I  = 7") != std::string::npos);
    CHECK(Sh2SrDetail(0x00000070).find("none") == std::string::npos);
    CHECK(Sh2SrDetail(0x00000003).find("T  = 1") != std::string::npos);
    CHECK(Sh2SrDetail(0x00000003).find("S  = 1") != std::string::npos);
}
}  // namespace

int main()
{
    TestValueMapping();
    TestTableShape();
    TestSrDecode();
    if (gFailures)
    {
        std::cerr << gFailures << " check(s) failed\n";
        return 1;
    }
    std::cout << "Sh2RegInfo tests passed\n";
    return 0;
}
