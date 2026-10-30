#include "Debug/CallStack.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>

#include "AddressNameLine.h"
#include "FileRead.h"
#include "FileWrite.h"

namespace sfe
{
namespace
{
const char* kFunctionFile = "saturn_functions.txt";

// Read a big-endian u32 out of a byte span at 'off'; false if it doesn't fit.
bool BeU32(const std::vector<uint8_t>& b, size_t off, uint32_t& out)
{
    if (off + 4 > b.size()) return false;
    out = (uint32_t(b[off]) << 24) | (uint32_t(b[off + 1]) << 16) |
          (uint32_t(b[off + 2]) << 8) | uint32_t(b[off + 3]);
    return true;
}

// The largest names file or symbol map read: 8 MiB is some 200,000 entries.
const size_t kMaxNamesFileBytes = 8u * 1024u * 1024u;
}  // namespace

bool IsPlausibleCodeAddress(uint32_t addr)
{
    if (addr & 1u) return false;                    // SH-2 instructions are 2-byte aligned
    const uint32_t a = addr & 0x0FFFFFFFu;          // ignore cache/mirror bits
    if (a >= 0x06000000u && a < 0x06100000u) return true;   // HWRAM (1 MB) — most game code
    if (a >= 0x00200000u && a < 0x00300000u) return true;   // LWRAM (1 MB)
    // Deliberately NOT the boot ROM (0..0x80000): that range collides with the small
    // integers, counts, and flags that fill a real stack, producing false "code"
    // pointers. Game code runs from HWRAM/LWRAM; a rare BIOS frame is worth missing to
    // keep the reconstructed stack clean.
    return false;
}

bool IsSh2CallOpcode(uint16_t op)
{
    if ((op & 0xF000u) == 0xB000u) return true;   // bsr  disp
    if ((op & 0xF0FFu) == 0x400Bu) return true;   // jsr  @Rn
    if ((op & 0xF0FFu) == 0x0003u) return true;   // bsrf Rn
    return false;
}

// The called function's entry point, when the call instruction encodes it.
//
// `bsr disp12` does: the target is site + 4 + (sign-extended disp << 1), and the site is fixed
// at returnAddress - 4 because PR holds the address after the delay slot. `jsr @Rn` and
// `bsrf Rn` do not -- the target came out of a register, and a stack image taken later has no
// record of what was in it. Guessing there is what CPU-03 is about, so this returns false and
// the frame carries no function address at all.
bool DecodeCallTarget(uint16_t op, uint32_t site, uint32_t& target)
{
    if ((op & 0xF000u) != 0xB000u) return false;
    // Sign-extend the 12-bit displacement by arithmetic, not by shifting: the shift-based idiom
    // for this (cast to int16_t, shift back down) left-shifts and right-shifts values that can be
    // negative, which is what UBSan caught in the emulator-side call tracking (FV-005).
    int32_t disp = static_cast<int32_t>(op & 0x0FFFu);
    if (disp >= 0x0800) disp -= 0x1000;
    target = site + 4 + static_cast<uint32_t>(disp * 2);
    return true;
}

// ---- FunctionNames -------------------------------------------------------------

std::string FunctionNames::NameOf(uint32_t address) const
{
    auto it = mNames.find(address);
    if (it != mNames.end() && !it->second.empty()) return it->second;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "sub_%06X", address & 0x00FFFFFFu);
    return buf;
}

bool FunctionNames::HasName(uint32_t address) const
{
    auto it = mNames.find(address);
    return it != mNames.end() && !it->second.empty();
}

void FunctionNames::Rename(uint32_t address, const std::string& name)
{
    if (name.empty()) mNames.erase(address);
    else              mNames[address] = name;
    ++mGeneration;
}

namespace
{
// A names file's text into 'names'. Returns false, with 'error', for a file that is there and was not read.
bool ReadNames(const std::string& path, std::map<uint32_t, std::string>& names, size_t& entries, size_t& skipped,
               size_t& firstSkipped, std::string& error, bool& missing)
{
    missing = false;
    std::string text;
    switch (ReadFileCapped(path, kMaxNamesFileBytes, text))
    {
    case CappedRead::Ok: break;
    case CappedRead::Missing: missing = true; return true;
    case CappedRead::TooLarge: error = path + " is larger than " + std::to_string(kMaxNamesFileBytes >> 20) + " MiB; not read"; return false;
    case CappedRead::NotRegular: error = path + " is not a regular file; not read"; return false;
    case CappedRead::Unreadable: error = "could not read " + path; return false;
    }
    entries = skipped = firstSkipped = 0;
    size_t lineNo = 0, p = 0;
    uint32_t addr = 0;
    std::string name;
    while (p <= text.size())
    {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        const std::string line = text.substr(p, e - p);
        const bool last = e == text.size();
        p = e + 1;
        if (last && line.empty()) break;
        ++lineNo;
        switch (ParseAddressNameLine(line, addr, name))
        {
        case AddressNameLine::Entry: names[addr] = name; ++entries; break;
        case AddressNameLine::Comment: break;
        case AddressNameLine::Malformed: if (!skipped++) firstSkipped = lineNo; break;
        }
    }
    return true;
}
}  // namespace

