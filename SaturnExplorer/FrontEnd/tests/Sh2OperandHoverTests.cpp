// SH-2 operand hover — headless ImGui hit testing of the Assembly panel's operand cell
// (see ImGuiHarness.h).
//
// The bug these pin: the cell draws one ImGui item per *token*, but the panel asked
// ImGui::IsItemHovered() after the whole cell had been submitted. That only ever answers
// for the last item, so "mov.l @(r0,r4),r1" showed a value preview when the pointer was
// over "r1" and nothing at all over "@(r0,r4)". Every token now reports its hover to
// DrawSh2Operands, which maps it back to the operand it belongs to.
//
// Hover points are found by sweeping the pointer across the cell rather than by measuring
// glyphs: a headless context has no font atlas upload, so nothing may be inferred from
// rendered text. Item rects and hover state are ImGui core state and are reliable.

#include <iostream>
#include <string>
#include <vector>

#include "ImGuiHarness.h"
#include "Sh2Operands.h"

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
}  // namespace

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

namespace
{

// Registers with distinct, recognisable values so a tooltip line can be traced back to the
// operand that produced it.
se_sh2_regs TestRegs()
{
    se_sh2_regs r = {};
    r.r[0] = 0x00000010;
    r.r[1] = 0x11111111;
    r.r[3] = 0x06000000;
    r.r[4] = 0x06004000;
    r.r[5] = 0x55555555;
    r.gbr  = 0x20000000;
    return r;
}

// A reader standing in for the memory backend: every address holds the same long.
const uint32_t kMemValue = 0xDEADBEEFu;
Sh2MemReader Reader()
{
    return [](uint32_t, uint32_t n, uint32_t& out) {
        out = kMemValue >> ((4 - n) * 8);
        return true;
    };
}

// One operand cell on its own, so the sweep has nothing else to land on.
struct OperandRow
{
    DisassembledInstruction ins;

    Sh2OperandsDrawn drawn;        // recorded every frame
    ImVec2 cellMin {}, cellMax {};
    int    menuOperand = -2;       // -2 until a right-click opens the menu

    void Draw()
    {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(600.0f, 200.0f));
        ImGui::Begin("Asm", nullptr, ImGuiWindowFlags_NoSavedSettings);
        const ImVec2 start = ImGui::GetCursorScreenPos();
        drawn = DrawSh2Operands(ins);
        // What the panel does with the result: the pointer has moved off the operand by
        // the time a menu item is picked, so the index is latched when the menu opens.
        if (drawn.rightClicked) menuOperand = drawn.hovered;
        // Tokens are laid out with SameLine(0,0), so the last one's right edge is the
        // cell's right edge.
        cellMin = start;
        cellMax = ImGui::GetItemRectMax();
        ImGui::End();
    }
};

DisassembledInstruction Decode(uint16_t opcode, const char* expectOperands)
{
    const DisassembledInstruction ins = Sh2Decode(0x06000000, opcode);
    // Pin the text the rest of the test reasons about: a disassembler change that altered
    // the operand spelling would otherwise quietly make these tests prove nothing.
    Check(ins.Operands == expectOperands, expectOperands, __LINE__);
    return ins;
}

// Sweep the pointer across the cell, a pixel at a time, and report the operand index each
// position hovers (-1 for none).
std::vector<int> SweepHover(OperandRow& row)
{
    ImGuiHarness harness([&] { row.Draw(); });
    harness.Settle();
    // Settle parks the pointer off the widgets, so nothing may be reported as hovered --
    // otherwise a test asserting "operand 0 is hoverable" would pass on a stuck value.
    Check(row.drawn.hovered == -1, "nothing hovered while the pointer is away", __LINE__);

    const float y = (row.cellMin.y + row.cellMax.y) * 0.5f;
    std::vector<int> hits;
    for (float x = row.cellMin.x + 0.5f; x < row.cellMax.x; x += 1.0f)
    {
        harness.Hover(ImVec2(x, y));
        hits.push_back(row.drawn.hovered);
    }
    return hits;
}

