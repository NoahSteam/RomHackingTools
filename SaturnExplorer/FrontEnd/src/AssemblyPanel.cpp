#include "AssemblyPanel.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include "imgui.h"

#include "Sh2Operands.h"
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
const ImU32 kColCmt   = IM_COL32(110, 130, 110, 255);   // the operand palette: Sh2Operands.cpp
const ImU32 kColPcRow = IM_COL32(60, 90, 60, 110);      // current-PC row tint
const ImU32 kColBpHitRow = IM_COL32(150, 45, 45, 110);  // faint red: breakpoint-hit row

// Resolve a memory operand's effective address + a natural watch type, from the
// disassembled instruction text + current registers. Returns false when the
// access isn't statically resolvable (e.g. depends on an unmodelled value).
// 'operand' picks the operand the pointer was over (-1: the first one that resolves).
bool ResolveMemOperand(const DisassembledInstruction& ins, int operand, const se_sh2_regs& r,
                       uint32_t& outAddr, WatchType& outType)
{
    uint32_t width = 0;
    if (!ResolveSh2MemOperand(ins, operand, r, outAddr, width)) return false;
    outType = (width == 1) ? WatchType::U8 : (width == 2) ? WatchType::U16 : WatchType::U32;
    return true;
}

// Read 'n' big-endian bytes for the operand hover preview. SH-2 is big-endian, so
// assemble the bytes MSB-first.
Sh2MemReader MemReaderFor(IMemoryBackend& backend)
{
    return [&backend](uint32_t addr, uint32_t n, uint32_t& outValue) {
        auto mr = backend.ReadMemoryBatch({ { addr, n } })[0];
        if (!mr.success) return false;
        outValue = 0;
        for (uint32_t i = 0; i < n; ++i) outValue = (outValue << 8) | mr.bytes[i];
        return true;
    };
}

// Printable-ASCII annotation for a value, e.g. 0x66 -> " ('f')".
std::string AsciiTag(uint32_t v)
{
    if (v >= 0x20 && v <= 0x7E)
    { char b[8]; std::snprintf(b, sizeof(b), " ('%c')", (char)v); return b; }
    return "";
}

// A heuristic, human-readable comment for one instruction. Structural only —
// branch intent, immediates, compares, loads/stores, and PC-relative literal-pool
// resolution — not dataflow. Returns "" when nothing useful can be said.
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

    // --- Immediate to register: mov/add/cmp/eq/and/or/xor/tst #imm,rN ---
    unsigned imm = 0, rn = 0, rm = 0;
    if (std::sscanf(o.c_str(), "#0x%x,r%u", &imm, &rn) == 2 && rn < 16)
    {
        char b[80];
        if (m == "mov")         std::snprintf(b, sizeof(b), "r%u = 0x%X%s", rn, imm, AsciiTag(imm).c_str());
        else if (m == "add")    std::snprintf(b, sizeof(b), "r%u += %d", rn, (int)(int8_t)(unsigned char)imm);
        else if (m == "cmp/eq") std::snprintf(b, sizeof(b), "compare r%u with 0x%X%s", rn, imm, AsciiTag(imm).c_str());
        else                    std::snprintf(b, sizeof(b), "r%u = r%u %s 0x%X", rn, rn, m.c_str(), imm);
        return b;
    }

    // --- Register compare / move ---
    if (m.rfind("cmp/", 0) == 0 && std::sscanf(o.c_str(), "r%u,r%u", &rm, &rn) == 2)
    { char b[48]; std::snprintf(b, sizeof(b), "compare r%u, r%u", rm, rn); return b; }
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
            uint32_t ea; WatchType wt; uint32_t val = 0;
            if (isLoad && o.rfind("@(0x", 0) == 0 && o.find(",r") != std::string::npos &&
                ResolveMemOperand(ins, memOp, regs, ea, wt) &&
                readMem(ea, WatchTypeSize(wt), val))
            {
                char b[64]; std::snprintf(b, sizeof(b), "= [%08X] = 0x%X%s", ea, val,
                                          wt == WatchType::U8 ? AsciiTag(val).c_str() : "");
                return b;
            }
            return std::string(isLoad ? "load " : "store ") + unit;
        }
    }
    return "";
}
}  // namespace

void AssemblyPanel::GoTo(int cpu, uint32_t addr)
{
    mCpu = cpu & 1;
    Navigate(addr, true);
}

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
    mScrollToPc = true;      // reuse the scroll flag to bring the target into view
    mFocusRequested = true;  // and bring the panel forward (e.g. a Call Stack "Go to ...")
}

