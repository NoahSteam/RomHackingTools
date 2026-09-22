#include "Sh2Operands.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#include "imgui.h"

#include "Debug/Sh2RegInfo.h"   // Sh2RegIndexFromName: the one name -> register mapping

namespace sfe
{

namespace
{
// Syntax colours for the operand cell. The rest of the Assembly panel's palette (address,
// bytes, mnemonic, comment) stays in AssemblyPanel.cpp, which is where those are drawn.
const ImU32 kColReg    = IM_COL32(220, 200, 130, 255);   // amber
const ImU32 kColImm    = IM_COL32(180, 205, 150, 255);   // green
const ImU32 kColTarget = IM_COL32(130, 175, 255, 255);   // link blue
const ImU32 kColPunct  = IM_COL32(140, 140, 150, 255);

bool IsTokenChar(char c) { return std::isalnum((unsigned char)c) != 0 || c == '.'; }

// r0..r15 -> 0..15; the special registers and anything else -> -1.
int RegIndex(const std::string& t)
{
    const int i = Sh2RegIndexFromName(t);
    return i < 16 ? i : -1;
}

std::string OperandText(const std::string& operands, const Sh2OperandSpan& sp)
{
    return operands.substr(sp.begin, sp.end - sp.begin);
}
}  // namespace

uint32_t Sh2AccessWidth(const std::string& mnemonic)
{
    if (mnemonic.size() < 2 || mnemonic[mnemonic.size() - 2] != '.') return 4;
    const char w = mnemonic.back();
    return (w == 'b') ? 1u : (w == 'w') ? 2u : 4u;
}

bool Sh2OperandAt(const std::string& operands, int index, Sh2OperandSpan& out)
{
    if (index < 0 || operands.empty()) return false;
    size_t begin = 0;
    int depth = 0, n = 0;
    for (size_t i = 0; i < operands.size(); ++i)
    {
        const char c = operands[i];
        if (c == '(') ++depth;
        else if (c == ')') { if (depth > 0) --depth; }
        else if (c == ',' && depth == 0)
        {
            if (n == index) { out.begin = begin; out.end = i; return true; }
            ++n;
            begin = i + 1;
        }
    }
    if (n != index) return false;
    out.begin = begin;
    out.end = operands.size();
    return true;
}

uint16_t Sh2OperandRegMask(const std::string& operand)
{
    uint16_t mask = 0;
    for (size_t i = 0; i < operand.size();)
    {
        if (!IsTokenChar(operand[i])) { ++i; continue; }
        size_t j = i;
        while (j < operand.size() && IsTokenChar(operand[j])) ++j;
        const int rn = RegIndex(operand.substr(i, j - i));
        if (rn >= 0) mask |= (uint16_t)(1u << rn);
        i = j;
    }
    return mask;
}

int Sh2MemOperandIndex(const std::string& operands)
{
    if (operands.find('@') == std::string::npos) return -1;
    Sh2OperandSpan sp;
    for (int i = 0; Sh2OperandAt(operands, i, sp); ++i)
        if (operands.find('@', sp.begin) < sp.end) return i;
    return -1;
}

bool ResolveSh2MemOperand(const std::string& operand, const std::string& mnemonic,
                          const se_sh2_regs& r, uint32_t& outAddr, uint32_t& outWidth)
{
    const size_t at = operand.find('@');
    if (at == std::string::npos) return false;
    outWidth = Sh2AccessWidth(mnemonic);

    const std::string s = operand.substr(at + 1);
    unsigned reg = 0, reg2 = 0, disp = 0;
    // @(0x........)  — absolute (PC-relative already resolved by the disassembler).
    if (std::sscanf(s.c_str(), "(0x%x)", &disp) == 1 && s.find(',') == std::string::npos)
    { outAddr = disp; return true; }
    // @(0xX,rN) / @(0xX,gbr)
    if (std::sscanf(s.c_str(), "(0x%x,r%u)", &disp, &reg) == 2 && reg < 16)
    { outAddr = r.r[reg] + disp; return true; }
    if (std::strncmp(s.c_str(), "(0x", 3) == 0 && s.find(",gbr)") != std::string::npos &&
        std::sscanf(s.c_str(), "(0x%x", &disp) == 1)
    { outAddr = r.gbr + disp; return true; }
    // @(r0,rN)
    if (std::sscanf(s.c_str(), "(r0,r%u)", &reg2) == 1 && reg2 < 16)
    { outAddr = r.r[0] + r.r[reg2]; return true; }
    // @rN, @rN+, @-rN
    if (std::sscanf(s.c_str(), "r%u", &reg) == 1 && reg < 16)
    { outAddr = r.r[reg]; return true; }
    if (std::sscanf(s.c_str(), "-r%u", &reg) == 1 && reg < 16)
    { outAddr = r.r[reg] - outWidth; return true; }
    return false;
}

bool ResolveSh2MemOperand(const DisassembledInstruction& ins, int index, const se_sh2_regs& r,
                          uint32_t& outAddr, uint32_t& outWidth)
{
    if (index < 0) index = Sh2MemOperandIndex(ins.Operands);
    Sh2OperandSpan sp;
    // Most instructions touch no memory at all, and this runs for every visible row every
    // frame: those leave with no operand walked and no text copied.
    return Sh2OperandAt(ins.Operands, index, sp) &&
           ResolveSh2MemOperand(OperandText(ins.Operands, sp), ins.Mnemonic, r, outAddr, outWidth);
}

std::vector<std::string> Sh2OperandHoverLines(const DisassembledInstruction& ins, int index,
                                              const se_sh2_regs& r, const Sh2MemReader& readMem)
{
    std::vector<std::string> lines;
    Sh2OperandSpan sp;
    if (!Sh2OperandAt(ins.Operands, index, sp)) return lines;
    const std::string text = OperandText(ins.Operands, sp);

    const uint16_t regs = Sh2OperandRegMask(text);
    char b[64];
    for (int rn = 0; rn < 16; ++rn)
        if (regs & (1u << rn))
        {
            std::snprintf(b, sizeof(b), "r%-2d = %08X", rn, r.r[rn]);
            lines.push_back(b);
        }

    uint32_t ea = 0, n = 0;
    if (ResolveSh2MemOperand(text, ins.Mnemonic, r, ea, n))
    {
        // Read exactly the access width the mnemonic implies (.b/.w/.l -> 1/2/4) so a
        // mov.l shows a long and a mov.b a byte, not a fixed-size dump.
        uint32_t val = 0;
        if (readMem && readMem(ea, n, val)) std::snprintf(b, sizeof(b), "[%08X] = %0*X", ea, (int)(n * 2), val);
        else                                std::snprintf(b, sizeof(b), "[%08X] unavailable", ea);
        lines.push_back(b);
    }
    return lines;
}

Sh2OperandsDrawn DrawSh2Operands(const DisassembledInstruction& ins)
{
    Sh2OperandsDrawn out;
    const std::string& s = ins.Operands;
    char targetStr[16] = {};
    if (ins.HasBranchTarget) std::snprintf(targetStr, sizeof(targetStr), "0x%08X", ins.BranchTarget);

    bool first = true;
    // Hover and right-click are read off each token as it is submitted. Reading them after
    // the cell answers only for the last item ImGui saw.
    auto seg = [&](const std::string& tok, int operand, ImU32 col, bool link)
    {
        if (!first) ImGui::SameLine(0.0f, 0.0f);
        first = false;
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::TextUnformatted(tok.c_str());
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered() && operand >= 0) out.hovered = operand;
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) out.rightClicked = true;
        if (link)
        {
            if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsItemClicked()) out.clicked = true;
        }
    };

    size_t i = 0;
    int operand = 0;   // the operand the token being drawn belongs to
    int depth = 0;     // parenthesis nesting: a comma inside a group does not separate
    while (i < s.size())
    {
        const char c = s[i];
        if (IsTokenChar(c))
        {
            size_t j = i;
            while (j < s.size() && IsTokenChar(s[j])) ++j;
            const std::string tok = s.substr(i, j - i);
            i = j;
            if (ins.HasBranchTarget && tok == targetStr)      seg(tok, operand, kColTarget, true);
            else if (tok.rfind("0x", 0) == 0)                 seg(tok, operand, kColImm, false);
            else if (Sh2RegIndexFromName(tok) >= 0)           seg(tok, operand, kColReg, false);
            else                                              seg(tok, operand, kColPunct, false);
        }
        else if (c == '#')
        {
            // immediate: '#', then the following number token
            size_t j = i + 1;
            while (j < s.size() && IsTokenChar(s[j])) ++j;
            seg(s.substr(i, j - i), operand, kColImm, false);
            i = j;
        }
        else
        {
            if (c == '(') ++depth;
            else if (c == ')') { if (depth > 0) --depth; }
            const bool separator = c == ',' && depth == 0;
            seg(std::string(1, c), separator ? -1 : operand, kColPunct, false);
            if (separator) ++operand;
            ++i;
        }
    }
    if (first) ImGui::TextUnformatted(" ");   // empty operands: keep the row height
    return out;
}

}  // namespace sfe
