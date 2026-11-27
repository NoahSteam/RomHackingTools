#include "Theme.h"
#include "AssemblyPanel.h"

#include "AsmCodeWindow.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include "imgui.h"

#include "AddressNameLine.h"
#include "Debug/WriteDestText.h"
#include "PanelWidgets.h"
#include "FileRead.h"
#include "FileWrite.h"
#include "StoreMerge.h"
#include "WorkFiles.h"
#include "Sh2Operands.h"
#include "WatchPanel.h"

namespace sfe
{

namespace
{
// Rows decoded per cached page (pages are aligned to the span's base).
constexpr uint32_t kPageRows = 64;

// Instructions in the page starting at row 'first': a full page, or what is left of the span.
// By value on purpose: std::min would bind a class-static kPageRows by reference, an ODR-use that
// needs an out-of-line definition under C++14 and fails to link in an unoptimized build.
uint32_t PageRows(const AsmSpan& span, uint32_t first)
{
    const uint32_t left = span.size / 2 - first;
    return left < kPageRows ? left : kPageRows;
}

// Resolve syntax colors per draw so existing pages follow live theme switches.
ImU32 kColAddr() { return ui::Color(IM_COL32(150, 150, 160, 255), IM_COL32(89,101,121,255)); }
ImU32 kColBytes() { return ui::Color(IM_COL32(120, 120, 130, 255), IM_COL32(89,101,121,255)); }
ImU32 kColMnem() { return ui::Color(IM_COL32(120, 200, 235, 255), IM_COL32(8,100,130,255)); }   // cyan-ish
ImU32 kColCmt() { return ui::Color(IM_COL32(110, 130, 110, 255), IM_COL32(74,100,74,255)); }   // the operand palette: Sh2Operands.cpp
ImU32 kColPcRow() { return ui::Color(IM_COL32(60, 90, 60, 110), IM_COL32(150,210,165,110)); }      // current-PC row tint
ImU32 kColBpHitRow() { return ui::Color(IM_COL32(150, 45, 45, 110), IM_COL32(235,150,150,110)); }  // faint red: breakpoint-hit row

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

std::string Hex8(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof b, "%08X", v);
    return b;
}

uint16_t BE16(const std::vector<uint8_t>& b) { return (uint16_t)((b[0] << 8) | b[1]); }

// The literal a PC-relative load reads: its address and width. False for everything else (mova names
// an address, it does not read one) and for a row reached after a delayed branch, whose PC -- and so
// whose literal -- depends on whether it runs in the slot (the same ambiguity the row's operand shows
// as "@(disp,pc)"). Computed from the decoded displacement, the way the hardware does.
bool LiteralOf(const DisassembledInstruction& ins, uint32_t& ea, uint32_t& size)
{
    if (ins.PcRelAmbiguous || (ins.PcRel != Sh2PcRel::Word && ins.PcRel != Sh2PcRel::Long)) return false;
    const uint32_t pc = ins.Address + 4;
    ea = (ins.PcRel == Sh2PcRel::Word ? pc : (pc & ~3u)) + ins.PcRelDisp;
    size = ins.PcRel == Sh2PcRel::Word ? 2 : 4;
    return true;
}

// A number the way the assembler spells them (0x1F, H'1F, $1F, decimal, optional sign) that fits
// 'size' bytes, signed or unsigned. A literal pool word is just bits, so both readings are allowed.
bool ParseLiteral(const std::string& text, uint32_t size, uint32_t& out, std::string& err, size_t& col)
{
    size_t i = 0;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
    size_t end = text.size();
    while (end > i && (text[end - 1] == ' ' || text[end - 1] == '\t')) --end;
    col = i;
    bool neg = false;
    if (i < end && (text[i] == '-' || text[i] == '+')) { neg = text[i] == '-'; ++i; }
    int base = 10;
    if (i + 1 < end && text[i] == '0' && (text[i + 1] == 'x' || text[i + 1] == 'X')) { base = 16; i += 2; }
    else if (i + 1 < end && (text[i] == 'H' || text[i] == 'h') && text[i + 1] == '\'') { base = 16; i += 2; }
    else if (i < end && text[i] == '$') { base = 16; ++i; }
    if (i >= end) { err = "expected a number"; col = i < text.size() ? i : text.size(); return false; }
    uint64_t v = 0;
    for (; i < end; ++i)
    {
        const char c = text[i];
        int d = c >= '0' && c <= '9' ? c - '0' : (c | 0x20) >= 'a' && (c | 0x20) <= 'f' ? (c | 0x20) - 'a' + 10 : 99;
        if (d >= base) { err = std::string("'") + c + "' is not a digit here"; col = i; return false; }
        v = v * (uint64_t)base + (uint64_t)d;
        if (v > 0xFFFFFFFFull + 1) { err = "number is too large"; col = i; return false; }
    }
    const uint64_t maxU = size == 2 ? 0xFFFFu : 0xFFFFFFFFu;
    if (neg ? v > (maxU >> 1) + 1 : v > maxU)
    {
        char b[96];
        std::snprintf(b, sizeof b, "does not fit %u bytes (%lld..%llu)", size,
                      -(long long)((maxU >> 1) + 1), (unsigned long long)maxU);
        err = b;
        col = 0;
        return false;
    }
    out = (uint32_t)(neg ? (0 - v) : v) & (uint32_t)maxU;
    return true;
}

// Instructions below 'ea' that load from the same literal (or overlap it). A pool word is shared, so
// patching it changes every one of them. Only the bytes just below the literal can hold a load that
// reaches it: a word load reaches 510 bytes forward, a long load 1020, so one read covers all of them.
// Decoded as if reached directly (what the panel's rows show), so data that looks like a load counts.
void CountSharers(IMemoryBackend& backend, uint32_t rowAddr, uint32_t ea, uint32_t size,
                  size_t& count, uint32_t& first)
{
    count = 0;
    first = 0;
    const uint32_t span = 1032;
    // Not before the start of the literal's region: a read across a region edge fails as a whole.
    const uint32_t regionBase = AsmSpanFor(ea).base;
    const uint32_t lo = ((ea - regionBase >= span) ? ea - span : regionBase) & ~1u;
    if (ea <= lo) return;
    const MemoryReadResult r = backend.ReadMemoryBatch({ { lo, ea - lo } })[0];
    if (!r.success || r.bytes.size() < ea - lo) return;
    for (uint32_t off = 0; off + 1 < ea - lo; off += 2)
    {
        const uint32_t a = lo + off;
        if (a == rowAddr) continue;
        uint32_t ea2 = 0, sz2 = 0;
        if (!LiteralOf(Sh2Decode(a, (uint16_t)((r.bytes[off] << 8) | r.bytes[off + 1])), ea2, sz2)) continue;
        if (ea2 < ea + size && ea < ea2 + sz2)
        {
            if (!count) first = a;
            ++count;
        }
    }
}

std::string SharersText(size_t n, uint32_t first)
{
    return "also loaded by " + std::to_string(n) + " other instruction" + (n == 1 ? "" : "s") +
           " (first at " + Hex8(first) + ")";
}

std::string JoinWarnings(const std::vector<std::string>& w)
{
    std::string s;
    for (const std::string& x : w) s += (s.empty() ? "" : "; ") + x;
    return s;
}

}  // namespace