// Right-click the first point that hovers operand 'operand'.
void RightClickOperand(OperandRow& row, int operand)
{
    ImGuiHarness harness([&] { row.Draw(); });
    harness.Settle();
    const float y = (row.cellMin.y + row.cellMax.y) * 0.5f;
    for (float x = row.cellMin.x + 0.5f; x < row.cellMax.x; x += 1.0f)
    {
        harness.Hover(ImVec2(x, y));
        if (row.drawn.hovered != operand) continue;
        harness.RightClick(ImVec2(x, y));
        return;
    }
    Check(false, "no point in the cell hovers the wanted operand", __LINE__);
}

// The operands, in the order the sweep first meets them, with repeats collapsed. A group
// that got split into several hover targets shows up as an extra run.
std::vector<int> HoverRuns(const std::vector<int>& hits)
{
    std::vector<int> runs;
    for (int h : hits)
        if (runs.empty() || runs.back() != h) runs.push_back(h);
    return runs;
}

// What a sweep across a two-operand cell must produce: the first operand, then the comma
// separating them (punctuation, part of neither operand, so no preview), then the second.
const std::vector<int> kTwoOperands = {0, -1, 1};

std::string Join(const std::vector<std::string>& lines)
{
    std::string s;
    for (const std::string& l : lines) { s += l; s += '\n'; }
    return s;
}

// --- The regression itself -------------------------------------------------------------

void TestEitherRegisterOperandIsHoverable()
{
    // mov r4,r1 — two plain register operands. Before the fix only "r1", the last item the
    // cell submitted, produced a preview.
    OperandRow row;
    row.ins = Decode(0x6143, "r4,r1");
    CHECK(HoverRuns(SweepHover(row)) == kTwoOperands);

    // And each one resolves to its own register, not the instruction's whole register set:
    // the two previews must differ.
    const se_sh2_regs r = TestRegs();
    const std::string first  = Join(Sh2OperandHoverLines(row.ins, 0, r, Reader()));
    const std::string second = Join(Sh2OperandHoverLines(row.ins, 1, r, Reader()));
    CHECK(first == "r4  = 06004000\n");
    CHECK(second == "r1  = 11111111\n");
    CHECK(first != second);
}

void TestIndexedOperandIsOneHoverTarget()
{
    // mov.l @(r0,r4),r1 — the user's example. The indexed group is a single operand: its
    // halves resolve to nothing on their own, so the whole "@(r0,r4)" (comma included) is
    // one hover target, and "r1" beside it is another.
    OperandRow row;
    row.ins = Decode(0x014E, "@(r0,r4),r1");
    // Exactly two targets, met left to right and never revisited: were the indexed group
    // split into its parts, the sweep would cross more than two.
    CHECK(HoverRuns(SweepHover(row)) == kTwoOperands);

    const se_sh2_regs r = TestRegs();
    // The group shows both its registers and the address they compute, plus the value
    // there, read at the mnemonic's width (.l -> 4 bytes).
    CHECK(Join(Sh2OperandHoverLines(row.ins, 0, r, Reader())) ==
          "r0  = 00000010\nr4  = 06004000\n[06004010] = DEADBEEF\n");
    CHECK(Join(Sh2OperandHoverLines(row.ins, 1, r, Reader())) == "r1  = 11111111\n");
}

void TestDisplacementOperandIsOneHoverTarget()
{
    // mov.l @(0x10,r3),r2 — the displacement form. Same rule: one group, one target.
    OperandRow row;
    row.ins = Decode(0x5234, "@(0x10,r3),r2");
    CHECK(HoverRuns(SweepHover(row)) == kTwoOperands);

    const se_sh2_regs r = TestRegs();
    CHECK(Join(Sh2OperandHoverLines(row.ins, 0, r, Reader())) ==
          "r3  = 06000000\n[06000010] = DEADBEEF\n");
}

void TestSingleOperandIsHoverableAcrossTheCell()
{
    // shll r5 — one operand, so every point in the cell is on it.
    OperandRow row;
    row.ins = Decode(0x4500, "r5");
    const std::vector<int> hits = SweepHover(row);
    CHECK(!hits.empty());
    for (int h : hits) CHECK(h == 0);
    CHECK(Join(Sh2OperandHoverLines(row.ins, 0, TestRegs(), Reader())) == "r5  = 55555555\n");
}

