// What the savestate parsers do with containers whose shape is not what the format promises,
// and with ones that are valid but hold only part of the machine.
//
// Both behaviours were previously asserted against the wrong thing: an odd-length u16 field was
// tested on the Yabause byte-swapper, whose callers pass compile-time even constants, and
// "a source with no VDP1 opens" was tested against a hand-built se_data_source, which does not
// go through a parser at all. So both tests passed while the real paths were unfixed. These
// synthesize the containers instead, which is the only way to reach the code a user's file does.
#include "SavestateDriver.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, const char* what, int line)
{
    if (ok) return;
    std::printf("CHECK failed at line %d: %s\n", line, what);
    ++gFailures;
}

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

// Sizes the .yss parser's structural match requires (see SavestateDriver.cpp).
const uint32_t kVramSize    = 0x80000;
const uint32_t kYssCramSize = 0x1000;
const uint32_t kVdp2RegSize = 288;
const size_t   kYssHeaderSize = 0x14;

void Put32LE(std::vector<uint8_t>& v, uint32_t x)
{
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
}

// A .yss section: tag(4) + version(4) + size(4 LE) + data.
void AddYssSection(std::vector<uint8_t>& f, const char* tag, const std::vector<uint8_t>& data)
{
    f.insert(f.end(), tag, tag + 4);
    Put32LE(f, 1);
    Put32LE(f, static_cast<uint32_t>(data.size()));
    f.insert(f.end(), data.begin(), data.end());
}

std::vector<uint8_t> YssHeader()
{
    std::vector<uint8_t> f{ 'Y', 'S', 'S', ' ' };
    f.resize(kYssHeaderSize, 0);
    return f;
}

// A VDP2-only .yss: registers + VRAM + CRAM, no VDP1 section anywhere. Before SNAP-02 reached
// the parsers this was rejected outright with SE_ERR_NO_DATA, so none of its backgrounds,
// tiles or palettes could be looked at.
void TestVdp2OnlyYssOpens()
{
    std::vector<uint8_t> vdp2(kVdp2RegSize + kVramSize + kYssCramSize, 0);
    for (size_t i = 0; i < kVramSize; ++i) vdp2[kVdp2RegSize + i] = static_cast<uint8_t>(i & 0xFF);

    std::vector<uint8_t> file = YssHeader();
    AddYssSection(file, "VDP2", vdp2);

    se_data_source ds{};
    const se_result r = se_savestate_open_buffer(file.data(), file.size(), &ds);
    CHECK(r == SE_OK);
    if (r != SE_OK) return;
    CHECK(ds.read_vdp2_vram != nullptr);
    uint8_t probe[8] = {};
    CHECK(ds.read_vdp2_vram(ds.user, 4, probe, sizeof(probe)) == sizeof(probe));
    CHECK(probe[0] == 4);   // the pattern written above, so this is really our VRAM
    if (ds.close) ds.close(ds.user);
}

// A .yss carrying no recognized section at all is still refused: the relaxation is "anything",
// not "everything".
void TestEmptyYssRefused()
{
    std::vector<uint8_t> file = YssHeader();
    AddYssSection(file, "JUNK", std::vector<uint8_t>(64, 0xAB));
    se_data_source ds{};
    CHECK(se_savestate_open_buffer(file.data(), file.size(), &ds) != SE_OK);
}

// ---- Mednafen MDFNSVST: 32-byte header, then 32-byte-named sections, then SFORMAT fields
//      (name-length(1) + name + size(4 LE) + payload). ----
void AddMdfnField(std::vector<uint8_t>& sec, const char* name, const std::vector<uint8_t>& payload)
{
    const uint8_t n = static_cast<uint8_t>(std::strlen(name));
    sec.push_back(n);
    sec.insert(sec.end(), name, name + n);
    Put32LE(sec, static_cast<uint32_t>(payload.size()));
    sec.insert(sec.end(), payload.begin(), payload.end());
}

