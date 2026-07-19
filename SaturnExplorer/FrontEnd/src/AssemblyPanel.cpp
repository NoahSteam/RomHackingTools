#include "AssemblyPanel.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#include "imgui.h"

#include "WatchPanel.h"

namespace sfe
{

namespace
{
constexpr int kWinInstr = 128;   // instructions per disassembled window

// Subtle syntax colours for the dark theme.
const ImU32 kColAddr  = IM_COL32(150, 150, 160, 255);
const ImU32 kColBytes = IM_COL32(120, 120, 130, 255);
const ImU32 kColMnem  = IM_COL32(120, 200, 235, 255);   // cyan-ish
const ImU32 kColReg   = IM_COL32(220, 200, 130, 255);   // amber
const ImU32 kColImm   = IM_COL32(180, 205, 150, 255);   // green
const ImU32 kColTarget= IM_COL32(130, 175, 255, 255);   // link blue
const ImU32 kColPunct = IM_COL32(140, 140, 150, 255);
const ImU32 kColCmt   = IM_COL32(110, 130, 110, 255);
const ImU32 kColPcRow = IM_COL32(60, 90, 60, 110);      // current-PC row tint

bool IsRegToken(const std::string& t)
{
    if (t.size() >= 2 && t[0] == 'r' && std::isdigit((unsigned char)t[1])) return true;
    static const char* kSpecial[] = { "pc", "pr", "sr", "gbr", "vbr", "mach", "macl" };
    for (const char* s : kSpecial) if (t == s) return true;
    return false;
}
int RegIndex(const std::string& t)   // r0..r15 -> 0..15, else -1
{
    if (t.size() < 2 || t[0] != 'r') return -1;
    int n = 0; for (size_t i = 1; i < t.size(); ++i) { if (!std::isdigit((unsigned char)t[i])) return -1; n = n*10 + (t[i]-'0'); }
    return (n >= 0 && n < 16) ? n : -1;
}

// Resolve a memory operand's effective address + a natural watch type, from the
// disassembled instruction text + current registers. Returns false when the
// access isn't statically resolvable (e.g. depends on an unmodelled value).
bool ResolveMemOperand(const DisassembledInstruction& ins, const se_sh2_regs& r,
                       uint32_t& outAddr, WatchType& outType)
{
    const std::string& op = ins.Operands;
    const size_t at = op.find('@');
    if (at == std::string::npos) return false;

    // Access width from the mnemonic suffix.
    outType = WatchType::U32;
    if (ins.Mnemonic.size() >= 2 && ins.Mnemonic[ins.Mnemonic.size()-2] == '.')
    {
        const char w = ins.Mnemonic.back();
        outType = (w == 'b') ? WatchType::U8 : (w == 'w') ? WatchType::U16 : WatchType::U32;
    }

    const std::string s = op.substr(at + 1);
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
    { outAddr = r.r[reg] - WatchTypeSize(outType); return true; }
    return false;
}

// Render 'operands' with subtle per-token colouring. If a token equals the
// instruction's branch target it becomes a clickable link; returns the clicked
// target (or 0 / clicked=false).
void DrawOperands(const DisassembledInstruction& ins, bool& clicked, uint32_t& clickTarget)
{
    clicked = false;
    const std::string& s = ins.Operands;
    char targetStr[16] = {};
    if (ins.HasBranchTarget) std::snprintf(targetStr, sizeof(targetStr), "0x%08X", ins.BranchTarget);

    size_t i = 0;
    bool first = true;
    auto seg = [&](const std::string& tok, ImU32 col, bool link)
    {
        if (!first) ImGui::SameLine(0.0f, 0.0f);
        first = false;
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::TextUnformatted(tok.c_str());
        ImGui::PopStyleColor();
        if (link)
        {
            if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsItemClicked()) { clicked = true; clickTarget = ins.BranchTarget; }
        }
    };
    while (i < s.size())
    {
        const char c = s[i];
        if (std::isalnum((unsigned char)c) || c == '.' || c == 'x')
        {
            size_t j = i;
            while (j < s.size() && (std::isalnum((unsigned char)s[j]) || s[j] == '.')) ++j;
            std::string tok = s.substr(i, j - i);
            i = j;
            if (ins.HasBranchTarget && tok == targetStr)      seg(tok, kColTarget, true);
            else if (tok.rfind("0x", 0) == 0)                 seg(tok, kColImm, false);
            else if (IsRegToken(tok))                         seg(tok, kColReg, false);
            else                                              seg(tok, kColPunct, false);
        }
        else if (c == '#')
        {
            // immediate: '#', then the following number token
            size_t j = i + 1;
            while (j < s.size() && (std::isalnum((unsigned char)s[j]) || s[j] == '.')) ++j;
            seg(s.substr(i, j - i), kColImm, false);
            i = j;
        }
        else
        {
            seg(std::string(1, c), kColPunct, false);
            ++i;
        }
    }
    if (first) ImGui::TextUnformatted(" ");   // empty operands: keep the row height
}
}  // namespace