void TestPostIncrementOperandsResolveSeparately()
{
    // mac.l @r4+,@r5+ — two memory operands. Resolving from the instruction's first '@'
    // (what the panel used to do) gave both sides r4's address; per operand each side now
    // answers for its own register.
    OperandRow row;
    row.ins = Decode(0x054F, "@r4+,@r5+");
    CHECK(HoverRuns(SweepHover(row)) == kTwoOperands);

    const se_sh2_regs r = TestRegs();
    CHECK(Join(Sh2OperandHoverLines(row.ins, 0, r, Reader())) == "r4  = 06004000\n[06004000] = DEADBEEF\n");
    CHECK(Join(Sh2OperandHoverLines(row.ins, 1, r, Reader())) == "r5  = 55555555\n[55555555] = DEADBEEF\n");
}

void TestRightClickAimsTheMenuAtTheOperandUnderThePointer()
{
    // The same wrong-operand defect as the tooltip, in the row context menu: "Add Operand
    // to Watch" and "View Address in Memory" resolved the instruction's *first* memory
    // operand whichever one the user right-clicked. On mac.l @r4+,@r5+ the two halves have
    // different addresses, so the menu must follow the pointer.
    const se_sh2_regs r = TestRegs();
    uint32_t ea = 0, width = 0;

    OperandRow second;
    second.ins = Decode(0x054F, "@r4+,@r5+");
    RightClickOperand(second, 1);
    CHECK(second.menuOperand == 1);
    CHECK(ResolveSh2MemOperand(second.ins, second.menuOperand, r, ea, width) && ea == r.r[5]);

    OperandRow firstOp;
    firstOp.ins = second.ins;
    RightClickOperand(firstOp, 0);
    CHECK(firstOp.menuOperand == 0);
    CHECK(ResolveSh2MemOperand(firstOp.ins, firstOp.menuOperand, r, ea, width) && ea == r.r[4]);

    // The fallback a menu opened elsewhere on the row uses. It answers r4 for both halves,
    // which is exactly why the latched index has to be passed through.
    CHECK(ResolveSh2MemOperand(second.ins, -1, r, ea, width) && ea == r.r[4]);
}

// --- Splitting and resolution, without ImGui --------------------------------------------

std::vector<std::string> Split(const std::string& operands)
{
    std::vector<std::string> out;
    Sh2OperandSpan sp;
    for (int i = 0; Sh2OperandAt(operands, i, sp); ++i)
        out.push_back(operands.substr(sp.begin, sp.end - sp.begin));
    return out;
}

void TestSplitKeepsParenthesisedGroupsWhole()
{
    CHECK(Split("").empty());
    CHECK(Split("r1") == std::vector<std::string>{"r1"});
    CHECK((Split("r4,r1") == std::vector<std::string>{"r4", "r1"}));
    CHECK((Split("@(r0,r4),r1") == std::vector<std::string>{"@(r0,r4)", "r1"}));
    CHECK((Split("@(0x10,r3),r2") == std::vector<std::string>{"@(0x10,r3)", "r2"}));
    CHECK((Split("#0x3,@(r0,gbr)") == std::vector<std::string>{"#0x3", "@(r0,gbr)"}));
}

void TestOperandsWithNothingToResolveShowNoTooltip()
{
    // bra 0x06000104 — a branch target is a link, not a value; an immediate is already on
    // screen. Neither has anything to add, so the panel must not open an empty tooltip.
    const DisassembledInstruction bra = Sh2Decode(0x06000100, 0xA000);
    CHECK(bra.HasBranchTarget);
    CHECK(Sh2OperandHoverLines(bra, 0, TestRegs(), Reader()).empty());

    const DisassembledInstruction movi = Decode(0xE720, "#0x20,r7");
    CHECK(Sh2OperandHoverLines(movi, 0, TestRegs(), Reader()).empty());
    CHECK(Sh2OperandHoverLines(movi, 1, TestRegs(), Reader()).size() == 1);

    // Out-of-range indices are answered, not indexed into.
    CHECK(Sh2OperandHoverLines(movi, -1, TestRegs(), Reader()).empty());
    CHECK(Sh2OperandHoverLines(movi, 2, TestRegs(), Reader()).empty());
}

