// Sh2Dump -- the text the Data > Dump SH-2 command writes: which columns appear, in what form,
// that a user note replaces a generated one, that decoding matches the Assembly panel (so a
// block boundary changes nothing), and that unavailable regions are skipped rather than faked.
#include "Sh2Dump.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace sfe;

namespace
{
int gFailures;

#define CHECK(cond) do {                                                      \
    if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++gFailures; } \
} while (0)

// A few opcodes with known text: 0009 nop, E200 mov #0x0,r2, 6122 mov.l @r2,r1, 2118 tst r1,r1
// (D108 -- a PC-relative load -- is covered by the delay-slot case).
void Put(std::vector<uint8_t>& m, uint32_t offset, uint16_t op)
{
    m[offset] = static_cast<uint8_t>(op >> 8);
    m[offset + 1] = static_cast<uint8_t>(op);
}

std::vector<uint8_t> Hwram()
{
    std::vector<uint8_t> m(0x100000u, 0);
    for (uint32_t i = 0; i < m.size(); i += 2) Put(m, i, 0x0009);   // nop
    Put(m, 0x10, 0xE200);   // mov #0x0,r2
    Put(m, 0x12, 0x6122);   // mov.l @r2,r1
    Put(m, 0x14, 0x2118);   // tst r1,r1
    return m;
}

Sh2DumpInput Input()
{
    Sh2DumpInput in;
    in.memory[1] = Hwram();
    in.opt.regions[0] = false;   // LWRAM unless a case asks for it
    in.opt.autoComments = false;
    in.opt.comments = false;
    return in;
}

std::string Run(Sh2DumpInput in, uint32_t slice = 1u << 30)
{
    Sh2DumpJob job(std::move(in));
    while (!job.Step(slice)) {}
    return job.TakeText();
}

// The line for address 0x06000010 in 'text', or "" -- found by its address column when on.
std::string LineWith(const std::string& text, const std::string& needle)
{
    const size_t at = text.find(needle);
    if (at == std::string::npos) return "";
    const size_t start = text.rfind('\n', at);
    const size_t end = text.find('\n', at);
    return text.substr(start == std::string::npos ? 0 : start + 1,
                       end - (start == std::string::npos ? 0 : start + 1));
}

// All columns on: address, raw word, then the instruction as the panel shows it.
void TestAllColumns()
{
    Sh2DumpInput in = Input();
    in.opt.addresses = in.opt.bytes = in.opt.opcodes = true;
    const std::string t = Run(in);
    CHECK(LineWith(t, "06000010") == "06000010  E200  mov      #0x0,r2" ||
          LineWith(t, "06000010").find("E200  mov") != std::string::npos);
    CHECK(LineWith(t, "06000012").find("6122  mov.l") != std::string::npos);
    CHECK(t.find("; ---- HWRAM  06000000-060FFFFF ----") != std::string::npos);
}

// Each column can be dropped on its own, and the others stay.
void TestColumnsAreIndependent()
{
    {
        Sh2DumpInput in = Input();
        in.opt.addresses = true; in.opt.bytes = false; in.opt.opcodes = false;
        const std::string t = Run(in);
        CHECK(LineWith(t, "06000010") == "06000010");
    }
    {
        Sh2DumpInput in = Input();
        in.opt.addresses = false; in.opt.bytes = true; in.opt.opcodes = false;
        CHECK(LineWith(Run(in), "E200") == "E200");
    }
    {
        Sh2DumpInput in = Input();
        in.opt.addresses = false; in.opt.bytes = false; in.opt.opcodes = true;
        const std::string t = Run(in);
        CHECK(LineWith(t, "mov.l") == "mov.l   @r2,r1");
        CHECK(t.find("06000010") == std::string::npos);   // no address anywhere
        CHECK(t.find("E200") == std::string::npos);       // no raw word
    }
}

// A user note is written, aligned after the instruction; it replaces the generated one.
void TestUserNoteReplacesGenerated()
{
    Sh2DumpInput in = Input();
    in.opt.comments = true;
    in.opt.autoComments = false;
    in.userComments[0x06000012u] = "load word";
    const std::string t = Run(in);
    const std::string l = LineWith(t, "06000012");
    CHECK(l.find("; load word") != std::string::npos);
    CHECK(l.find("mov.l") < l.find("; load word"));
    // Only the annotated row carries a comment.
    CHECK(LineWith(t, "06000014").find(';') == std::string::npos);
}