// The assembler's context for the instruction at 'addr', from memory as it is NOW: the previous word
// says whether this row can sit in a delay slot (and where that branch goes), the next word lets the
// assembler warn about an illegal slot instruction. Re-run at commit, because with Auto Refresh on the
// neighbours can change under an open editor.
bool AssemblyPanel::BuildAsmContext(IMemoryBackend& backend, uint32_t addr, Sh2AsmContext& ctx, uint16_t& curOp)
{
    const auto r = backend.ReadMemoryBatch({ { addr, 2 }, { addr - 2, 2 }, { addr + 2, 2 } });
    if (r.size() < 3 || !r[0].success || r[0].bytes.size() < 2) return false;
    curOp = BE16(r[0].bytes);
    ctx = Sh2AsmContext();
    ctx.address = addr;
    ctx.resolveSymbol = mResolveSymbol;
    if (addr >= 2 && r[1].success && r[1].bytes.size() >= 2)
    {
        const DisassembledInstruction prev = Sh2Decode(addr - 2, BE16(r[1].bytes));
        if (prev.HasDelaySlot)
        {
            ctx.afterDelayedBranch = true;
            ctx.slotTargetKnown = prev.HasBranchTarget;
            ctx.slotBranchTarget = prev.BranchTarget;
        }
    }
    if (r[2].success && r[2].bytes.size() >= 2)
    {
        ctx.haveNext = true;
        ctx.nextOpcode = BE16(r[2].bytes);
    }
    return true;
}