bool FunctionNames::Load(std::string* error)
{
    std::map<uint32_t, std::string> names;
    size_t entries = 0;
    bool missing = false;
    std::string why;
    mSkipped = mFirstSkipped = 0;
    if (!ReadNames(kFunctionFile, names, entries, mSkipped, mFirstSkipped, why, missing))
    {
        if (error) *error = why;
        return false;
    }
    if (missing) return true;
    mNames = std::move(names);
    ++mGeneration;
    return true;
}

bool FunctionNames::Save(std::string* error) const
{
    std::string text;
    char b[16];
    for (const auto& kv : mNames)
    {
        if (kv.second.empty()) continue;
        std::snprintf(b, sizeof b, "%x ", kv.first);
        text += b;
        text += kv.second;
        text += '\n';
    }
    // Staged and renamed: written in place, a second instance (or a crash) read half of it as the whole list (A9-3).
    std::string why;
    if (WriteFileAtomically(kFunctionFile, text.data(), text.size(), why)) return true;
    if (error) *error = std::string(kFunctionFile) + ": " + why;
    return false;
}

size_t FunctionNames::Import(const char* path, std::string* error)
{
    std::map<uint32_t, std::string> names;
    size_t entries = 0;
    bool missing = false;
    std::string why;
    mSkipped = mFirstSkipped = 0;
    if (!path || !ReadNames(path, names, entries, mSkipped, mFirstSkipped, why, missing))
    {
        if (error) *error = why;
        return 0;
    }
    if (missing) return 0;
    for (auto& kv : names) mNames[kv.first] = std::move(kv.second);
    if (entries) ++mGeneration;
    return entries;
}

// ---- CallStack -----------------------------------------------------------------

// Heuristic reconstruction, returned as a fresh vector so both Reconstruct (which
// stores it) and ReconcileHeuristicTail (which grafts part of it) can share the logic.
static std::vector<CallStackFrame> ReconstructHeuristic(int cpu, const se_sh2_regs& regs,
                                                        IMemoryBackend& mem)
{
    std::vector<CallStackFrame> out;

    // Frame #0: the halted instruction. PC is exactly known (● Confirmed); it returns
    // to PR.
    // PC is exactly where execution stopped, which is not where the function began: without a
    // recorded call event or a symbol table there is nothing here that says. So frame #0 reports
    // PC as the address it is at, and claims no entry point.
    CallStackFrame f0;
    f0.cpu = cpu;
    f0.functionAddress = 0;
    f0.functionKnown   = false;
    f0.returnAddress   = regs.pr;
    f0.stackPointer    = regs.r[15];
    f0.callSite        = 0;
    f0.currentAddress  = regs.pc;
    f0.confidence      = FrameConfidence::Confirmed;
    out.push_back(f0);

    if (!mem.Connected()) return out;

    // Walk a bounded window of the stack from R15 upward, collecting return addresses.
    const uint32_t sp   = regs.r[15];
    const uint32_t span = 0x400;          // 1 KB of stack
    const size_t   kMaxFrames = 32;

    std::vector<MemoryReadResult> stack = mem.ReadMemoryBatch({{sp, span}});
    if (stack.empty() || !stack[0].success) return out;
    const std::vector<uint8_t>& sb = stack[0].bytes;

    // Pass 1: find candidate return addresses (code-looking, 4-aligned stack words).
    struct Cand { uint32_t at; uint32_t ret; };
    std::vector<Cand> cands;
    for (uint32_t off = 0; off + 4 <= sb.size() && cands.size() < kMaxFrames; off += 4)
    {
        uint32_t w = 0;
        if (!BeU32(sb, off, w)) break;
        if (IsPlausibleCodeAddress(w)) cands.push_back({sp + off, w});
    }
    if (cands.empty()) return out;

    // Pass 2: batch-read the 16-bit opcode preceding each candidate's target (ret-4);
    // a call there confirms it as a real return address (◐ Probable), else drop it as
    // likely data. Unreadable preceding word -> ○ Heuristic (can't verify).
    std::vector<MemoryReadRequest> ops;
    ops.reserve(cands.size());
    for (const Cand& c : cands) ops.push_back({c.ret - 4, 2});
    std::vector<MemoryReadResult> opRes = mem.ReadMemoryBatch(ops);

    for (size_t i = 0; i < cands.size(); ++i)
    {
        const Cand& c = cands[i];
        CallStackFrame fr;
        fr.cpu = cpu;
        fr.returnAddress   = c.ret;
        fr.currentAddress  = c.ret;
        fr.stackPointer    = c.at;

        const MemoryReadResult& o = (i < opRes.size()) ? opRes[i] : MemoryReadResult{};
        if (o.success && o.bytes.size() >= 2)
        {
            const uint16_t op = uint16_t((o.bytes[0] << 8) | o.bytes[1]);
            if (IsSh2CallOpcode(op))
            {
                fr.callSite   = c.ret - 4;
                fr.confidence = FrameConfidence::Probable;
                // Only a bsr says where the callee starts. For jsr/bsrf the frame keeps its
                // return address and no function address, which the UI shows as such.
                fr.functionKnown = DecodeCallTarget(op, fr.callSite, fr.functionAddress);
                out.push_back(fr);
            }
            // else: code-looking word not preceded by a call -> almost certainly data,
            // skip it to keep the stack clean.
        }
        else
        {
            fr.callSite   = 0;
            fr.confidence = FrameConfidence::Heuristic;
            out.push_back(fr);
        }
    }
    return out;
}

