// The frame half of the conservation law (ledger A3-124, patch 0068), shared by the decompiler
// suite's ledger (ReferenceOk, TestGameCode) and the generator.
//
// The access ledger compares the shown run with the reference run (every address volatile), whose
// list holds the bus accesses the flow reaches. A load or store the engine folds into the
// function's own frame is a variable in both runs, so neither lists it, and its record (fate
// FoldedOrdinary, 'frame' set) pairs with nothing: the fold was taken for conservation. A3-124's
// stores were folded, and the variable writes they became were then removed before the read of
// them was found -- an access lost with "0 dropped against the reference run". Patch 0068 records
// what the fold can still lose: an access whose value nothing but the engine's markers read when it
// went (FrameUnread), and a frame access that went with an unreachable block (FrameRemoved). A
// FrameUnread write is a loss when the code reads a byte of it later: some path of the flow as
// first followed reaches a folded read of the frame that overlaps it, whose own value something
// used (a pop of a register the engine takes for unaffected is no use), before folded writes of the
// frame have covered every byte of it. Only folded writes, exact frame offsets the engine itself
// placed, cover a byte; a store through a pointer that stayed one, or a call, may not write it.
// A call the engine guarded a local of the frame at (DecompileResult::frameCalls: its effect on the
// local is unknown, a lent callee's is known) reads the local's bytes too (A3-124's struct was handed
// to a call and read by nothing else), but for the save of a callee-saved register (a push of r8 to
// r14, or of a control or system register), which no callee is handed.
#pragma once

