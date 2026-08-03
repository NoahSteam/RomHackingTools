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

    if (gFail == 0) std::printf("All AccessLog tests passed.\n");
    return gFail == 0 ? 0 : 1;
}
