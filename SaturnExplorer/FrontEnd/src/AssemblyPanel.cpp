#include "AssemblyPanel.h"

#include "AsmCodeWindow.h"

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
        // The anchor, not the row at the top of the view: Back should return to where the user
        // was looking, wherever they have since scrolled.
        mBack.push_back(mFollowPc ? mLastPc : mWindowAnchor);
        mFwd.clear();
    }
    mFollowPc = false;
    mWindowAnchor = addr & ~1u;
    mWindowValid = true;
    // Put the target on the first visible line. It is what the user asked to see, so it goes
    // at the top -- with everything above it in the region available by scrolling up.
    RequestScroll(mWindowAnchor, 0.0f);
    mFocusRequested = true;  // and bring the panel forward (e.g. a Call Stack "Go to ...")
}

// Read and decode the pages covering rows [rowFirst, rowEnd) that are not already cached. All the
// missing pages go in one batch: a frame needs a handful of small reads, not one per row.
void AssemblyPanel::LoadPages(IMemoryBackend& backend, const AsmSpan& span, uint32_t rowFirst, uint32_t rowEnd)
{
    if (rowEnd <= rowFirst) return;
    std::vector<uint32_t> missing;
    for (uint32_t p = rowFirst / kPageRows; p <= (rowEnd - 1) / kPageRows; ++p)
    {
        const uint32_t base = span.base + p * kPageRows * 2;
        if (!mPages.count(base)) missing.push_back(p);
    }
    if (missing.empty()) return;

    // Each page also asks for the instruction just before it: its first row may sit in the delay
    // slot of the branch above, and decoding it must not depend on where the page happens to
    // start. Unreadable (the page starts at the region's edge) just means no predecessor.
    std::vector<MemoryReadRequest> reqs;
    std::vector<int> prevAt;   // index of each page's predecessor read in reqs, or -1 at the span start
    for (uint32_t p : missing)
    {
        const uint32_t first = p * kPageRows;
        const uint32_t rows = std::min(kPageRows, span.size / 2 - first);
        const uint32_t base = span.base + first * 2;
        reqs.push_back({ base, rows * 2 });
        prevAt.push_back(first ? (int)reqs.size() : -1);
        if (first) reqs.push_back({ base - 2, 2 });
    }
    const auto results = backend.ReadMemoryBatch(reqs);
    size_t next = 0;
    for (size_t i = 0; i < missing.size(); ++i)
    {
        const uint32_t first = missing[i] * kPageRows;
        const uint32_t base = span.base + first * 2;
        const MemoryReadResult& body = results[next];
        const bool havePrev = prevAt[i] >= 0 && results[prevAt[i]].success && results[prevAt[i]].bytes.size() >= 2;
        const MemoryReadResult* prev = havePrev ? &results[prevAt[i]] : nullptr;
        next += prevAt[i] >= 0 ? 2 : 1;
        const uint32_t rows = std::min(kPageRows, span.size / 2 - first);
        const std::vector<Sh2WindowLine> decoded =
            Sh2DecodeWindow(base, body.success ? body.bytes.data() : nullptr,
                            body.success ? body.bytes.size() : 0, (int)rows,
                            prev ? prev->bytes.data() : nullptr);
        Page& page = mPages[base];
        page.lines.reserve(decoded.size());
        for (const Sh2WindowLine& w : decoded)
        {
            Line ln; ln.addr = w.addr; ln.op = w.op; ln.readable = w.readable; ln.ins = w.ins;
            page.lines.push_back(std::move(ln));
        }
    }
}