void AssemblyPanel::BeginInstructionEdit(IMemoryBackend& backend, const Line& ln)
{
    if (!ln.readable) return;
    if (!backend.CanWrite(ln.addr))
    {
        const std::string why = backend.WriteRefusal(ln.addr);
        SetNotice(why.empty() ? "Writing is not available at " + Hex8(ln.addr) + "." : why, 2);
        return;
    }
    AsmEdit e;
    uint16_t cur = 0;
    if (!BuildAsmContext(backend, ln.addr, e.ctx, cur))
    {
        SetNotice("Cannot read memory at " + Hex8(ln.addr) + ".", 2);
        return;
    }
    e.kind = EditKind::Instruction;
    e.rowAddr = e.target = ln.addr;
    e.size = 2;
    e.cpu = mCpu;
    e.sourceId = backend.SourceId();
    e.focusPending = true;
    e.beganFrame = (unsigned)ImGui::GetFrameCount();
    e.oldValue = ln.op;
    // Exactly what the Instr and Operands cells show, so the text pastes back unchanged. A slot row's
    // operand is already the "@(disp,pc)" form, which the assembler takes.
    const std::string seed = ln.ins.Operands.empty() ? ln.ins.Mnemonic : ln.ins.Mnemonic + " " + ln.ins.Operands;
    std::snprintf(e.buf, sizeof e.buf, "%s", seed.c_str());
    mEdit = e;
}

void AssemblyPanel::BeginLiteralEdit(IMemoryBackend& backend, const Line& ln)
{
    uint32_t ea = 0, size = 0;
    if (!ln.readable || !LiteralOf(ln.ins, ea, size)) return;
    if (!backend.CanWrite(ea))
    {
        const std::string why = backend.WriteRefusal(ea);
        SetNotice(why.empty() ? "Writing is not available at " + Hex8(ea) + "." : why, 2);
        return;
    }
    const MemoryReadResult r = backend.ReadMemoryBatch({ { ea, size } })[0];
    if (!r.success || r.bytes.size() < size)
    {
        SetNotice("Cannot read the literal at " + Hex8(ea) + ".", 2);
        return;
    }
    AsmEdit e;
    e.kind = EditKind::Literal;
    e.rowAddr = ln.addr;
    e.target = ea;
    e.size = size;
    e.cpu = mCpu;
    e.sourceId = backend.SourceId();
    e.focusPending = true;
    e.beganFrame = (unsigned)ImGui::GetFrameCount();
    for (uint32_t i = 0; i < size; ++i) e.oldValue = (e.oldValue << 8) | r.bytes[i];
    std::snprintf(e.buf, sizeof e.buf, size == 2 ? "0x%04X" : "0x%08X", e.oldValue);
    CountSharers(backend, ln.addr, ea, size, e.sharers, e.firstSharer);
    mEdit = e;
}

// Re-assemble only when the text changed: Sh2Assemble is cheap, but a decode-back per frame is waste.
void AssemblyPanel::UpdatePreview()
{
    AsmEdit& e = mEdit;
    if (e.previewReady && e.previewFor == e.buf) return;
    e.previewReady = true;
    e.previewFor = e.buf;
    e.previewOk = false;
    e.previewWarn = false;
    e.previewMsg.clear();
    if (e.kind == EditKind::Instruction)
    {
        const Sh2AsmResult r = Sh2Assemble(e.buf, e.ctx);
        if (!r.ok)
            e.previewMsg = r.error + " (column " + std::to_string(r.errorColumn + 1) + ")";
        else
        {
            e.previewOk = true;
            e.previewValue = r.opcode;
            e.previewMsg = JoinWarnings(r.warnings);
            e.previewWarn = !r.warnings.empty();
        }
    }
    else
    {
        std::string err;
        size_t col = 0;
        if (!ParseLiteral(e.buf, e.size, e.previewValue, err, col))
            e.previewMsg = err + " (column " + std::to_string(col + 1) + ")";
        else
        {
            e.previewOk = true;
            if (e.sharers) { e.previewMsg = SharersText(e.sharers, e.firstSharer); e.previewWarn = true; }
        }
    }
}

// Which bytes a halted SH-2 has already fetched, from the PC the panel shows for it. That PC is the decode
// stage (PC_ID; apply.py's SsDbgSh2Regs: GSREG_RPC, the next fetch address, is 4 bytes ahead of it, so the
// stage behind decode, IF, holds PC+2). Both are latched opcodes: Pipe_ID at PC and Pipe_IF at PC+2, so a write
// to [PC, PC+4) is never seen. The instruction cache fetches a longword at a time (sh7095.inc FetchIF keeps the
// other half in IBuffer): when PC+2 is longword aligned (PC & 2) its fetch read PC+2..PC+5, so the half at
// PC+4 is already buffered as well (found by reading the core: a 2-byte write at PC+2 ran its old opcode
// once on a real Mednafen; PC+4 follows from FetchIF but was not run). Returned as the end, exclusive.
static uint32_t PrefetchedEnd(uint32_t pc)
{
    return pc + ((pc & 2u) ? 6u : 4u);
}