void AddMdfnSection(std::vector<uint8_t>& f, const char* name, const std::vector<uint8_t>& sec)
{
    const size_t at = f.size();
    f.resize(at + 32, 0);
    std::memcpy(&f[at], name, std::strlen(name));
    Put32LE(f, static_cast<uint32_t>(sec.size()));
    f.insert(f.end(), sec.begin(), sec.end());
}

std::vector<uint8_t> MdfnHeader()
{
    std::vector<uint8_t> f;
    const char* magic = "MDFNSVST";
    f.insert(f.end(), magic, magic + 8);
    f.resize(32, 0);
    return f;
}

// A "RawRegs" field whose length is odd. It is declared as uint16[...], so an odd length means
// the field is not that array -- and the normalizer used to round down, copy the pairs, and
// leave the final byte at zero, producing a register image that is byte-swapped up to the seam
// and wrong after it. The state has to be refused, not half-converted.
//
// 'sz' here is the field header's own length, which is the one length in this path that the
// file chooses -- which is why this is the call that can be refused at all.
void TestOddLengthMednafenU16FieldRefused()
{
    std::vector<uint8_t> vdp2sec;
    AddMdfnField(vdp2sec, "RawRegs", std::vector<uint8_t>(0x201, 0x5A));   // odd: 513 bytes
    std::vector<uint8_t> file = MdfnHeader();
    AddMdfnSection(file, "VDP2", vdp2sec);

    se_data_source ds{};
    CHECK(se_savestate_open_buffer(file.data(), file.size(), &ds) != SE_OK);

    // The same field at an even length is not refused for this reason -- so the check is a
    // shape test, not a blanket rejection of the section.
    std::vector<uint8_t> evenSec;
    AddMdfnField(evenSec, "RawRegs", std::vector<uint8_t>(0x200, 0x5A));
    std::vector<uint8_t> evenFile = MdfnHeader();
    AddMdfnSection(evenFile, "VDP2", evenSec);
    se_data_source eds{};
    const se_result even = se_savestate_open_buffer(evenFile.data(), evenFile.size(), &eds);
    CHECK(even == SE_OK);
    if (even == SE_OK && eds.close) eds.close(eds.user);
}


// The other two openers were still gated on VDP1 VRAM after SNAP-02 relaxed the savestate
// parsers, so a source carrying only work RAM -- perfectly good for the hex editor, the
// debugger and a RAM search -- opened from a .yss but not from a raw dump of the same memory.
//
// Work RAM Low sits at 0x00200000, so a dump based there covers WRAM and nothing else: no VDP1
// VRAM, which is exactly the shape the old rule refused.
void TestWorkRamOnlyFullDumpOpens()
{
    std::vector<uint8_t> dump(64 * 1024, 0x42);
    se_data_source ds{};
    const se_result r = se_savestate_open_full_dump_buffer(dump.data(), dump.size(),
                                                           0x00200000u, &ds);
    CHECK(r == SE_OK);
    if (r != SE_OK) return;

    // And the region really is readable, rather than the open having succeeded on an empty state.
    CHECK((ds.capabilities & SE_CAP_MAIN_RAM) != 0);
    uint8_t byte = 0;
    // read_main_ram takes a Saturn bus address, not a region offset.
    CHECK(ds.read_main_ram && ds.read_main_ram(ds.user, 0x00200000u, &byte, 1) == 1);
    CHECK(byte == 0x42);
    if (ds.close) ds.close(ds.user);
}

// A dump based somewhere that maps no region at all still has to be refused: "anything at all"
// is not "nothing".
void TestDumpCoveringNoRegionRefused()
{
    std::vector<uint8_t> dump(64 * 1024, 0x42);
    se_data_source ds{};
    CHECK(se_savestate_open_full_dump_buffer(dump.data(), dump.size(), 0x01000000u, &ds) != SE_OK);
}

}  // namespace

int main()
{
    TestVdp2OnlyYssOpens();
    TestEmptyYssRefused();
    TestOddLengthMednafenU16FieldRefused();
    TestWorkRamOnlyFullDumpOpens();
    TestDumpCoveringNoRegionRefused();
    if (gFailures)
    {
        std::printf("SavestateShapeTests: %d check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("SavestateShapeTests: all checks passed\n");
    return 0;
}
