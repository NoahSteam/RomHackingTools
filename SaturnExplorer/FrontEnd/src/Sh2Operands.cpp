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

bool Sh2OperandIsAddressOnly(const std::string& mnemonic)
{
    return mnemonic == "jmp" || mnemonic == "jsr" || mnemonic == "mova";
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
    // @(r0,gbr): the byte read-modify-write forms (tst.b/and.b/xor.b/or.b)
    if (s.compare(0, 8, "(r0,gbr)") == 0)
    { outAddr = r.r[0] + r.gbr; return true; }
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

bool ResolveSh2OperandAddress(const DisassembledInstruction& ins, int index, const se_sh2_regs& r,
                              uint32_t& outAddr, uint32_t& outWidth)
{
    if (index < 0) index = Sh2MemOperandIndex(ins.Operands);
    Sh2OperandSpan sp;
    // Most instructions touch no memory at all, and this runs for every visible row every
    // frame: those leave with no operand walked and no text copied.
    return Sh2OperandAt(ins.Operands, index, sp) &&
           ResolveSh2MemOperand(OperandText(ins.Operands, sp), ins.Mnemonic, r, outAddr, outWidth);
}

bool ResolveSh2MemOperand(const DisassembledInstruction& ins, int index, const se_sh2_regs& r,
                          uint32_t& outAddr, uint32_t& outWidth)
{
    return !Sh2OperandIsAddressOnly(ins.Mnemonic) &&
           ResolveSh2OperandAddress(ins, index, r, outAddr, outWidth);
}

bool Sh2MayAccessRange(const DisassembledInstruction& ins, const se_sh2_regs& r, uint32_t base,
                       uint32_t size)
{
    if (ins.Mnemonic == "rte" || ins.Mnemonic == "trapa") return true;
    if (Sh2OperandIsAddressOnly(ins.Mnemonic)) return false;
    Sh2OperandSpan sp;
    for (int i = 0; Sh2OperandAt(ins.Operands, i, sp); ++i)
    {
        if (ins.Operands.find('@', sp.begin) >= sp.end) continue;
        const std::string text = OperandText(ins.Operands, sp);
        if (text.find("@-") != std::string::npos || text.back() == '+') return true;
        uint32_t addr = 0, width = 0;
        if (!ResolveSh2MemOperand(text, ins.Mnemonic, r, addr, width)) return true;
        // Unsigned distance, so a range at the top of the address space does not wrap.
        if (addr < base + size && base < addr + width) return true;
    }
    return false;
}

bool Sh2OperandIsUncertainPcRel(const DisassembledInstruction& ins, int index)
{
    Sh2OperandSpan sp;
    return ins.PcRelAmbiguous && Sh2OperandAt(ins.Operands, index, sp) &&
           ins.Operands.find("pc", sp.begin) < sp.end;
}

bool DrawViewAddressMenuItem(const DisassembledInstruction& ins, int operand, const se_sh2_regs& r,
                             uint32_t& outAddr)
{
    if (!ImGui::MenuItem("View Address in Memory", nullptr, false,
                         !Sh2OperandIsUncertainPcRel(ins, operand)))
        return false;
    uint32_t width = 0;
    if (!ResolveSh2OperandAddress(ins, operand, r, outAddr, width)) outAddr = ins.Address;
    return true;
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
    if (Sh2OperandIsUncertainPcRel(ins, index))
    {
        // Memory cannot say whether this runs in the branch's delay slot, so give each candidate.
        const bool mova = ins.PcRel == Sh2PcRel::Mova;
        const uint32_t width = mova ? 0 : (ins.PcRel == Sh2PcRel::Word ? 2u : 4u);
        auto candidate = [&](const char* how, uint32_t addr)
        {
            if (mova) std::snprintf(b, sizeof(b), "%s: address = %08X", how, addr);
            else
            {
                uint32_t val = 0;
                if (readMem && readMem(addr, width, val))
                    std::snprintf(b, sizeof(b), "%s: [%08X] = %0*X", how, addr, (int)(width * 2), val);
                else
                    std::snprintf(b, sizeof(b), "%s: [%08X] unavailable", how, addr);
            }
            lines.push_back(b);
        };
        candidate("reached directly", ins.PcRelDirectAddress);
        if (ins.PcRelHasSlotAddress) candidate("in the slot of the taken branch", ins.PcRelSlotAddress);
        else lines.push_back("in the slot of the taken branch: depends on its destination");
        return lines;
    }
    if (ResolveSh2MemOperand(text, ins.Mnemonic, r, ea, n))
    {
        if (Sh2OperandIsAddressOnly(ins.Mnemonic))
        {
            // Not a memory access: the operand is the address itself.
            std::snprintf(b, sizeof(b), "%s = %08X", ins.Mnemonic == "mova" ? "address" : "target", ea);
            lines.push_back(b);
            return lines;
        }
        // Read exactly the access width the mnemonic implies (.b/.w/.l -> 1/2/4) so a
        // mov.l shows a long and a mov.b a byte, not a fixed-size dump.
        uint32_t val = 0;
        if (readMem && readMem(ea, n, val)) std::snprintf(b, sizeof(b), "[%08X] = %0*X", ea, (int)(n * 2), val);
        else                                std::snprintf(b, sizeof(b), "[%08X] unavailable", ea);
        lines.push_back(b);
    }
    return lines;
}

namespace
{
// Printable-ASCII annotation for a value, e.g. 0x66 -> " ('f')".
std::string AsciiTag(uint32_t v)
{
    if (v >= 0x20 && v <= 0x7E)
    { char b[8]; std::snprintf(b, sizeof(b), " ('%c')", (char)v); return b; }
    return "";
}
}  // namespace

std::string Sh2Comment(const DisassembledInstruction& ins, const se_sh2_regs& regs,
                       const Sh2MemReader& readMem)
{
    if (!ins.IsValid) return "";
    const std::string& m = ins.Mnemonic;
    const std::string& o = ins.Operands;

    // --- Control flow ---
    if (ins.IsReturn) return "return";
    if (ins.HasBranchTarget)
    {
        char loc[24]; std::snprintf(loc, sizeof(loc), "loc_%08X", ins.BranchTarget);
        if (ins.IsCall) return std::string("call ") + loc;
        if (ins.IsConditional)
            return std::string((m == "bt" || m == "bt.s") ? "if T set -> " : "if T clear -> ") + loc;
        return std::string("-> ") + loc;
    }
    if (m == "jmp" || m == "braf")  return std::string("jump ") + o;
    if (m == "jsr" || m == "bsrf")  return std::string("call ") + o;

    // --- PC-relative operand in a delay slot whose branch may not be taken ---
    if (ins.PcRelAmbiguous) return "PC-relative: address depends on whether this is the branch's delay slot";
    if (m == "mova")
    {
        uint32_t ea = 0, w = 0;
        if (ResolveSh2OperandAddress(ins, 0, regs, ea, w)) { char b[40]; std::snprintf(b, sizeof(b), "r0 = address 0x%08X", ea); return b; }
        return "";
    }

    // --- Immediate to register: mov/add/cmp/eq/and/or/xor/tst #imm,rN ---
    unsigned imm = 0, rn = 0, rm = 0;
    if (std::sscanf(o.c_str(), "#0x%x,r%u", &imm, &rn) == 2 && rn < 16)
    {
        // mov, add and cmp/eq sign-extend the 8-bit immediate to 32 bits (E0FF loads 0xFFFFFFFF); and,
        // or, xor and tst zero-extend it. The operand text keeps the encoded byte.
        const uint32_t sext = (uint32_t)(int32_t)(int8_t)(unsigned char)imm;
        char b[80];
        if (m == "mov")         std::snprintf(b, sizeof(b), "r%u = 0x%X%s", rn, sext, AsciiTag(sext).c_str());
        else if (m == "add")    std::snprintf(b, sizeof(b), "r%u += %d", rn, (int)(int8_t)(unsigned char)imm);
        else if (m == "cmp/eq") std::snprintf(b, sizeof(b), "compare r%u with 0x%X%s", rn, sext, AsciiTag(sext).c_str());
        else if (m == "tst")    std::snprintf(b, sizeof(b), "T = ((r%u & 0x%X) == 0)", rn, imm);
        else                    std::snprintf(b, sizeof(b), "r%u = r%u %s 0x%X", rn, rn, m.c_str(), imm);
        return b;
    }

    // --- Register compare / move ---
    if (m.rfind("cmp/", 0) == 0 && std::sscanf(o.c_str(), "r%u,r%u", &rm, &rn) == 2)
    { char b[48]; std::snprintf(b, sizeof(b), "compare r%u, r%u", rm, rn); return b; }
    if (m == "tst" && std::sscanf(o.c_str(), "r%u,r%u", &rm, &rn) == 2)
    { char b[48]; std::snprintf(b, sizeof(b), "T = ((r%u & r%u) == 0)", rn, rm); return b; }
    if (m == "mov" && std::sscanf(o.c_str(), "r%u,r%u", &rm, &rn) == 2)
    { char b[32]; std::snprintf(b, sizeof(b), "r%u = r%u", rn, rm); return b; }

    // --- Memory move: a load when the memory operand is the source, else a store ---
    if (m.rfind("mov.", 0) == 0)
    {
        const uint32_t width = Sh2AccessWidth(m);
        const char* unit = (width == 1) ? "byte" : (width == 2) ? "word" : "long";
        // Which *operand* the '@' falls in, not which side of the first comma it is on:
        // that comma can be the group's own, as in "@(r0,r4),r1".
        const int memOp = Sh2MemOperandIndex(o);
        Sh2OperandSpan second;
        if (memOp >= 0 && Sh2OperandAt(o, 1, second))
        {
            const bool isLoad = memOp == 0;   // "@src,rN" vs "rN,@dst"
            // PC-relative literal pool: the disassembler resolves it to @(0xABS),rN.
            uint32_t ea = 0, w = 0, val = 0;
            if (isLoad && o.rfind("@(0x", 0) == 0 && o.find(",r") != std::string::npos &&
                ResolveSh2MemOperand(ins, memOp, regs, ea, w) && readMem && readMem(ea, w, val))
            {
                char b[64]; std::snprintf(b, sizeof(b), "= [%08X] = 0x%X%s", ea, val,
                                          w == 1 ? AsciiTag(val).c_str() : "");
                return b;
            }
            return std::string(isLoad ? "load " : "store ") + unit;
        }
    }
    return "";
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
        // One hover query per token: the assembly view submits ~6-8 of these per row across
        // ~128 rows every frame, and a link token would otherwise ask twice.
        const bool hovered = ImGui::IsItemHovered();
        if (hovered && operand >= 0) out.hovered = operand;
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) out.rightClicked = true;
        if (link)
        {
            if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
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
