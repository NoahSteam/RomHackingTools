// Unit tests for AccessLog — the "what accesses this address" record: per-instruction
// dedup with a hit count, separate rows per CPU, latest-stack retention, and Clear.
#include "Debug/AccessLog.h"

#include <cstdio>

using namespace sfe;

namespace
{
int gFail = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; }
}

std::vector<CallStackFrame> Stack(uint32_t fn)
{
    CallStackFrame f;
    f.functionAddress = fn;
    return {f};
}
}  // namespace

int main()
{
    AccessLog log;
    Check(log.Empty() && log.Size() == 0, "starts empty");

    // Two hits from the same instruction collapse to one row, count 2, latest stack + frame.
    log.Record(0x06001000, 0, 10, "mov.l r0,@r4", Stack(0xAAAA));
    log.Record(0x06001000, 0, 12, "mov.l r0,@r4", Stack(0xBBBB));
    Check(log.Size() == 1, "same pc/cpu merges");
    Check(log.Records()[0].count == 2, "merged count");
    Check(log.Records()[0].lastFrame == 12, "latest frame kept");
    Check(!log.Records()[0].stack.empty() &&
          log.Records()[0].stack[0].functionAddress == 0xBBBB, "latest stack kept");

    // The decoded instruction follows the latest hit: another game's code at the same PC must
    // not keep the old mnemonic beside the new count and stack.
    log.Record(0x06001000, 0, 13, "add r1,r2", Stack(0xBBBB));
    Check(log.Records()[0].insn == "add r1,r2", "merge refreshes the instruction");
    log.Record(0x06001000, 0, 14, "", Stack(0xBBBB));
    Check(log.Records()[0].insn == "add r1,r2", "an undecodable hit keeps the last instruction");
    log.Clear();
    log.Record(0x06001000, 0, 10, "mov.l r0,@r4", Stack(0xAAAA));
    log.Record(0x06001000, 0, 12, "mov.l r0,@r4", Stack(0xBBBB));

    // A different instruction is a separate row; first-seen order preserved.
    log.Record(0x06002000, 0, 13, "mov.w r1,@r5", Stack(0xCCCC));
    Check(log.Size() == 2, "different pc adds a row");
    Check(log.Records()[0].pc == 0x06001000 && log.Records()[1].pc == 0x06002000,
          "first-seen order");

    // Same PC but the other CPU is its own row.
    log.Record(0x06001000, 1, 14, "mov.l r0,@r4", Stack(0xDDDD));
    Check(log.Size() == 3, "same pc different cpu is distinct");
    Check(log.Records()[2].cpu == 1, "slave row recorded");

    log.Clear();
    Check(log.Empty() && log.Size() == 0, "clear empties");

    // After Clear, a previously-seen key starts a fresh row (index was cleared too).
    log.Record(0x06001000, 0, 20, "mov.l r0,@r4", Stack(0xEEEE));
    Check(log.Size() == 1 && log.Records()[0].count == 1, "clear resets the index");

    // The log is bounded: a new accessor past the row cap is counted, not stored, while
    // instructions already in the log keep merging; a deep stack is cut to the cap.
    {
        AccessLog small(3, 2);
        small.Record(0x100, 0, 1, "a", Stack(1));
        small.Record(0x102, 0, 1, "b", Stack(2));
        small.Record(0x104, 0, 1, "c", Stack(3));
        Check(small.Size() == 3 && small.Full() && small.Dropped() == 0, "filled to the cap");
        small.Record(0x106, 0, 2, "d", Stack(4));
        small.Record(0x108, 1, 2, "e", Stack(5));
        Check(small.Size() == 3, "no row past the cap");
        Check(small.Dropped() == 2, "turned-away accessors are counted");
        small.Record(0x102, 0, 9, "b", Stack(22));
        Check(small.Size() == 3 && small.Records()[1].count == 2 &&
              small.Records()[1].stack[0].functionAddress == 22 && small.Dropped() == 2,
              "a known instruction still merges when the log is full");

        std::vector<CallStackFrame> deep;
        for (uint32_t i = 0; i < 10; ++i) { CallStackFrame f; f.functionAddress = i; deep.push_back(f); }
        small.Record(0x104, 0, 10, "c", deep);
        const std::vector<CallStackFrame>& kept = small.Records()[2].stack;
        Check(kept.size() == 2 && kept[0].functionAddress == 0 && kept[1].functionAddress == 1,
              "the stack is cut to its innermost frames");

        small.Clear();
        Check(small.Size() == 0 && small.Dropped() == 0 && !small.Full(), "Clear resets the budget");
    }

    // Defaults hold a long run's worth of accessors in bounded memory.
    {
        AccessLog big;
        std::vector<CallStackFrame> deep(64);
        for (uint32_t i = 0; i < 20000; ++i) big.Record(0x06000000 + i * 2, 0, i, "x", deep);
        Check(big.Size() == AccessLog::kDefaultMaxRows, "default row cap");
        Check(big.Dropped() == 20000 - AccessLog::kDefaultMaxRows, "the rest are counted");
        Check(big.Records()[0].stack.size() == AccessLog::kDefaultMaxStackFrames, "default stack cap");
    }

    if (gFail == 0) std::printf("All AccessLog tests passed.\n");
    return gFail == 0 ? 0 : 1;
}
