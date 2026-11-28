// Unit tests for FunctionExport: the text "Export SH-2 Function" and "Export Decompiled Code" write.
#include "Debug/FunctionExport.h"

#include <cstdio>
#include <map>

using namespace sfe;

namespace
{
int gFail = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; }
}
bool Has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

// A little memory: big-endian words at addresses; anything else is unreadable.
struct FakeMemory
{
    std::map<uint32_t, uint8_t> bytes;
    void Word(uint32_t at, uint16_t w) { bytes[at] = static_cast<uint8_t>(w >> 8); bytes[at + 1] = static_cast<uint8_t>(w); }
    ExportMemoryReader Reader()
    {
        return [this](uint32_t a, uint32_t n, std::vector<uint8_t>& out, std::string& err) {
            out.clear();
            for (uint32_t i = 0; i < n; ++i)
            {
                const auto it = bytes.find(a + i);
                if (it == bytes.end()) { err = "not captured"; return false; }
                out.push_back(it->second);
            }
            return true;
        };
    }
};
}  // namespace

int main()
{
    // Merging: sorted, touching and overlapping blocks joined, inverted ranges dropped.
    {
        const auto m = MergeCodeRanges({ { 0x100, 0x106 }, { 0x0F0, 0x0FE }, { 0x108, 0x10A }, { 0x200, 0x204 },
                                         { 0x202, 0x210 }, { 0x300, 0x2FE } });
        Check(m.size() == 2, "four blocks and an inverted one merge into two runs");
        Check(m.size() == 2 && m[0].first == 0x0F0 && m[0].second == 0x10A, "touching blocks join (0F0-0FE, 100-106, 108-10A)");
        Check(m.size() == 2 && m[1].first == 0x200 && m[1].second == 0x210, "overlapping blocks join");
        Check(MergeCodeRanges({}).empty(), "no ranges, no runs");
        const auto top = MergeCodeRanges({ { 0xFFFFFFFC, 0xFFFFFFFE }, { 0xFFFFFFF8, 0xFFFFFFFA } });
        Check(top.size() == 1 && top[0].second == 0xFFFFFFFE, "ranges at the top of memory merge without overflow");
    }

    // The listing: header, label, one line per instruction, a note, a gap line.
    {
        FakeMemory mem;
        mem.Word(0x06010468, 0xD102);   // mov.l @(disp,pc),r1
        mem.Word(0x0601046A, 0x4110);   // dt r1
        mem.Word(0x0601046C, 0x8BFD);   // bf
        mem.Word(0x0601046E, 0x000B);   // rts
        mem.Word(0x06010470, 0x0009);   // nop (delay slot)
        mem.Word(0x06010466, 0x0009);
        mem.Word(0x06010478, 0x0009);   // a second run after a 6-byte gap
        mem.Word(0x06010476, 0x0009);
        FunctionExportInfo info;
        info.name = "sub_010468";
        info.entry = 0x06010468;
        info.cpu = 0;
        info.source = "live, paused";
        info.frame = 34794;
        std::unordered_map<uint32_t, std::string> notes = { { 0x0601046A, "count down" } };
        std::string text, err;
        const bool ok = FormatSh2FunctionListing(info, { { 0x06010468, 0x06010470 }, { 0x06010478, 0x06010478 } },
                                                 mem.Reader(), &notes, text, err);
        Check(ok, "a readable function is listed");
        Check(Has(text, "; sub_010468 -- master SH-2, entry 06010468\n"), "header names the function, CPU and entry");
        Check(Has(text, "; memory: live, paused, frame 34794\n"), "header names the memory's source and frame");
        Check(Has(text, "\nsub_010468:\n"), "a label line before the code");
        Check(Has(text, "    06010468  D102  mov.l"), "first instruction with address and opcode");
        Check(Has(text, "0601046A  4110  dt       r1  ; count down\n"), "the user's note follows its instruction");
        Check(Has(text, "0601046E  000B  rts\n"), "a mnemonic with no operands has no trailing blanks");
        Check(Has(text, "; 06010472-06010477: not reached by the function's flow"), "the gap between runs is named");
        Check(Has(text, "    06010478  0009  nop\n"), "the second run is listed");
        Check(text.back() == '\n', "the file ends in a newline");
        Check(err.empty(), "no error on success");
    }

    // Unreadable memory and an empty function fail with a message and no text.
    {
        FakeMemory mem;
        mem.Word(0x06000000, 0x0009);
        FunctionExportInfo info;
        info.entry = 0x06000000;
        std::string text = "stale", err;
        Check(!FormatSh2FunctionListing(info, { { 0x06000000, 0x06000004 } }, mem.Reader(), nullptr, text, err),
              "a range memory does not hold fails");
        Check(text.empty(), "no partial text on failure");
        Check(Has(err, "06000000-06000005 cannot be read: not captured"), "the error names the range and the reason");
        err.clear();
        Check(!FormatSh2FunctionListing(info, {}, mem.Reader(), nullptr, text, err) && !err.empty(),
              "a function with no instructions fails with a reason");
        mem.Word(0x06000002, 0x0009);
        Check(FormatSh2FunctionListing(info, { { 0x06000000, 0x06000002 } }, mem.Reader(), nullptr, text, err) &&
                  Has(text, "; sub_000000 -- master") && Has(text, "\nsub_000000:\n"),
              "an unnamed function has one name in the header and the label");
    }

    // The pseudocode file.
    {
        FunctionExportInfo info;
        info.name = "func*/x";
        info.entry = 0x06010478;
        info.cpu = 1;
        info.source = "recorded frame";
        info.frame = 0;
        const std::string marks = "/* WARNING: something */\n";
        const std::string text = FormatDecompiledExport(info, marks + "void f(void)\n{\n}");
        Check(Has(text, "/* func* /x -- slave SH-2, entry 06010478 */\n"), "header is a C comment that cannot close early");
        Check(Has(text, "/* memory: recorded frame */\n"), "frame 0 is not stated");
        Check(Has(text, marks + "void f(void)"), "the warning marks precede the code");
        Check(text.back() == '\n', "ends in a newline");
        Check(CCommentLine("a\nb */ c") == "/* a b * / c */\n", "a comment line holds no line break and cannot close early");
    }

    // File names.
    {
        FunctionExportInfo info;
        info.entry = 0x06010468;
        Check(FunctionExportFileName(info, FunctionExportKind::Listing) == "sub_010468.s", "no name: FunctionNames' sub_ spelling, .s");
        Check(FunctionExportFileName(info, FunctionExportKind::Pseudocode) == "sub_010468.c", "pseudocode is .c");
        info.name = "Player::Update <inline>";
        Check(FunctionExportFileName(info, FunctionExportKind::Listing) == "Player__Update__inline_.s", "unsafe characters become '_'");
        info.name = "sub_010468";
        Check(FunctionExportFileName(info, FunctionExportKind::Pseudocode) == "sub_010468.c", "a plain name is kept");
    }

    if (gFail == 0) std::printf("FunctionExportTests: all passed\n");
    return gFail ? 1 : 0;
}