void TestAccessWidthAndUnreadableMemory()
{
    const se_sh2_regs r = TestRegs();
    // mov.b @r4,r1 reads one byte, and the preview is printed at that width.
    const DisassembledInstruction b = Decode(0x6140, "@r4,r1");
    CHECK(Join(Sh2OperandHoverLines(b, 0, r, Reader())) == "r4  = 06004000\n[06004000] = DE\n");

    // An unreadable address still names the address it worked out.
    const Sh2MemReader dead = [](uint32_t, uint32_t, uint32_t&) { return false; };
    CHECK(Join(Sh2OperandHoverLines(b, 0, r, dead)) == "r4  = 06004000\n[06004000] unavailable\n");
}

void TestMemOperandIndexDecidesLoadVersusStore()
{
    // Which operand holds the '@' is what makes a mov a load or a store. Deciding it from
    // the first comma in the text instead is right only as long as no non-memory operand
    // is ever parenthesised: for "@(r0,r4),r1" that first comma is the group's own.
    CHECK(Sh2MemOperandIndex("@(r0,r4),r1") == 0);      // load
    CHECK(Sh2MemOperandIndex("r1,@(r0,r4)") == 1);      // store
    CHECK(Sh2MemOperandIndex("@(0x10,r3),r2") == 0);
    CHECK(Sh2MemOperandIndex("r0,@(0x12,gbr)") == 1);
    CHECK(Sh2MemOperandIndex("@r4+,@r5+") == 0);
    CHECK(Sh2MemOperandIndex("#0x3,@(r0,gbr)") == 1);
    // No memory operand at all, and nothing to mistake for one.
    CHECK(Sh2MemOperandIndex("r4,r1") == -1);
    CHECK(Sh2MemOperandIndex("") == -1);

    // The case that separates the two rules: a '@' sitting after an *inner* comma but
    // still inside the first operand. Comparing positions ("is the @ left of the first
    // comma?") calls this operand 1; on operand boundaries it is operand 0. No SH-2 form
    // reaches it today — '@' always precedes '(' — which is precisely why the old position
    // test was right by accident. This pins the answer to the boundaries instead.
    CHECK(Sh2MemOperandIndex("(0x10,@r3),r2") == 0);

    // Against the real disassembler, both directions of the same indexed form.
    CHECK(Sh2MemOperandIndex(Decode(0x014E, "@(r0,r4),r1").Operands) == 0);
    CHECK(Sh2MemOperandIndex(Decode(0x0146, "r4,@(r0,r1)").Operands) == 1);
}

void TestAccessWidthFromMnemonic()
{
    CHECK(Sh2AccessWidth("mov.b") == 1);
    CHECK(Sh2AccessWidth("mov.w") == 2);
    CHECK(Sh2AccessWidth("mov.l") == 4);
    CHECK(Sh2AccessWidth("mov") == 4);     // no suffix: the SH-2's natural word
    CHECK(Sh2AccessWidth("") == 4);
}

void TestRegMaskMatchesWholeTokens()
{
    // "r1" must not be found inside "r15", or hovering @(0x4,r15) would claim to show r1.
    CHECK(Sh2OperandRegMask("@(0x4,r15)") == (1u << 15));
    CHECK(Sh2OperandRegMask("@(r0,r4)") == ((1u << 0) | (1u << 4)));
    CHECK(Sh2OperandRegMask("#0x20") == 0);
    CHECK(Sh2OperandRegMask("gbr") == 0);
}

}  // namespace

int main()
{
    TestEitherRegisterOperandIsHoverable();
    TestIndexedOperandIsOneHoverTarget();
    TestDisplacementOperandIsOneHoverTarget();
    TestSingleOperandIsHoverableAcrossTheCell();
    TestPostIncrementOperandsResolveSeparately();
    TestRightClickAimsTheMenuAtTheOperandUnderThePointer();
    TestSplitKeepsParenthesisedGroupsWhole();
    TestOperandsWithNothingToResolveShowNoTooltip();
    TestAccessWidthAndUnreadableMemory();
    TestMemOperandIndexDecidesLoadVersusStore();
    TestAccessWidthFromMnemonic();
    TestRegMaskMatchesWholeTokens();
    if (gFailures != 0)
    {
        std::cerr << gFailures << " SH-2 operand hover check(s) failed\n";
        return 1;
    }
    std::cout << "SH-2 operand hover tests passed\n";
    return 0;
}