void AssemblyPanel::Navigate(uint32_t addr, bool pushHistory)
{
    if (pushHistory && mWindowValid)
    {
        mBack.push_back(mFollowPc ? mLastPc : mWindowBase);
        mFwd.clear();
    }
    mFollowPc = false;
    mWindowBase = addr & ~1u;
    mWindowValid = true;
    mScrollToPc = true;   // reuse the scroll flag to bring the target into view
}

void AssemblyPanel::Draw(se_context* ctx, IMemoryBackend& backend, BreakpointManager& bps,
                         WatchPanel& watch, bool live, Request& req)
{
    if (!ImGui::Begin("SH-2 Assembly"))
    {
        ImGui::End();
        return;
    }

    se_sh2_regs regs = {};
    const bool haveRegs = ctx && se_get_sh2_regs(ctx, mCpu, &regs) == SE_OK;
    const uint32_t pc = regs.pc;

    // --- Header: CPU selector, PC, Follow PC, nav, goto ---
    ImGui::SetNextItemWidth(120.0f);
    const char* kCpus[] = { "Master SH-2", "Slave SH-2" };
    if (ImGui::Combo("##cpu", &mCpu, kCpus, 2)) { mWindowValid = false; mBack.clear(); mFwd.clear(); }
    ImGui::SameLine();
    if (haveRegs) ImGui::Text("PC %08X", pc); else ImGui::TextDisabled("PC --------");
    ImGui::SameLine();
    ImGui::Checkbox("Follow PC", &mFollowPc);
    ImGui::SameLine();
    ImGui::BeginDisabled(mBack.empty());
    if (ImGui::ArrowButton("##back", ImGuiDir_Left) && !mBack.empty())
    { mFwd.push_back(mWindowBase); mWindowBase = mBack.back(); mBack.pop_back(); mFollowPc = false; mScrollToPc = true; }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Back");
    ImGui::SameLine(0.0f, 2.0f);
    ImGui::BeginDisabled(mFwd.empty());
    if (ImGui::ArrowButton("##fwd", ImGuiDir_Right) && !mFwd.empty())
    { mBack.push_back(mWindowBase); mWindowBase = mFwd.back(); mFwd.pop_back(); mFollowPc = false; mScrollToPc = true; }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Forward");
    ImGui::SameLine();
    if (haveRegs && ImGui::Button("Go to PC")) { mFollowPc = true; mWindowValid = false; }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::InputTextWithHint("##goto", "goto 0x..", mGotoBuf, sizeof(mGotoBuf),
                                 ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsHexadecimal))
    {
        unsigned a = 0;
        if (std::sscanf(mGotoBuf, "%x", &a) == 1) Navigate(a, true);
        mGotoBuf[0] = '\0';
    }

    if (!haveRegs)
    {
        ImGui::Separator();
        ImGui::TextWrapped("No SH-2 state in this source. Load a savestate (or connect a "
                           "live Yabause) that provides CPU registers.");
        ImGui::End();
        return;
    }

    // --- Window base: follow PC unless browsing; recenter only when PC leaves view ---
    if (mFollowPc)
    {
        if (!mWindowValid || pc < mWindowBase || pc >= mWindowBase + (uint32_t)kWinInstr * 2)
        {
            mWindowBase = (pc >= 48) ? (pc - 48) & ~1u : 0;   // PC ~1/5 down
            mScrollToPc = true;
        }
        mWindowValid = true;
    }
    else if (!mWindowValid)
    {
        mWindowBase = pc & ~1u; mWindowValid = true;
    }

    // Read the code window (some lines may be unreadable at region edges).
    auto results = backend.ReadMemoryBatch({ { mWindowBase, (uint32_t)kWinInstr * 2 } });
    const MemoryReadResult& code = results[0];
    mLines.clear();
    for (int k = 0; k < kWinInstr; ++k)
    {
        Line ln; ln.addr = mWindowBase + (uint32_t)k * 2;
        if (code.success && (size_t)(k * 2 + 1) < code.bytes.size())
        {
            ln.op = (uint16_t)((code.bytes[k*2] << 8) | code.bytes[k*2+1]);
            ln.ins = Sh2Decode(ln.addr, ln.op);
            ln.readable = true;
        }
        mLines.push_back(std::move(ln));
    }

    // --- Table ---
    const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;
    if (!ImGui::BeginTable("asm", 6, flags))
    {
        ImGui::End();
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("##bp", ImGuiTableColumnFlags_WidthFixed, 26.0f);
    ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 74.0f);
    ImGui::TableSetupColumn("Bytes", ImGuiTableColumnFlags_WidthFixed, 40.0f);
    ImGui::TableSetupColumn("Instr", ImGuiTableColumnFlags_WidthFixed, 62.0f);
    ImGui::TableSetupColumn("Operands", ImGuiTableColumnFlags_WidthStretch, 0.6f);
    ImGui::TableSetupColumn("Comment", ImGuiTableColumnFlags_WidthStretch, 0.4f);
    ImGui::TableHeadersRow();

    for (const Line& ln : mLines)
    {
        const bool isPc = ln.addr == pc;
        ImGui::TableNextRow();
        if (isPc) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, kColPcRow);
        ImGui::PushID((int)ln.addr);

        // Gutter: breakpoint dot (click toggles) + PC arrow.
        ImGui::TableSetColumnIndex(0);
        const Breakpoint* bp = bps.ExecutionAt(mCpu, ln.addr);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ImGui::GetTextLineHeight();
        if (bp)
        {
            const ImU32 c = bp->enabled ? IM_COL32(220, 60, 60, 255) : IM_COL32(120, 90, 90, 255);
            dl->AddCircleFilled(ImVec2(p.x + 6, p.y + h*0.5f), 5.0f, c);
        }
        if (isPc)
            dl->AddText(ImVec2(p.x + 12, p.y), IM_COL32(230, 230, 90, 255), ">");
        if (ImGui::InvisibleButton("g", ImVec2(24, h)) && ln.readable)
            bps.ToggleExecution(mCpu, ln.addr);
        if (isPc && mScrollToPc) { ImGui::SetScrollHereY(0.35f); mScrollToPc = false; }

        // Address / bytes / mnemonic / operands.
        ImGui::TableSetColumnIndex(1);
        ImGui::PushStyleColor(ImGuiCol_Text, kColAddr); ImGui::Text("%08X", ln.addr); ImGui::PopStyleColor();
        ImGui::TableSetColumnIndex(2);
        if (ln.readable) { ImGui::PushStyleColor(ImGuiCol_Text, kColBytes); ImGui::Text("%04X", ln.op); ImGui::PopStyleColor(); }
        else ImGui::TextDisabled("----");
        ImGui::TableSetColumnIndex(3);
        if (ln.readable) { ImGui::PushStyleColor(ImGuiCol_Text, kColMnem); ImGui::TextUnformatted(ln.ins.Mnemonic.c_str()); ImGui::PopStyleColor(); }
        else ImGui::TextDisabled("????");
        ImGui::TableSetColumnIndex(4);
        bool tClicked = false; uint32_t tTarget = 0;
        if (ln.readable) DrawOperands(ln.ins, tClicked, tTarget);
        if (tClicked) Navigate(tTarget, true);

        // Register / memory hover preview on the operands cell.
        if (ln.readable && ImGui::IsItemHovered() && !ln.ins.Operands.empty())
        {
            ImGui::BeginTooltip();
            for (int rn = 0; rn < 16; ++rn)
            {
                char t[4]; std::snprintf(t, sizeof(t), "r%d", rn);
                if (ln.ins.Operands.find(t) != std::string::npos)
                    ImGui::Text("r%-2d = %08X", rn, regs.r[rn]);
            }
            uint32_t ea; WatchType wt;
            if (ResolveMemOperand(ln.ins, regs, ea, wt))
            {
                auto mr = backend.ReadMemoryBatch({ { ea, 2 } })[0];
                if (mr.success) ImGui::Text("[%08X] = %02X %02X", ea, mr.bytes[0], mr.bytes[1]);
                else ImGui::Text("[%08X] unavailable", ea);
            }
            ImGui::EndTooltip();
        }

        // Comment: auto branch label.
        ImGui::TableSetColumnIndex(5);
        if (ln.readable && ln.ins.HasBranchTarget)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, kColCmt);
            ImGui::Text("loc_%08X", ln.ins.BranchTarget);
            ImGui::PopStyleColor();
        }

        // Row context menu.
        if (ImGui::BeginPopupContextItem("ctx"))
        {
            uint32_t ea; WatchType wt;
            const bool hasMem = ln.readable && ResolveMemOperand(ln.ins, regs, ea, wt);
            if (ImGui::MenuItem(bp ? "Remove Breakpoint" : "Toggle Breakpoint", nullptr, false, ln.readable))
                bps.ToggleExecution(mCpu, ln.addr);
            if (ImGui::MenuItem("Run to Here", nullptr, false, live && ln.readable))
            { req.runTo = true; req.runToAddr = ln.addr; }
            if (ImGui::MenuItem("Follow Branch", nullptr, false, ln.readable && ln.ins.HasBranchTarget))
                Navigate(ln.ins.BranchTarget, true);
            ImGui::Separator();
            if (ImGui::MenuItem("Add Operand to Watch", nullptr, false, hasMem))
            {
                char nm[32], ex[16];
                std::snprintf(nm, sizeof(nm), "op_%08X", ea);
                std::snprintf(ex, sizeof(ex), "0x%08X", ea);
                watch.AddWatch(nm, ex, wt);
            }
            if (ImGui::MenuItem("Copy Address"))
            { char b[16]; std::snprintf(b, sizeof(b), "0x%08X", ln.addr); ImGui::SetClipboardText(b); }
            if (ImGui::MenuItem("Copy Instruction", nullptr, false, ln.readable))
            {
                char b[64]; std::snprintf(b, sizeof(b), "%08X  %-8s %s", ln.addr,
                                          ln.ins.Mnemonic.c_str(), ln.ins.Operands.c_str());
                ImGui::SetClipboardText(b);
            }
            ImGui::BeginDisabled();
            ImGui::MenuItem("Set PC Here");
            ImGui::MenuItem("View Address in Hex Editor");
            ImGui::EndDisabled();
            ImGui::EndPopup();
        }

        ImGui::PopID();
    }
    ImGui::EndTable();

    // Step control lives in the toolbar/transport; expose a step request here too.
    (void)req;
    ImGui::End();
}

}  // namespace sfe
