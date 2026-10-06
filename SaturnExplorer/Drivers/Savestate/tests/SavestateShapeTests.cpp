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

// "YSS" then the writer's host endianness: 1 little, 0 big. Real Yabause-family states carry 1,
// and the parser's every multi-byte read assumes it.
std::vector<uint8_t> YssHeader()
{
    std::vector<uint8_t> f{ 'Y', 'S', 'S', 1 };
    f.resize(kYssHeaderSize, 0);
    return f;
}

// A VDP2 section whose register struct is all zeroes, which is what a blanked-display state
// holds -- and passes the register sanity check, since every bit it tests is a zero.
std::vector<uint8_t> PlausibleVdp2Section()
{
    return std::vector<uint8_t>(kVdp2RegSize + kVramSize + kYssCramSize, 0);
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


// A section whose header promises more bytes than the file holds is a damaged state. It used
// to stop the walk and accept whatever had been recovered before it, so a VDP2-only prefix of
// a cut-off file loaded as if it were the whole machine.
void TestTruncatedMednafenSectionRefused()
{
    std::vector<uint8_t> vdp2sec;
    AddMdfnField(vdp2sec, "RawRegs", std::vector<uint8_t>(0x200, 0x5A));
    std::vector<uint8_t> file = MdfnHeader();
    AddMdfnSection(file, "VDP2", vdp2sec);
    se_data_source whole{};
    const se_result ok = se_savestate_open_buffer(file.data(), file.size(), &whole);
    CHECK(ok == SE_OK);
    if (ok == SE_OK && whole.close) whole.close(whole.user);

    // Append a MAIN header declaring 1000 bytes, then only 10 of them.
    const size_t at = file.size();
    file.resize(at + 32, 0);
    std::memcpy(&file[at], "MAIN", 4);
    Put32LE(file, 1000);
    file.insert(file.end(), 10, 0);
    se_data_source ds{};
    CHECK(se_savestate_open_buffer(file.data(), file.size(), &ds) != SE_OK);
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


// --- OFF-01: a layout we do not decode has to be refused, not decoded anyway ---

// A big-endian-host .yss. Every multi-byte read in the parser is little-endian, so this would
// have produced section sizes off by orders of magnitude and, past those, plausible-looking
// nonsense. Recognized and refused is the honest answer.
void TestBigEndianYssRefused()
{
    std::vector<uint8_t> file = YssHeader();
    file[3] = 0;
    AddYssSection(file, "VDP2", PlausibleVdp2Section());
    se_data_source ds{};
    CHECK(se_savestate_open_buffer(file.data(), file.size(), &ds) != SE_OK);
}

// Anything but 0 or 1 in that byte is not this family's header at all.
void TestUnknownEndiannessByteRefused()
{
    std::vector<uint8_t> file = YssHeader();
    file[3] = 0x20;
    AddYssSection(file, "VDP2", PlausibleVdp2Section());
    se_data_source ds{};
    CHECK(se_savestate_open_buffer(file.data(), file.size(), &ds) != SE_OK);
}

// A desynchronized chain: the section after a good one starts with bytes that are not a tag.
// The parser used to keep walking, so a later stretch of arbitrary bytes that happened to spell
// "VDP2" would be decoded at an offset that means nothing. Now it stops -- and because it stops
// by refusing, the caller does not receive the earlier section dressed up as a whole state.
void TestDesynchronizedChainRefused()
{
    std::vector<uint8_t> file = YssHeader();
    AddYssSection(file, "VDP2", PlausibleVdp2Section());

    // A well-formed section header in every respect except the tag: a small in-bounds size, so
    // the truncation check below has nothing to say about it and this really does test the tag.
    // The first version of this test used a garbage size too, and passed with the tag check
    // removed.
    const char binaryTag[4] = { '\0', '\1', '\2', '\3' };
    file.insert(file.end(), binaryTag, binaryTag + 4);
    Put32LE(file, 1);
    Put32LE(file, 4);
    file.insert(file.end(), { 0, 0, 0, 0 });

    se_data_source ds{};
    CHECK(se_savestate_open_buffer(file.data(), file.size(), &ds) != SE_OK);
}

// A section whose declared size runs past the end of the file. This used to keep whatever had
// decoded before it, reporting a truncated file as a complete state.
void TestTruncatedSectionRefused()
{
    std::vector<uint8_t> file = YssHeader();
    AddYssSection(file, "VDP2", PlausibleVdp2Section());
    file.insert(file.end(), { 'O', 'T', 'H', 'R' });
    Put32LE(file, 1);
    Put32LE(file, 0x400000);     // claims 4 MiB; the file ends here
    se_data_source ds{};
    CHECK(se_savestate_open_buffer(file.data(), file.size(), &ds) != SE_OK);
}

// A fork with a larger VDP1 VRAM keeps a section size that still satisfies `size >= 512 KiB`,
// and the trailing-512-KiB heuristic would hand back the wrong half of its VRAM. The register
// prefix is 52 bytes in a real state; half a megabyte of it is a different layout.
void TestOversizedVdp1PrefixSkipped()
{
    std::vector<uint8_t> vdp1(kVramSize * 2, 0x11);
    std::vector<uint8_t> file = YssHeader();
    AddYssSection(file, "VDP1", vdp1);
    se_data_source ds{};
    // Nothing else in the file, so refusing the section means refusing the state.
    CHECK(se_savestate_open_buffer(file.data(), file.size(), &ds) != SE_OK);
}

// The case OFF-01 actually names: a section of exactly the right size whose fields have moved,
// so the recovered register image is not a register image. TVMD bits 3 and 9-14 are unused on
// the hardware and read zero in every real state, so a set one means the struct is not ours.
// VDP1 still decodes -- the documented degrade for a fork whose VDP2 layout we do not share.
void TestMovedVdp2StructDegradesToVdp1Only()
{
    std::vector<uint8_t> vdp1(kVramSize + 52, 0x22);
    std::vector<uint8_t> vdp2 = PlausibleVdp2Section();
    vdp2[0] = 0xFF;   // TVMD (struct offset 0, host-endian) with reserved bits set
    vdp2[1] = 0xFF;

    std::vector<uint8_t> file = YssHeader();
    AddYssSection(file, "VDP1", vdp1);
    AddYssSection(file, "VDP2", vdp2);

    se_data_source ds{};
    const se_result r = se_savestate_open_buffer(file.data(), file.size(), &ds);
    CHECK(r == SE_OK);
    if (r != SE_OK) return;
    CHECK((ds.capabilities & SE_CAP_VDP1_VRAM) != 0);
    CHECK((ds.capabilities & SE_CAP_VDP2_VRAM) == 0);
    CHECK((ds.capabilities & SE_CAP_VDP2_REGS) == 0);
    CHECK((ds.capabilities & SE_CAP_CRAM) == 0);
    if (ds.close) ds.close(ds.user);
}

// The same file with the struct in place keeps its VDP2 data, so the check above is a shape
// test and not a blanket refusal of every VDP2 section.
void TestPlausibleVdp2StructKept()
{
    std::vector<uint8_t> file = YssHeader();
    AddYssSection(file, "VDP1", std::vector<uint8_t>(kVramSize + 52, 0x22));
    AddYssSection(file, "VDP2", PlausibleVdp2Section());

    se_data_source ds{};
    const se_result r = se_savestate_open_buffer(file.data(), file.size(), &ds);
    CHECK(r == SE_OK);
    if (r != SE_OK) return;
    CHECK((ds.capabilities & SE_CAP_VDP2_VRAM) != 0);
    CHECK((ds.capabilities & SE_CAP_VDP2_REGS) != 0);
    if (ds.close) ds.close(ds.user);
}

}  // namespace

int main()
{
    TestVdp2OnlyYssOpens();
    TestEmptyYssRefused();
    TestOddLengthMednafenU16FieldRefused();
    TestTruncatedMednafenSectionRefused();
    TestWorkRamOnlyFullDumpOpens();
    TestDumpCoveringNoRegionRefused();
    TestBigEndianYssRefused();
    TestUnknownEndiannessByteRefused();
    TestDesynchronizedChainRefused();
    TestTruncatedSectionRefused();
    TestOversizedVdp1PrefixSkipped();
    TestMovedVdp2StructDegradesToVdp1Only();
    TestPlausibleVdp2StructKept();
    if (gFailures)
    {
        std::printf("SavestateShapeTests: %d check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("SavestateShapeTests: all checks passed\n");
    return 0;
}
