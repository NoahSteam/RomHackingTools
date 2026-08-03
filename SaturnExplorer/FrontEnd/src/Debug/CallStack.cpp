#include "Debug/CallStack.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>

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
}

void FunctionNames::Load()
{
    std::ifstream f(kFunctionFile);
    if (!f) return;
    mNames.clear();
    std::string line;
    while (std::getline(f, line))
    {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        unsigned addr = 0;
        const size_t sp = line.find(' ');
        if (sp == std::string::npos || std::sscanf(line.c_str(), "%x", &addr) != 1) continue;
        std::string name = line.substr(sp + 1);
        if (!name.empty()) mNames[addr] = name;
    }
}

void FunctionNames::Save() const
{
    std::ofstream f(kFunctionFile);
    if (!f) return;
    for (const auto& kv : mNames)
        if (!kv.second.empty()) f << std::hex << kv.first << " " << kv.second << "\n";
}

size_t FunctionNames::Import(const char* path)
{
    std::ifstream f(path);
    if (!f) return 0;
    auto space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    size_t n = 0;
    std::string line;
    while (std::getline(f, line))
    {
        // Each line is "<hex-addr> <name>" — the address (optionally 0x-prefixed) then the
        // name, separated by any run of spaces or tabs. Leading whitespace, blank lines, and
        // ';' / '#' / '//' comments are tolerated so hand-made lists and simple map dumps load.
        size_t b = 0;
        while (b < line.size() && space(line[b])) ++b;
        if (b >= line.size()) continue;
        if (line[b] == ';' || line[b] == '#' ||
            (line[b] == '/' && b + 1 < line.size() && line[b + 1] == '/')) continue;

        char* end = nullptr;
        const unsigned long addr = std::strtoul(line.c_str() + b, &end, 16);
        if (end == line.c_str() + b) continue;   // no hex digits where the address should be

        size_t np = static_cast<size_t>(end - line.c_str());
        while (np < line.size() && space(line[np])) ++np;   // skip to the name
        std::string name = line.substr(np);
        while (!name.empty() && space(name.back())) name.pop_back();   // trim CR / trailing ws
        if (!name.empty()) { mNames[static_cast<uint32_t>(addr)] = name; ++n; }
    }
    return n;
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
    CallStackFrame f0;
    f0.cpu = cpu;
    f0.functionAddress = regs.pc;
    f0.returnAddress   = regs.pr;
    f0.stackPointer    = regs.r[15];
    f0.callSite        = 0;
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
        fr.functionAddress = c.ret;
        fr.stackPointer    = c.at;

        const MemoryReadResult& o = (i < opRes.size()) ? opRes[i] : MemoryReadResult{};
        if (o.success && o.bytes.size() >= 2)
        {
            const uint16_t op = uint16_t((o.bytes[0] << 8) | o.bytes[1]);
            if (IsSh2CallOpcode(op))
            {
                fr.callSite   = c.ret - 4;
                fr.confidence = FrameConfidence::Probable;
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

}  // namespace sfe