#include "Decompiler/MemorySnapshot.h"
#include "Decompiler/Sh2Decompiler.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace dts
{

struct FrameLoss
{
    uint32_t write = 0;    // the instruction of the write that went unread
    uint32_t read = 0;     // ...and of a read of the code that reads one of its bytes
    uint32_t offset = 0;   // the write's frame offset (the stack space's, as the record has it)
    uint8_t size = 0;
};

// A record's fate as the findings and listings name it.
inline const char* FateLabel(sfe::decomp::AccessFate::Kind k)
{
    using sfe::decomp::AccessFate;
    switch (k)
    {
    case AccessFate::FoldedOrdinary: return "folded";
    case AccessFate::Dead: return "dead";
    case AccessFate::Unreachable: return "unreachable";
    case AccessFate::SwitchLookup: return "switch-lookup";
    case AccessFate::FrameUnread: return "frame-unread";
    case AccessFate::FrameRemoved: return "frame-removed";
    default: return "malformed-piece";
    }
}

// The instruction words of a snapshot, big-endian, from whichever region holds the address (0 elsewhere).
inline std::function<uint16_t(uint32_t)> SnapshotWords(const std::shared_ptr<const sfe::decomp::MemorySnapshot>& snap)
{
    return [snap](uint32_t a) -> uint16_t {
        for (const sfe::decomp::MemoryRegion& g : snap->regions)
            if (a >= g.base && a - g.base + 1 < g.bytes.size())
                return static_cast<uint16_t>(g.bytes[a - g.base] << 8 | g.bytes[a - g.base + 1]);
        return 0;
    };
}

// Is the instruction a push of a callee-saved register: mov.l Rm,@-r15 with m 8 to 14, or sts.l / stc.l to @-r15.
inline bool SavesCalleeRegister(uint16_t w)
{
    if ((w & 0xFF0F) == 0x2F06) return ((w >> 4) & 15) >= 8 && ((w >> 4) & 15) <= 14;
    return w == 0x4F02 || w == 0x4F12 || w == 0x4F22 || w == 0x4F03 || w == 0x4F13 || w == 0x4F23;
}

// 'wordAt' gives the instruction word at an address (nothing given: no write is taken for a save).
inline std::vector<FrameLoss> FrameLosses(const sfe::decomp::DecompileResult& r, const std::function<uint16_t(uint32_t)>& wordAt = nullptr)
{
    using sfe::decomp::AccessFate;
    struct Acc
    {
        int block;
        uint32_t order;
        bool write;
        int64_t lo;     // the frame offset, sign-extended (the stack space wraps at 32 bits)
        int64_t hi;     // one past the last byte
        uint32_t insn;
        bool call;      // a call that may read the bytes
    };
    // Gone with unreachable code, or (a read) its value used by nothing.
    auto removed = [&](const AccessFate& f) {
        for (const AccessFate& g : r.accessFates)
            if ((g.fate == AccessFate::FrameRemoved || (g.fate == AccessFate::FrameUnread && !f.write)) && g.origBlock == f.origBlock &&
                g.origOrder == f.origOrder && g.write == f.write)
                return true;
        return false;
    };
    std::vector<Acc> accs;
    for (const AccessFate& f : r.accessFates)
    {
        if (!f.frame || f.fate != AccessFate::FoldedOrdinary || f.origBlock < 0 || removed(f)) continue;
        const int64_t lo = int64_t(int32_t(f.address));
        accs.push_back(Acc{ f.origBlock, f.origOrder, f.write, lo, lo + f.size, f.insn, false });
    }
    for (const auto& c : r.frameCalls)
        if (c.origBlock >= 0)
        {
            const int64_t lo = int64_t(int32_t(c.offset));
            accs.push_back(Acc{ c.origBlock, c.origOrder, false, lo, lo + c.size, 0, true });
        }
    std::sort(accs.begin(), accs.end(), [](const Acc& a, const Acc& b) {
        return a.block != b.block ? a.block < b.block : a.order < b.order;
    });
    const size_t nblocks = r.flowSuccessors.size();
    std::vector<std::vector<size_t>> inBlock(nblocks);
    for (size_t i = 0; i < accs.size(); ++i)
        if (size_t(accs[i].block) < nblocks) inBlock[size_t(accs[i].block)].push_back(i);

    std::vector<FrameLoss> out;
    for (const AccessFate& w : r.accessFates)
    {
        if (w.fate != AccessFate::FrameUnread || !w.write || w.origBlock < 0 || size_t(w.origBlock) >= nblocks || w.size == 0 ||
            w.size > 32)
            continue;
        const int64_t wlo = int64_t(int32_t(w.address));
        // The bytes of the write still unread and not yet covered, one bit each.
        auto overlap = [&](const Acc& a) {
            uint32_t m = 0;
            for (int64_t b = std::max(a.lo, wlo); b < std::min(a.hi, wlo + w.size); ++b) m |= 1u << uint32_t(b - wlo);
            return m;
        };
        const uint32_t all = w.size == 32 ? 0xFFFFFFFFu : ((1u << w.size) - 1);
        const bool save = wordAt && SavesCalleeRegister(wordAt(w.insn));
        std::vector<uint32_t> seen(nblocks, 0);   // bytes already followed from a block's start
        struct Item
        {
            int block;
            bool fromStart;
            uint32_t live;
        };
        std::vector<Item> todo{ Item{ w.origBlock, false, all } };
        uint32_t readAt = 0;
        bool lost = false;
        while (!todo.empty() && !lost)
        {
            Item it = todo.back();
            todo.pop_back();
            uint32_t live = it.live;
            for (size_t k : inBlock[size_t(it.block)])
            {
                const Acc& a = accs[k];
                if (!it.fromStart && a.order <= w.origOrder) continue;
                if (a.call && save) continue;
                const uint32_t m = overlap(a) & live;
                if (!m) continue;
                if (!a.write)
                {
                    lost = true;
                    readAt = a.insn;
                    break;
                }
                live &= ~m;
                if (!live) break;
            }
            if (lost || !live) continue;
            for (int s : r.flowSuccessors[size_t(it.block)])
            {
                if (s < 0 || size_t(s) >= nblocks) continue;
                const uint32_t fresh = live & ~seen[size_t(s)];
                if (!fresh) continue;
                seen[size_t(s)] |= fresh;
                todo.push_back(Item{ s, true, fresh });
            }
        }
        if (lost) out.push_back(FrameLoss{ w.insn, readAt, w.address, w.size });
    }
    return out;
}

}  // namespace dts