void AssemblyPanel::Draw(se_context* ctx, IMemoryBackend& backend, BreakpointManager& bps,
                         ExecutionActions& actions, WatchPanel& watch, bool live, Request& req)
{
    if (mFocusRequested) { ImGui::SetNextWindowFocus(); mFocusRequested = false; }
    if (!ImGui::Begin("SH-2 Assembly"))
    {
        ImGui::End();
        return;
    }

    se_sh2_regs regs = {};
    const bool haveRegs = ctx && se_get_sh2_regs(ctx, mCpu, &regs) == SE_OK;
    // While halted (or holding across a step's resume->re-halt) on this CPU, follow the stable
    // halt PC rather than the live register PC: the App keeps re-snapshotting during the brief
    // in-flight window, so regs.pc momentarily reflects the running CPU and makes the view
    // jitter/jump before settling. mBpStopPc is rock-steady and advances one clean step at a time.
    const bool halted = mBpStopActive && mCpu == mBpStopCpu;
    const uint32_t pc = halted ? mBpStopPc : regs.pc;

    // --- Header: CPU selector, PC, Follow PC, nav, goto ---
    ImGui::SetNextItemWidth(120.0f);
    const char* kCpus[] = { "Master SH-2", "Slave SH-2" };
    if (ImGui::Combo("##cpu", &mCpu, kCpus, 2)) { mWindowValid = false; mBack.clear(); mFwd.clear(); }
    ImGui::SameLine();
    if (haveRegs) ImGui::Text("PC %08X", pc); else ImGui::TextDisabled("PC --------");
    ImGui::SameLine();
    // Re-enabling Follow PC forces a recenter+scroll back to PC (mWindowValid=false), so it
    // snaps back even when PC hasn't moved since you scrolled away.
    if (ImGui::Checkbox("Follow PC", &mFollowPc) && mFollowPc) mWindowValid = false;
    ImGui::SameLine();
    ImGui::Checkbox("Auto Refresh", &mAutoRefresh);
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

    // Run control mirrored from the Call Stack strip, so stepping is reachable right in the
    // disassembly. Shown only while halted at a breakpoint/step (mBpStopActive) — a bare
    // frame-pause can't single-step. The App resolves the request against this panel's CPU.
    if (live && mBpStopActive)
    {
        if (ImGui::Button("Continue"))  req.continueRun = true;
        ImGui::SameLine();
        if (ImGui::Button("Step Into"))  req.stepInto = true;
        ImGui::SetItemTooltip("Run one SH-2 instruction");
        ImGui::SameLine();
        if (ImGui::Button("Step Over"))  req.stepOver = true;
        ImGui::SetItemTooltip("Over a call, run the subroutine to its return; else one instruction");
        ImGui::SameLine();
        if (ImGui::Button("Step Out"))   req.stepOut = true;
        ImGui::SetItemTooltip("Run to the current frame's return address");
    }

    // --- Window base: follow PC unless browsing ---
    if (mFollowPc)
    {
        // Rebuild the decode buffer when PC leaves it (a far jump/call, or walking off the end),
        // or when a recenter was forced (Go to PC / re-enabling Follow PC set mWindowValid=false).
        const bool rebuilt = !mWindowValid || pc < mWindowBase ||
                             pc >= mWindowBase + (uint32_t)kWinInstr * 2;
        if (rebuilt) mWindowBase = (pc >= 48) ? (pc - 48) & ~1u : 0;   // PC ~1/5 down
        // Scroll on a rebuild (so a forced recenter at a *steady* PC still moves the view — the
        // "Go to PC" case) and on any PC change: the decode buffer is 128 instructions tall, far
        // taller than the viewport, so a step or near branch can walk PC past the visible rows
        // while still inside the buffer and the view would otherwise stop following. A steady PC
        // that neither moved nor was recentered leaves scrolling to the user.
        if (rebuilt || pc != mLastPc) mScrollToPc = true;
        mWindowValid = true;
    }
    else if (!mWindowValid)
    {
        mWindowBase = pc & ~1u; mWindowValid = true;
    }
    mLastPc = pc;   // track the followed PC (also the Back-history anchor in Navigate)

    // Read the code window. Cache-gated: with Auto Refresh off and the base
    // unchanged, reuse the last bytes so the disassembly holds still (also spares
    // the re-read on a paused/savestate source, where the code can't change).
    const uint32_t winLen = (uint32_t)kWinInstr * 2;
    if (mAutoRefresh || !mHaveWindowBytes || mWindowBytesBase != mWindowBase)
    {
        auto results = backend.ReadMemoryBatch({ { mWindowBase, winLen } });
        mWindowBytes = results[0].success ? results[0].bytes : std::vector<uint8_t>();
        mWindowBytesBase = mWindowBase;
        mHaveWindowBytes = true;
    }
    const std::vector<uint8_t>& code = mWindowBytes;
    mLines.clear();
    for (int k = 0; k < kWinInstr; ++k)
    {
        Line ln; ln.addr = mWindowBase + (uint32_t)k * 2;
        if ((size_t)(k * 2 + 1) < code.size())
        {
            ln.op = (uint16_t)((code[k*2] << 8) | code[k*2+1]);
            ln.ins = Sh2Decode(ln.addr, ln.op);
            ln.readable = true;
        }
        mLines.push_back(std::move(ln));
    }

    // Branch targets that land inside the window get a "loc_" label row above them.
    std::unordered_set<uint32_t> labels;
    for (const Line& ln : mLines)
        if (ln.readable && ln.ins.HasBranchTarget &&
            ln.ins.BranchTarget >= mWindowBase && ln.ins.BranchTarget < mWindowBase + winLen)
            labels.insert(ln.ins.BranchTarget & ~1u);

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

    // One reader for the whole table: the rows' comments and the hover preview both read
    // memory through it, and building it here rather than per row keeps the lifetime of
    // the backend reference it captures plainly bounded by this call.
    const Sh2MemReader readMem = MemReaderFor(backend);

    for (const Line& ln : mLines)
    {
        // Location label row for an in-window branch target.
        if (labels.count(ln.addr))
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(3);
            ImGui::PushStyleColor(ImGuiCol_Text, kColCmt);
            ImGui::Text(";-- loc_%08X:", ln.addr);
            ImGui::PopStyleColor();
        }

        const bool isPc = ln.addr == pc;
        const bool isBpHit = mBpStopActive && mCpu == mBpStopCpu && ln.addr == mBpStopPc;
        ImGui::TableNextRow();
        // A breakpoint halt tints the hit row faint red; otherwise the live PC row is green.
        if (isBpHit)   ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, kColBpHitRow);
        else if (isPc) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, kColPcRow);
        ImGui::PushID((int)ln.addr);

        // A context menu belongs to the instruction row, not just its final Comment
        // widget. Open the same popup from every visible cell so right-clicking the
        // gutter, address, opcode, mnemonic, operands, or comment behaves uniformly.
        // Opened from anywhere but the operands, the menu has no operand to aim at, so its
        // memory actions fall back to the instruction's first one (mCtxOperand = -1).
        const auto openRowContext = [this]()
        {
            if (!ImGui::IsItemClicked(ImGuiMouseButton_Right)) return;
            mCtxOperand = -1;
            ImGui::OpenPopup("ctx");
        };

        // Gutter: breakpoint dot (click toggles) + PC arrow.
        ImGui::TableSetColumnIndex(0);
        const Breakpoint* bp = bps.ExecutionAt(mCpu, ln.addr);
        const ExecutionAction* tp = actions.LogAt(mCpu, ln.addr);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ImGui::GetTextLineHeight();
        const float cy = p.y + h * 0.5f;
        if (bp)   // breakpoint = red circle
        {
            const ImU32 c = bp->enabled ? IM_COL32(220, 60, 60, 255) : IM_COL32(120, 90, 90, 255);
            dl->AddCircleFilled(ImVec2(p.x + 6, cy), 5.0f, c);
        }
        if (tp)   // tracepoint = blue diamond (a distinct SHAPE, not just colour)
        {
            const ImU32 c = tp->enabled ? IM_COL32(90, 160, 240, 255) : IM_COL32(95, 115, 140, 255);
            const float x = p.x + 17, r = 5.0f;
            const ImVec2 pts[4] = {{x, cy - r}, {x + r, cy}, {x, cy + r}, {x - r, cy}};
            dl->AddConvexPolyFilled(pts, 4, c);
        }
        if (isPc)
            dl->AddText(ImVec2(p.x + 26, p.y), IM_COL32(230, 230, 90, 255), ">");
        // Left-click toggles a breakpoint (the common action); tracepoints toggle from
        // the right-click menu so the two don't fight over the same click.
        if (ImGui::InvisibleButton("g", ImVec2(34, h)) && ln.readable)
            bps.ToggleExecution(mCpu, ln.addr);
        openRowContext();
        if (isPc && mScrollToPc) { ImGui::SetScrollHereY(0.35f); mScrollToPc = false; }

        // Address / bytes / mnemonic / operands. The address cell is a Selectable so the
        // user can pick instruction(s) to search for: click selects one, shift-click
        // extends a contiguous range.
        ImGui::TableSetColumnIndex(1);
        {
            const bool rowSel = mHasSel && ln.addr >= mSelLoAddr && ln.addr <= mSelHiAddr;
            char al[16]; std::snprintf(al, sizeof(al), "%08X", ln.addr);
            ImGui::PushStyleColor(ImGuiCol_Text, kColAddr);
            if (ImGui::Selectable(al, rowSel, ImGuiSelectableFlags_None))
            {
                if (ImGui::GetIO().KeyShift && mHasSel)
                {
                    mSelLoAddr = std::min(mSelAnchorAddr, ln.addr);
                    mSelHiAddr = std::max(mSelAnchorAddr, ln.addr);
                }
                else { mSelAnchorAddr = mSelLoAddr = mSelHiAddr = ln.addr; mHasSel = true; }
            }
            ImGui::PopStyleColor();
        }
        openRowContext();
        ImGui::TableSetColumnIndex(2);
        if (ln.readable) { ImGui::PushStyleColor(ImGuiCol_Text, kColBytes); ImGui::Text("%04X", ln.op); ImGui::PopStyleColor(); }
        else ImGui::TextDisabled("----");
        openRowContext();
        ImGui::TableSetColumnIndex(3);
        if (ln.readable) { ImGui::PushStyleColor(ImGuiCol_Text, kColMnem); ImGui::TextUnformatted(ln.ins.Mnemonic.c_str()); ImGui::PopStyleColor(); }
        else ImGui::TextDisabled("????");
        openRowContext();
        ImGui::TableSetColumnIndex(4);
        Sh2OperandsDrawn ops;
        if (ln.readable) ops = DrawSh2Operands(ln.ins);
        // Right-clicking an operand aims the menu's memory actions at that operand, so
        // "Add Operand to Watch" on the @r5+ half of "mac.l @r4+,@r5+" watches r5's
        // address and not r4's.
        if (ops.rightClicked) { mCtxOperand = ops.hovered; ImGui::OpenPopup("ctx"); }
        if (ops.clicked) Navigate(ln.ins.BranchTarget, true);

        // Register / memory hover preview for the operand under the pointer. The index
        // comes from DrawSh2Operands because only it sees the individual token items.
        if (ops.hovered >= 0)
        {
            const std::vector<std::string> lines =
                Sh2OperandHoverLines(ln.ins, ops.hovered, regs, readMem);
            if (!lines.empty())
            {
                ImGui::BeginTooltip();
                for (const std::string& l : lines) ImGui::TextUnformatted(l.c_str());
                ImGui::EndTooltip();
            }
        }

        // Comment: user note (bright) overlaid on the auto-generated comment (dim).
        // Double-click to edit the user note; empty note clears it.
        ImGui::TableSetColumnIndex(5);
        {
            auto it = mComments.find(ln.addr);
            const bool hasUser = it != mComments.end() && !it->second.empty();
            if (mEditingComment && mEditCommentAddr == ln.addr)
            {
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (mCommentFocus) { ImGui::SetKeyboardFocusHere(); mCommentFocus = false; }
                const bool enter = ImGui::InputText("##cmt", mCommentBuf, sizeof(mCommentBuf),
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
                if (enter || ImGui::IsItemDeactivated())
                {
                    if (mCommentBuf[0]) mComments[ln.addr] = mCommentBuf;
                    else                mComments.erase(ln.addr);
                    mEditingComment = false;
                    SaveComments();
                }
            }
            else
            {
                const std::string autoCmt = ln.readable ? Sh2Comment(ln.ins, regs, readMem) : std::string();
                const char* txt = hasUser ? it->second.c_str() : autoCmt.c_str();
                ImGui::PushStyleColor(ImGuiCol_Text, hasUser ? IM_COL32(190, 185, 140, 255) : kColCmt);
                ImGui::TextUnformatted(txt[0] ? txt : " ");
                ImGui::PopStyleColor();
                openRowContext();
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                {
                    mEditingComment = true; mEditCommentAddr = ln.addr; mCommentFocus = true;
                    std::snprintf(mCommentBuf, sizeof(mCommentBuf), "%s", hasUser ? it->second.c_str() : "");
                }
            }
        }

        // Row context menu.
        if (ImGui::BeginPopup("ctx"))
        {
            uint32_t ea; WatchType wt;
            const bool hasMem = ln.readable && ResolveMemOperand(ln.ins, mCtxOperand, regs, ea, wt);
            if (ImGui::MenuItem(bp ? "Remove Breakpoint" : "Toggle Breakpoint", nullptr, false, ln.readable))
                bps.ToggleExecution(mCpu, ln.addr);
            if (ImGui::MenuItem(tp ? "Remove Tracepoint" : "Toggle Tracepoint", nullptr, false, ln.readable))
                actions.ToggleLog(mCpu, ln.addr);
            if (ImGui::MenuItem("Create Tracepoint...", nullptr, false, ln.readable))
            { req.editTracepoint = true; req.tpAddr = ln.addr; req.tpCpu = mCpu; }
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
            ImGui::Separator();
            if (ImGui::MenuItem("Edit Comment"))
            {
                mEditingComment = true; mEditCommentAddr = ln.addr; mCommentFocus = true;
                auto it = mComments.find(ln.addr);
                std::snprintf(mCommentBuf, sizeof(mCommentBuf), "%s",
                              it != mComments.end() ? it->second.c_str() : "");
            }
            if (ImGui::MenuItem("Clear Comment", nullptr, false, mComments.count(ln.addr) != 0))
            { mComments.erase(ln.addr); SaveComments(); }
            ImGui::Separator();
            uint32_t hexEa; WatchType hexWt;
            if (ImGui::MenuItem("View Address in Memory", nullptr, false,
                                ln.readable && ResolveMemOperand(ln.ins, mCtxOperand, regs, hexEa, hexWt)))
            { req.viewHex = true; req.hexAddr = hexEa; }

            // Find the selected instruction(s)'s code bytes in the game data directory.
            // If this row is inside the current multi-selection, search the whole range;
            // otherwise just this one instruction.
            // Gather the code bytes once (big-endian opcode order) and derive the count
            // + labels from the vector — one walk, one pack site.
            const bool inSel = mHasSel && ln.addr >= mSelLoAddr && ln.addr <= mSelHiAddr;
            std::vector<uint8_t> selBytes;
            uint32_t startAddr = ln.addr;
            if (inSel)
            {
                startAddr = mSelLoAddr;
                for (const Line& l : mLines)
                    if (l.readable && l.addr >= mSelLoAddr && l.addr <= mSelHiAddr)
                    { selBytes.push_back((uint8_t)(l.op >> 8)); selBytes.push_back((uint8_t)(l.op & 0xFF)); }
            }
            if (selBytes.empty())   // no selection (or empty range): just this instruction
            {
                selBytes.push_back((uint8_t)(ln.op >> 8));
                selBytes.push_back((uint8_t)(ln.op & 0xFF));
                startAddr = ln.addr;
            }
            char findItem[80];
            std::snprintf(findItem, sizeof(findItem), "Find %s in data directory (%zu bytes)",
                          selBytes.size() > 2 ? "selected instructions" : "this instruction",
                          selBytes.size());
            if (ImGui::MenuItem(findItem, nullptr, false, ln.readable))
            {
                char lbl[80];
                std::snprintf(lbl, sizeof(lbl), "SH-2 code @0x%08X (%zu bytes)",
                              startAddr, selBytes.size());
                req.findInData = true;
                req.findBytes = std::move(selBytes);
                req.findLabel = lbl;
            }
            ImGui::BeginDisabled();
            ImGui::MenuItem("Set PC Here");
            ImGui::EndDisabled();
            ImGui::EndPopup();
        }

        ImGui::PopID();
    }
    ImGui::EndTable();

    ImGui::End();
}

// Best-effort comment store: one fixed file in the working directory, each line
// "ADDRHEX <note>". Mirrors WatchPanel's session persistence (a native-desktop
// stopgap that no-ops on the web build's ephemeral MEMFS).
namespace { const char* kCommentFile = "assembly_comments.txt"; }

void AssemblyPanel::LoadComments()
{
    std::ifstream f(kCommentFile);
    if (!f) return;
    mComments.clear();
    std::string line;
    while (std::getline(f, line))
    {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        unsigned addr = 0;
        const size_t sp = line.find(' ');
        if (sp == std::string::npos || std::sscanf(line.c_str(), "%x", &addr) != 1) continue;
        std::string note = line.substr(sp + 1);
        if (!note.empty()) mComments[addr] = note;
    }
}

void AssemblyPanel::SaveComments() const
{
    std::ofstream f(kCommentFile);
    if (!f) return;
    for (const auto& kv : mComments)
        if (!kv.second.empty()) f << std::hex << kv.first << " " << kv.second << "\n";
}

}  // namespace sfe