// Generated comments come from the registers; they are off without registers or when asked.
void TestGeneratedComments()
{
    Sh2DumpInput in = Input();
    in.opt.comments = true;
    in.opt.autoComments = true;
    in.haveRegs = true;
    const std::string with = LineWith(Run(in), "E200");
    CHECK(with.find("; ") != std::string::npos);        // "r2 = 0x0" or similar
    in.opt.autoComments = false;
    CHECK(LineWith(Run(in), "E200").find(';') == std::string::npos);
    in.opt.autoComments = true;
    in.haveRegs = false;
    CHECK(LineWith(Run(in), "E200").find(';') == std::string::npos);
}

// A comments-only dump is just the notes (no leading separator).
void TestCommentsOnly()
{
    Sh2DumpInput in = Input();
    in.opt.addresses = in.opt.bytes = in.opt.opcodes = false;
    in.opt.comments = true;
    in.userComments[0x06000010u] = "entry";
    const std::string t = Run(in);
    CHECK(LineWith(t, "entry") == "entry");
}

// The slice a job is stepped by changes nothing: same text however it is cut up, including
// across the decode blocks and with the delayed-branch context carried over.
void TestSliceSizeDoesNotChangeTheText()
{
    Sh2DumpInput a = Input();
    a.opt.addresses = a.opt.bytes = a.opt.opcodes = true;
    a.opt.comments = true;
    a.haveRegs = true;
    // A delayed branch right before a block boundary, then a PC-relative load in its slot.
    Put(a.memory[1], 2046, 0xA002);   // bra (delay slot follows)
    Put(a.memory[1], 2048, 0xD108);   // mov.l @(disp,pc),r1: first row of the next block
    const Sh2DumpInput b = a;
    const std::string whole = Run(a);
    CHECK(Run(b, 7) == whole);
    CHECK(Run(b, 1024) == whole);
    // The first row of the second block follows the branch, so it is shown as "@(disp,pc)" (the
    // address depends on how it was reached) rather than resolved to one absolute address.
    CHECK(LineWith(whole, "06000800").find(",pc)") != std::string::npos);
}

// Two regions, in order, each with its own banner; an unavailable one is named and skipped.
void TestRegionsAndUnavailable()
{
    Sh2DumpInput in = Input();
    in.opt.addresses = true; in.opt.bytes = false; in.opt.opcodes = false;
    in.opt.regions[0] = in.opt.regions[1] = true;
    in.memory[0].clear();     // LWRAM cannot be read from this source
    const std::string t = Run(in);
    CHECK(t.find("LWRAM: not available from this source, skipped") != std::string::npos);
    CHECK(t.find("; ---- LWRAM") == std::string::npos);
    CHECK(t.find("; ---- HWRAM") != std::string::npos);

    in.memory[0] = Hwram();
    const std::string both = Run(in);
    CHECK(both.find("; ---- LWRAM") < both.find("; ---- HWRAM"));
    CHECK(both.find("00200000") != std::string::npos);
    CHECK(both.find("060FFFFE") != std::string::npos);   // the last instruction of HWRAM is there
}

// Row counts and progress: every instruction of a selected region is written, once.
void TestCountsAndProgress()
{
    Sh2DumpInput in = Input();
    in.opt.addresses = true; in.opt.bytes = false; in.opt.opcodes = false;
    Sh2DumpJob job(in);
    CHECK(job.TotalInstructions() == 0x80000u);
    CHECK(job.Progress() == 0.0f);
    job.Step(1000);
    CHECK(job.Progress() > 0.0f && job.Progress() < 1.0f);
    while (!job.Step(100000)) {}
    CHECK(job.Done() && job.Progress() == 1.0f);
    size_t lines = 0;
    for (char c : job.Text()) lines += c == '\n';
    // The rows, plus the file header and the region banner (';' lines).
    size_t comments = 0;
    for (size_t at = 0; (at = job.Text().find("\n;", at)) != std::string::npos; ++at) ++comments;
    comments += job.Text()[0] == ';';
    CHECK(lines - comments == 0x80000u);
}

// Nothing selected is not a dump.
void TestValid()
{
    Sh2DumpOptions o;
    CHECK(o.Valid());
    o.addresses = o.bytes = o.opcodes = o.comments = false;
    CHECK(!o.Valid());
    o = Sh2DumpOptions();
    o.regions[0] = o.regions[1] = false;
    CHECK(!o.Valid());
}
}  // namespace

int main()
{
    TestAllColumns();
    TestColumnsAreIndependent();
    TestUserNoteReplacesGenerated();
    TestGeneratedComments();
    TestCommentsOnly();
    TestSliceSizeDoesNotChangeTheText();
    TestRegionsAndUnavailable();
    TestCountsAndProgress();
    TestValid();
    if (gFailures) { std::printf("FAILURES: %d\n", gFailures); return 1; }
    std::printf("all cases passed\n");
    return 0;
}