void CallStack::Reconstruct(int cpu, const se_sh2_regs& regs, IMemoryBackend& mem)
{
    const int idx = Idx(cpu);
    mFrames[idx] = ReconstructHeuristic(cpu, regs, mem);
    mSelected[idx] = 0;
}

void CallStack::ReconcileHeuristicTail(int cpu, const se_sh2_regs& regs, IMemoryBackend& mem)
{
    const int idx = Idx(cpu);
    if (mFrames[idx].empty()) { Reconstruct(cpu, regs, mem); return; }

    // Graft heuristic caller frames that live below (at a higher stack address than) the
    // deepest confirmed frame — those predate when recording started.
    const uint32_t deepestSp = mFrames[idx].back().stackPointer;
    std::vector<CallStackFrame> heur = ReconstructHeuristic(cpu, regs, mem);
    for (const CallStackFrame& h : heur)
        if (h.stackPointer > deepestSp && h.confidence != FrameConfidence::Confirmed)
            mFrames[idx].push_back(h);
}

void CallStack::SetConfirmed(int cpu, std::vector<CallStackFrame> frames)
{
    const int idx = Idx(cpu);
    mFrames[idx] = std::move(frames);
    if (mSelected[idx] >= static_cast<int>(mFrames[idx].size())) mSelected[idx] = 0;
}

void CallStack::Clear(int cpu)
{
    const int idx = Idx(cpu);
    mFrames[idx].clear();
    mSelected[idx] = 0;
}

void CallStack::ClearAll()
{
    Clear(0);
    Clear(1);
}

void CallStack::Select(int cpu, int frame)
{
    const int idx = Idx(cpu);
    if (frame >= 0 && frame < static_cast<int>(mFrames[idx].size())) mSelected[idx] = frame;
}

const char* StopBannerLabel(bool haltActive, bool fromStep, bool dmaWatch, bool userBreakpoint,
                            bool watchpointArmed)
{
    if (!haltActive)      return "PAUSED";
    if (fromStep)         return "STEPPED";
    if (dmaWatch)         return "DMA WATCH HIT";
    if (userBreakpoint)   return "BREAKPOINT HIT";
    // A halting data watchpoint reports the accessing instruction through the execution-stop path,
    // so a stop with no breakpoint at the PC is one -- if one is set at all.
    if (watchpointArmed)  return "WATCHPOINT HIT";
    // Nothing the app has set explains the stop. Naming a cause would be a guess.
    return "HALTED";
}

StepOutTarget ChooseStepOutTarget(const std::vector<CallStackFrame>& frames, const se_sh2_regs& regs)
{
    StepOutTarget t;
    if (frames.empty())
    {
        t.why = "there is no call stack for this CPU";
        return t;
    }
    const CallStackFrame& f = frames.front();   // innermost: the frame the CPU is executing in
    if (!f.returnRecorded)
    {
        t.why = "no recorded call covers this frame, and PR is overwritten by any call the function "
                "makes, so its return address cannot be told from the stack alone";
        return t;
    }
    if (!IsPlausibleCodeAddress(f.returnAddress))
    {
        t.why = "the recorded return address is not in code";
        return t;
    }
    if (f.returnAddress == regs.pc)
    {
        t.why = "the recorded return address is the current instruction";
        return t;
    }
    // The callee runs below the stack the call was made at, give or take its delay slot: the slot of
    // a bsr/jsr runs before the callee does and may push or pop, so the callee's R15 is not exactly
    // bounded by the recorded one. The slack is for that; a recording of some other stack is far
    // outside it.
    constexpr uint32_t kDelaySlotSlack = 0x40;
    if (f.stackPointer != 0 && regs.r[15] > f.stackPointer + kDelaySlotSlack)
    {
        t.why = "the stack pointer is above the stack the recorded call was made at "
                "(the recording does not match this frame)";
        return t;
    }
    t.ok            = true;
    t.returnAddress = f.returnAddress;
    return t;
}

}  // namespace sfe