const AssemblyPanel::Line* AssemblyPanel::LineAt(const AsmSpan& span, uint32_t row) const
{
    const auto it = mPages.find(span.base + (row / kPageRows) * kPageRows * 2);
    return it != mPages.end() && row % kPageRows < it->second.lines.size()
               ? &it->second.lines[row % kPageRows] : nullptr;
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
    // Re-enabling Follow PC forces a re-frame+scroll back to PC (mWindowValid=false), so it
    // snaps back even when PC hasn't moved since you scrolled away.
    if (ImGui::Checkbox("Follow PC", &mFollowPc) && mFollowPc) mWindowValid = false;
    ImGui::SameLine();
    ImGui::Checkbox("Auto Refresh", &mAutoRefresh);
    ImGui::SetItemTooltip("On: re-read the code from the emulator every frame, so the disassembly "
                          "follows self-modifying code and freshly loaded overlays.\n"
                          "Off: keep the bytes already read, so the listing holds still while you "
                          "study it. Scrolling to rows not read yet still reads them.");
    ImGui::SameLine();
    ImGui::BeginDisabled(mBack.empty());
    if (ImGui::ArrowButton("##back", ImGuiDir_Left) && !mBack.empty())
    {
        mFwd.push_back(mWindowAnchor);
        mWindowAnchor = mBack.back(); mBack.pop_back();
        mFollowPc = false;
        RequestScroll(mWindowAnchor, 0.0f);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Back");
    ImGui::SameLine(0.0f, 2.0f);
    ImGui::BeginDisabled(mFwd.empty());
    if (ImGui::ArrowButton("##fwd", ImGuiDir_Right) && !mFwd.empty())
    {
        mBack.push_back(mWindowAnchor);
        mWindowAnchor = mFwd.back(); mFwd.pop_back();
        mFollowPc = false;
        RequestScroll(mWindowAnchor, 0.0f);
    }
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
        // Stepping applies to the CPU that halted, so while this panel shows the other one the step
        // buttons wait: a click would otherwise move a CPU the user is not looking at.
        const bool viewingHalted = mCpu == mBpStopCpu;
        ImGui::BeginDisabled(!viewingHalted);
        if (ImGui::Button("Step Into"))  req.stepInto = true;
        ImGui::SetItemTooltip("Run one SH-2 instruction");
        ImGui::SameLine();
        if (ImGui::Button("Step Over"))  req.stepOver = true;
        ImGui::SetItemTooltip("Over a call, run the subroutine to its return; else one instruction");
        ImGui::SameLine();
        if (ImGui::Button("Step Out"))   req.stepOut = true;
        ImGui::SetItemTooltip("Run to the current frame's return address");
        ImGui::EndDisabled();
        if (!viewingHalted)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(halted on the %s SH-2)", mBpStopCpu ? "slave" : "master");
        }
    }

    // --- Frame the view: follow PC unless browsing ---
    if (mFollowPc)
    {
        // Scroll when the view was re-framed (so a forced recentre at a *steady* PC still moves
        // it -- the "Go to PC" case) and on any PC change. The list is the whole region, so a
        // step or a branch can walk the PC far off screen; without this the view would stop
        // following. A steady PC that neither moved nor was re-framed leaves scrolling to the
        // user. PC a third down, so the next few instructions are visible.
        if (!mWindowValid || pc != mLastPc) RequestScroll(pc, 0.35f);
        mWindowAnchor = pc & ~1u;
        mWindowValid = true;
    }
    else if (!mWindowValid)
    {
        mWindowAnchor = pc & ~1u;
        RequestScroll(mWindowAnchor, 0.35f);
        mWindowValid = true;
    }
    mLastPc = pc;   // track the followed PC (also the Back-history anchor in Navigate)

    // The list spans the anchor's whole region; row r is the instruction at span.base + 2r. Held
    // in locals: a Navigate() raised by a click mid-table re-aims mWindowAnchor at what may be
    // another region, which the NEXT frame lists -- these rows must stay in the one being drawn.
    const AsmSpan span = AsmSpanFor(mWindowAnchor);
    const uint32_t rowCount = span.size / 2;

    // Rows are read a page at a time and cached; with Auto Refresh on the cache is dropped every
    // frame so what is on screen is always freshly read. The source is part of the key as well:
    // the bytes of a replaced emulator, a reloaded state or another scrubbed frame are not the
    // code at the same address now, even with Auto Refresh off.
    const uint64_t sourceId = backend.SourceId();
    if (mAutoRefresh || mPagesSource != sourceId || mPages.size() > 1024) mPages.clear();
    mPagesSource = sourceId;

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

    // Snapshot before any row is submitted: a Navigate() raised by a click inside the loop
    // below bumps mScrollSeq, which is how the stale-request drop after the loop knows not to
    // throw away a request meant for a window that has not been decoded yet.
    const unsigned scrollSeqAtRowStart = mScrollSeq;

    // Only the rows on screen are read and decoded. A pending scroll target is forced into the
    // clipper's range even when it is far off screen: SetScrollHereY below needs the row actually
    // submitted to know where it is.
    ImGuiListClipper clipper;
    clipper.Begin((int)rowCount);
    if (mScrollPending && mScrollAddr >= span.base && mScrollAddr - span.base < span.size)
        clipper.IncludeItemByIndex((int)((mScrollAddr - span.base) / 2));
    while (clipper.Step())
    {
        LoadPages(backend, span, (uint32_t)clipper.DisplayStart, (uint32_t)clipper.DisplayEnd);
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
        {
            const Line* lp = LineAt(span, (uint32_t)row);
            if (!lp) continue;   // unreachable: LoadPages just filled every page in the range
            const Line& ln = *lp;
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
            // Address alone: execution BPs are shared across both SH-2s, so the same dot must
            // show up in both the master and slave views of this address, not just the one
            // whichever view happened to set it from.
            const Breakpoint* bp = bps.ExecutionAt(ln.addr);
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
                bps.ToggleExecution(ln.addr);
            openRowContext();
            // Only a request that was already pending when these rows began. One raised during this
            // loop names an address in the window the NEXT frame will decode, and these rows are the
            // old one -- a forward branch whose target happens to lie inside them would otherwise be
            // matched here and scrolled against the wrong window, consuming the request so the
            // rebuilt window never got framed at all. Untouchable this frame, in other words:
            // neither honoured here nor dropped after the loop.
            if (mScrollPending && mScrollSeq == scrollSeqAtRowStart && ln.addr == mScrollAddr)
            {
                ImGui::SetScrollHereY(mScrollAlign);
                mScrollPending = false;
            }

            // Address / bytes / mnemonic / operands. The address cell is a Selectable so the
            // user can pick instruction(s) to search for: click selects one, shift-click
            // extends a contiguous range.
            ImGui::TableSetColumnIndex(1);
            {
                const bool rowSel = mHasSel && ln.addr >= mSelLoAddr && ln.addr <= mSelHiAddr;
                char al[16]; std::snprintf(al, sizeof(al), "%08X", ln.addr);
                ImGui::PushStyleColor(ImGuiCol_Text, kColAddr);
                if (ImGui::Selectable(al, rowSel, ImGuiSelectableFlags_AllowDoubleClick))
                {
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    { req.viewHex = true; req.hexAddr = ln.addr; }
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
                // Initialized because ResolveMemOperand leaves them untouched when it fails:
                // only the hasMem-gated menu item below reads them, which the compiler can't see.
                uint32_t ea = 0;
                WatchType wt = WatchType::U32;
                const bool hasMem = ln.readable && ResolveMemOperand(ln.ins, mCtxOperand, regs, ea, wt);
                if (ImGui::MenuItem(bp ? "Remove Breakpoint" : "Toggle Breakpoint", nullptr, false, ln.readable))
                    bps.ToggleExecution(ln.addr);
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
                // Aims at the operand's address when it has one, else the instruction's own, so the
                // item is available on every row (but not on an uncertain PC-relative operand).
                uint32_t viewAddr = 0;
                if (DrawViewAddressMenuItem(ln.ins, ln.readable ? mCtxOperand : -1, regs, viewAddr))
                { req.viewHex = true; req.hexAddr = ln.readable ? viewAddr : ln.addr; }

                // Find the selected instruction(s)'s code bytes in the game data directory.
                // If this row is inside the current multi-selection, search the whole range;
                // otherwise just this one instruction.
                // The selection can reach rows that are not on screen, so its bytes are read from the
                // backend, once, as the menu opens. Capped: a shift-click across a megabyte of RAM
                // should not become a megabyte search needle. Falls back to this instruction alone.
                if (ImGui::IsWindowAppearing())
                {
                    constexpr uint32_t kMaxFindBytes = 4096;
                    const bool inSel = mHasSel && ln.addr >= mSelLoAddr && ln.addr <= mSelHiAddr &&
                                       mSelHiAddr - mSelLoAddr < kMaxFindBytes;
                    mCtxFindBytes.clear();
                    mCtxFindStart = ln.addr;
                    if (inSel)
                    {
                        const auto r = backend.ReadMemoryBatch({ { mSelLoAddr, mSelHiAddr - mSelLoAddr + 2 } })[0];
                        if (r.success) { mCtxFindBytes = r.bytes; mCtxFindStart = mSelLoAddr; }
                    }
                    if (mCtxFindBytes.empty())
                    {
                        mCtxFindBytes.push_back((uint8_t)(ln.op >> 8));
                        mCtxFindBytes.push_back((uint8_t)(ln.op & 0xFF));
                    }
                }
                std::vector<uint8_t> selBytes = mCtxFindBytes;
                const uint32_t startAddr = mCtxFindStart;
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
    }
    // A request that was already pending and still is could not be consumed by any row (the
    // clipper always submits its target, so it named an address outside the span): drop it
    // rather than let it fire on some unrelated frame once that address happens to be on screen.
    // A request raised during the loop is left alone for the same reason the consume above skips
    // it -- it belongs to the span the next frame will list.
    if (mScrollPending && mScrollSeq == scrollSeqAtRowStart) mScrollPending = false;

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