// Does a write of 'size' bytes at 'addr' touch what a halted CPU at 'pc' has already fetched? The 0x0FFFFFFF
// mask folds the cache-through mirror (0x2....... vs 0x0.......) of the same RAM.
static bool WritesAlreadyFetched(uint32_t pc, uint32_t addr, uint32_t size)
{
    const uint64_t lo = pc & 0x0FFFFFFFu, hi = lo + (PrefetchedEnd(pc) - pc);
    const uint64_t wlo = addr & 0x0FFFFFFFu, whi = wlo + size;
    return wlo < hi && whi > lo;
}

// Enter on the edit box. Everything that can be checked is checked before the one WriteMemory call:
// the source is still the one the edit was opened on, the text assembles against memory as it is now,
// the backend accepts the address. A bad text keeps the editor open; a refusal ends it, leaving the bytes.
void AssemblyPanel::CommitEdit(IMemoryBackend& backend, Request& req)
{
    const bool literal = mEdit.kind == EditKind::Literal;
    const uint32_t addr = mEdit.target;
    const uint32_t size = mEdit.size;

    if (backend.SourceId() != mEdit.sourceId)
    {
        CancelEdit();
        SetNotice("Edit dropped: the memory source changed.", 2);
        return;
    }

    uint32_t oldValue = 0, newValue = 0;
    std::vector<std::string> warnings;
    std::string what;   // the text for the notice: the assembled line, or the literal
    size_t sharers = 0;
    uint32_t firstSharer = 0;
    if (!literal)
    {
        Sh2AsmContext ctx;
        uint16_t cur = 0;
        if (!BuildAsmContext(backend, addr, ctx, cur))
        {
            SetNotice("Cannot read memory at " + Hex8(addr) + ".", 2);
            mEdit.focusPending = true;
            return;
        }
        const Sh2AsmResult r = Sh2Assemble(mEdit.buf, ctx);
        if (!r.ok)
        {
            // Stay open so the text can be fixed; nothing was written.
            SetNotice("Cannot assemble: " + r.error + " (column " + std::to_string(r.errorColumn + 1) + ")", 2);
            mEdit.focusPending = true;
            return;
        }
        oldValue = cur;
        newValue = r.opcode;
        warnings = r.warnings;
        what = r.decoded.Operands.empty() ? r.decoded.Mnemonic : r.decoded.Mnemonic + " " + r.decoded.Operands;
        // The slot after a delayed branch stops being a slot when the branch is replaced. Only the panel
        // knows the old instruction, so it is the panel that says so (the assembler sees the new one).
        if (Sh2Decode(addr, cur).HasDelaySlot && !r.decoded.HasDelaySlot)
            warnings.push_back("replaced a delayed branch: the instruction at " + Hex8(addr + 2) +
                               " is no longer its delay slot");
        if (newValue == oldValue)
        {
            CancelEdit();
            SetNotice("No change at " + Hex8(addr) + ": the instruction already encodes as that.", 0);
            return;
        }
    }
    else
    {
        std::string err;
        size_t col = 0;
        if (!ParseLiteral(mEdit.buf, size, newValue, err, col))
        {
            SetNotice("Cannot write the literal: " + err + " (column " + std::to_string(col + 1) + ")", 2);
            mEdit.focusPending = true;
            return;
        }
        const MemoryReadResult cur = backend.ReadMemoryBatch({ { addr, size } })[0];
        if (!cur.success || cur.bytes.size() < size)
        {
            SetNotice("Cannot read the literal at " + Hex8(addr) + ".", 2);
            mEdit.focusPending = true;
            return;
        }
        for (uint32_t i = 0; i < size; ++i) oldValue = (oldValue << 8) | cur.bytes[i];
        // Counted again now: before the write it is what the user is about to change under other code.
        CountSharers(backend, mEdit.rowAddr, addr, size, sharers, firstSharer);
        char b[16];
        std::snprintf(b, sizeof b, size == 2 ? "0x%04X" : "0x%08X", newValue);
        what = b;
    }

    std::string result;
    int level = 0;
    if (!backend.CanWrite(addr))
    {
        const std::string why = backend.WriteRefusal(addr);
        result = why.empty() ? "Write refused at " + Hex8(addr) + "." : why;
        level = 2;
    }
    else
    {
        // One call for the whole opcode or literal: one poke, one staged record. Two byte writes could
        // leave half an instruction behind if the second were refused.
        uint8_t bytes[4];
        for (uint32_t i = 0; i < size; ++i) bytes[i] = (uint8_t)(newValue >> (8 * (size - 1 - i)));
        const size_t n = backend.WriteMemory(addr, bytes, size);
        if (n == size)
        {
            // A halted CPU has already pulled the next instructions into its pipeline; patching memory
            // does not reach them (the emulator's own cache invalidation cannot), so the old opcode can
            // run once. Only an instruction edit matters: a literal is read when its load executes.
            if (!literal && n == size && mBpStopActive && mCpu == mBpStopCpu)
            {
                if (WritesAlreadyFetched(mBpStopPc, addr, size))
                    warnings.push_back("the CPU is halted and had already fetched this instruction (the pipeline holds PC.." +
                                       Hex8(PrefetchedEnd(mBpStopPc) - 1) + "): it may execute the OLD instruction once before the new one runs");
                else
                {
                    // Halted in a delay slot, the fetch stage is not at PC+2: the taken branch already refilled it
                    // from its TARGET (sh7095.inc UCDelayBranch keeps the slot in PC_ID and calls
                    // FetchIF_ForceIBufferFill at the target, a longword read). The panel is not told the
                    // pipeline-stage addresses, so it works from the instruction before the PC: a static target
                    // gives the longword that was read; a register or stack target (jmp, jsr, rts, rte, braf,
                    // bsrf) cannot be derived here, so every instruction edit is warned about. A conditional
                    // bt/s / bf/s may not have been taken; warning anyway is the safe side.
                    const uint32_t prevAddr = mBpStopPc - 2;
                    uint16_t prevOp = 0;
                    bool havePrev = false;
                    if ((prevAddr & 0x0FFFFFFFu) == (addr & 0x0FFFFFFFu)) { prevOp = (uint16_t)oldValue; havePrev = true; }
                    else
                    {
                        const auto pr = backend.ReadMemoryBatch({ { prevAddr, 2 } });
                        if (!pr.empty() && pr[0].success && pr[0].bytes.size() >= 2) { prevOp = BE16(pr[0].bytes); havePrev = true; }
                    }
                    const DisassembledInstruction prev = havePrev ? Sh2Decode(prevAddr, prevOp) : DisassembledInstruction();
                    if (havePrev && prev.HasDelaySlot)
                    {
                        if (prev.HasBranchTarget)
                        {
                            const uint64_t lo = prev.BranchTarget & 0x0FFFFFFFu, hi = (lo | 3u) + 1u;
                            const uint64_t wlo = addr & 0x0FFFFFFFu, whi = wlo + size;
                            if (wlo < hi && whi > lo)
                                warnings.push_back("the CPU is halted in the delay slot of the branch at " + Hex8(prevAddr) +
                                                   " and had already fetched its target (" + Hex8(prev.BranchTarget) +
                                                   "): it may execute the OLD instruction once before the new one runs");
                        }
                        else
                            warnings.push_back("the CPU is halted in the delay slot of the branch at " + Hex8(prevAddr) +
                                               ", whose target is not known here and is already fetched: if this is that target, "
                                               "it may execute the OLD instruction once before the new one runs");
                    }
                }
            }
            result = "Wrote " + std::to_string(size) + " bytes at " + Hex8(addr) + " (" + what + "): " +
                     WriteDestText(backend.WriteDestination(addr));
            if (literal && sharers)
            {
                result += "; the literal is " + SharersText(sharers, firstSharer);
                level = 1;
            }
            if (!warnings.empty()) { result += ". Warning: " + JoinWarnings(warnings); level = 1; }
            req.assembled = true;
            req.asmAddr = addr;
            req.asmSize = size;
            req.asmOld = oldValue;
            req.asmNew = newValue;
            req.asmNote = result;
        }
        else
        {
            const std::string why = backend.WriteRefusal(addr);
            result = n == 0 ? (why.empty() ? "Write refused at " + Hex8(addr) + "." : why)
                            : "Partially written " + std::to_string(n) + "/" + std::to_string(size) + " at " +
                              Hex8(addr) + (why.empty() ? "" : ": " + why);
            level = 2;
        }
        // Even a short write may have changed memory, and a backend write does not move SourceId, so
        // with Auto Refresh off the cached rows would keep showing the old bytes.
        if (n > 0) mDropPages = true;
    }
    CancelEdit();
    SetNotice(result, level);
}

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
        const uint32_t rows = PageRows(span, first);
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
        const uint32_t rows = PageRows(span, first);
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
        CancelEdit();   // a hidden panel draws no edit box, so nothing would ever finish it
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
        CancelEdit();
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

    // The last edit's outcome (a refusal, an assembler error, "Wrote ..."), until it times out. The line is
    // reserved whether or not there is a notice, so the rows below it never move when one comes and goes.
    if (!mNotice.empty())
    {
        mNoticeAge -= ImGui::GetIO().DeltaTime;
        if (mNoticeAge <= 0.0f) mNotice.clear();
    }
    FixedStatusLine(mNotice,
                    mNoticeLevel == 2 ? ui::Color(IM_COL32(235, 100, 100, 255), IM_COL32(176, 32, 32, 255))
                  : mNoticeLevel == 1 ? ui::Color(IM_COL32(230, 180, 80, 255), IM_COL32(140, 90, 0, 255))
                                      : ui::Color(IM_COL32(150, 210, 150, 255), IM_COL32(30, 110, 50, 255)));

    // --- Frame the view: follow PC unless browsing ---
    // While an edit box is open the view stays put: a PC that moves would re-scroll the list (and, when
    // it leaves the region, swap the rows) out from under the box. Following resumes when it closes.
    if (mFollowPc && mEdit.kind == EditKind::None)
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
    else if (!mFollowPc && !mWindowValid)
    {
        mWindowAnchor = pc & ~1u;
        RequestScroll(mWindowAnchor, 0.35f);
        mWindowValid = true;
    }
    // Left alone while editing so that, once the box closes, a PC that moved meanwhile is scrolled to.
    if (mEdit.kind == EditKind::None) mLastPc = pc;   // the followed PC (also the Back-history anchor in Navigate)

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
    // The edit's target is gone when the bytes behind the address are different data, or the other CPU
    // is shown (its rows are not this row).
    if (mEdit.kind != EditKind::None && (sourceId != mEdit.sourceId || mCpu != mEdit.cpu))
    {
        CancelEdit();
        SetNotice("Edit cancelled: the memory source or CPU changed.", 1);
    }
    const bool startEditKey = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                              !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F2, false);
    bool editDrawn = false;

    // --- Table ---
    const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;
    if (!ImGui::BeginTable("asm", 6, flags))
    {
        CancelEdit();
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
            if (isBpHit)   ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, kColBpHitRow());
            else if (isPc) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, kColPcRow());
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
                const ImU32 c = tp->enabled ? ui::Color(IM_COL32(90, 160, 240, 255), IM_COL32(30,100,180,255)) : IM_COL32(95, 115, 140, 255);
                const float x = p.x + 17, r = 5.0f;
                const ImVec2 pts[4] = {{x, cy - r}, {x + r, cy}, {x, cy + r}, {x - r, cy}};
                dl->AddConvexPolyFilled(pts, 4, c);
            }
            if (isPc)
                dl->AddText(ImVec2(p.x + 26, p.y), ui::Color(IM_COL32(230, 230, 90, 255), IM_COL32(128,91,0,255)), ">");
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
                ImGui::PushStyleColor(ImGuiCol_Text, kColAddr());
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
            // Editing this row: the edit box replaces the mnemonic and operands (it holds both), and the
            // Bytes cell previews the encoding the text assembles to.
            const bool editingRow = mEdit.kind != EditKind::None && mEdit.rowAddr == ln.addr;
            // Double-click on Bytes or Instr (or F2 on the selected row) opens the instruction editor.
            // Operands keep their click-to-follow-branch meaning and the address its Memory jump.
            bool openAsm = startEditKey && mEdit.kind == EditKind::None && mHasSel &&
                           mSelLoAddr == mSelHiAddr && ln.addr == mSelLoAddr;
            ImGui::TableSetColumnIndex(2);
            if (editingRow && mEdit.kind == EditKind::Instruction)
            {
                UpdatePreview();
                if (mEdit.previewOk)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, kColMnem());
                    ImGui::Text("%04X", mEdit.previewValue);
                    ImGui::PopStyleColor();
                }
                else ImGui::TextDisabled("----");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%04X -> %s", mEdit.oldValue, mEdit.previewOk ? "new encoding" : "no valid encoding yet");
            }
            else if (ln.readable) { ImGui::PushStyleColor(ImGuiCol_Text, kColBytes()); ImGui::Text("%04X", ln.op); ImGui::PopStyleColor(); }
            else ImGui::TextDisabled("----");
            if (!editingRow && ln.readable && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) openAsm = true;
            openRowContext();
            ImGui::TableSetColumnIndex(3);
            if (editingRow) ImGui::TextUnformatted(" ");
            else if (ln.readable) { ImGui::PushStyleColor(ImGuiCol_Text, kColMnem()); ImGui::TextUnformatted(ln.ins.Mnemonic.c_str()); ImGui::PopStyleColor(); }
            else ImGui::TextDisabled("????");
            if (!editingRow && ln.readable && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) openAsm = true;
            openRowContext();
            ImGui::TableSetColumnIndex(4);
            Sh2OperandsDrawn ops;
            if (editingRow)
            {
                editDrawn = true;
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (mEdit.focusPending) { ImGui::SetKeyboardFocusHere(); mEdit.focusPending = false; }
                // Zero vertical frame padding for the same reason as the comment editor below: the list
                // is clipped on the assumption that every row is one text line tall.
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 0.0f));
                const bool enter = ImGui::InputText("##asm", mEdit.buf, sizeof(mEdit.buf),
                                                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                ImGui::PopStyleVar();
                // Enter commits (and leaves the box open when the text is bad). Anything else that
                // ends the activation -- Escape, a click elsewhere, focus moving away -- cancels.
                if (enter) CommitEdit(backend, req);
                else if (ImGui::IsItemDeactivated()) CancelEdit();
            }
            else if (ln.readable) ops = DrawSh2Operands(ln.ins);
            if (openAsm && !editingRow) BeginInstructionEdit(backend, ln);
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
                if (mEdit.kind != EditKind::None && mEdit.rowAddr == ln.addr)
                {
                    // Live result of the text in the box: the error in red with its column, else the
                    // warnings in amber, else a hint. Single line; the tooltip holds the whole text.
                    UpdatePreview();
                    const bool bad = !mEdit.previewOk;
                    const std::string msg = bad || mEdit.previewWarn ? mEdit.previewMsg
                                          : mEdit.kind == EditKind::Literal ? "Enter writes the literal, Esc cancels"
                                                                            : "Enter assembles, Esc cancels";
                    ImGui::PushStyleColor(ImGuiCol_Text, bad ? ui::Color(IM_COL32(235, 100, 100, 255), IM_COL32(176, 32, 32, 255))
                                          : mEdit.previewWarn ? ui::Color(IM_COL32(230, 180, 80, 255), IM_COL32(140, 90, 0, 255))
                                                              : kColCmt());
                    ImGui::TextUnformatted(msg.c_str());
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", msg.c_str());
                }
                else if (mEditingComment && mEditCommentAddr == ln.addr)
                {
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (mCommentFocus) { ImGui::SetKeyboardFocusHere(); mCommentFocus = false; }
                    // No vertical frame padding: the list is clipped on the assumption that every
                    // row is one text line tall, and a taller edit row shifts every scroll position
                    // computed from that height (a jump lands far from its target).
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                                        ImVec2(ImGui::GetStyle().FramePadding.x, 0.0f));
                    const bool enter = ImGui::InputText("##cmt", mCommentBuf, sizeof(mCommentBuf),
                                                        ImGuiInputTextFlags_EnterReturnsTrue);
                    ImGui::PopStyleVar();
                    if (enter || ImGui::IsItemDeactivated())
                    {
                        const std::string note = CleanEntryText(mCommentBuf);   // as the file holds it; blank is no note (A9-31)
                        if (!note.empty()) mComments[ln.addr] = note;
                        else               mComments.erase(ln.addr);
                        mEditingComment = false;
                        SaveAfterEdit();
                    }
                }
                else
                {
                    const std::string autoCmt = ln.readable ? Sh2Comment(ln.ins, regs, readMem) : std::string();
                    const char* txt = hasUser ? it->second.c_str() : autoCmt.c_str();
                    ImGui::PushStyleColor(ImGuiCol_Text, hasUser ? ui::Color(IM_COL32(190, 185, 140, 255), IM_COL32(112,89,25,255)) : kColCmt());
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
                if (mOfferDecompile && ImGui::MenuItem("Decompile Here"))
                { req.decompile = true; req.decompileAddr = ln.addr; }
                // The way back from a line (A7-5): the function containing this row is decompiled if it is not shown, and the line the row became is selected.
                if (mOfferDecompile && ImGui::MenuItem("Show in Pseudocode", nullptr, false, ln.readable))
                { req.showPseudocode = true; req.pseudoAddr = ln.addr; }
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
                // In-place edits write through the backend, so they follow its CanWrite; when it says no,
                // the tooltip carries the backend's own reason, as the hex editor and Structs panel do.
                {
                    const bool canAsm = ln.readable && backend.CanWrite(ln.addr);
                    if (ImGui::MenuItem("Assemble...", "F2", false, canAsm)) BeginInstructionEdit(backend, ln);
                    if (ln.readable && !canAsm && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    {
                        const std::string why = backend.WriteRefusal(ln.addr);
                        ImGui::SetTooltip("%s", why.empty() ? "Writing is not available here." : why.c_str());
                    }
                    uint32_t litEa = 0, litSize = 0;
                    const bool isLit = ln.readable && LiteralOf(ln.ins, litEa, litSize);
                    const bool canLit = isLit && backend.CanWrite(litEa);
                    if (ImGui::MenuItem("Edit Literal Value...", nullptr, false, canLit)) BeginLiteralEdit(backend, ln);
                    if (ln.readable && !canLit && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    {
                        if (isLit)
                        {
                            const std::string why = backend.WriteRefusal(litEa);
                            ImGui::SetTooltip("%s", why.empty() ? "Writing is not available here." : why.c_str());
                        }
                        else if (ln.ins.PcRelAmbiguous)
                            ImGui::SetTooltip("This row follows a delayed branch: which literal it loads depends on whether it runs in the delay slot.");
                        else
                            ImGui::SetTooltip("Only a PC-relative mov.w / mov.l load has a literal to edit.");
                    }
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
                { mComments.erase(ln.addr); SaveAfterEdit(); }
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

    // The edit's row left the clipped range (scrolled away, or the view moved): there is no box to finish
    // it in. The frame it opened on is exempt, its box is only submitted from the next one.
    if (mEdit.kind != EditKind::None && !editDrawn && (unsigned)ImGui::GetFrameCount() > mEdit.beganFrame)
        CancelEdit();
    if (mDropPages) { mPages.clear(); mDropPages = false; }

    ImGui::EndTable();

    ImGui::End();
}

// Best-effort comment store: one fixed file in the working directory, each line
// "ADDRHEX <note>". Mirrors WatchPanel's session persistence (a native-desktop
// stopgap that no-ops on the web build's ephemeral MEMFS).
namespace
{
const char* kCommentName = "assembly_comments.txt";
std::string CommentFile() { return WorkFilePath(kCommentName); }
const size_t kMaxCommentFileBytes = 8u * 1024u * 1024u;
}

void AssemblyPanel::SaveAfterEdit()
{
    std::string err;
    if (!SaveComments(&err) && mOnSaveError) mOnSaveError(err);
}

bool AssemblyPanel::LoadComments(std::string* error)
{
    std::string text;
    const std::string file = CommentFile();
    std::string why;
    switch (ReadFileCapped(file, kMaxCommentFileBytes, text))
    {
    case CappedRead::Ok: break;
    case CappedRead::Missing: mRefusedWhy.clear(); mBase.clear(); return true;
    case CappedRead::TooLarge: why = file + " is larger than 8 MiB; not read"; break;
    case CappedRead::NotRegular: why = file + " is not a regular file; not read"; break;
    case CappedRead::Unreadable: why = "could not read " + file; break;
    }
    if (!why.empty())
    {
        mRefusedWhy = file + " was refused (" + why + ")";   // no save may replace it (A9-19)
        if (error) *error = why;
        return false;
    }
    mRefusedWhy.clear();
    mComments.clear();
    mSkippedCommentLines = 0;
    std::string note;
    uint32_t addr = 0;
    for (const std::string& line : SplitLines(text))
    {
        switch (ParseAddressNameLine(line, addr, note))
        {
        case AddressNameLine::Entry: mComments[addr] = note; break;
        case AddressNameLine::Comment: break;
        case AddressNameLine::Malformed: ++mSkippedCommentLines; break;
        }
    }
    mBase.clear();
    for (const auto& kv : mComments) mBase[kv.first] = kv.second;
    return true;
}

bool AssemblyPanel::SaveComments(std::string* error) const
{
    // Only this session's changes, on the file as it is now, under a lock, atomically (A9-19, A9-20, A9-21); see StoreMerge.h.
    AddressMap cur;
    for (const auto& kv : mComments)
        if (!kv.second.empty()) cur[kv.first] = kv.second;
    auto classify = [](const std::string& line, uint32_t& a) {
        std::string n;
        return ParseAddressNameLine(line, a, n) == AddressNameLine::Entry;
    };
    auto format = [](uint32_t a, const std::string& n) {
        char b[16];
        std::snprintf(b, sizeof b, "%08x ", a);
        return std::string(b) + n + "\n";
    };
    return SaveStore(CommentFile(), "The assembly notes", mRefusedWhy, kMaxCommentFileBytes, mBase, cur, classify, format, std::string(), error);
}

}  // namespace sfe
