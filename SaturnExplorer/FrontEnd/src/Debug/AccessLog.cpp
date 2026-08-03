#include "Debug/AccessLog.h"

namespace sfe
{

void AccessLog::Record(uint32_t pc, int cpu, uint32_t frame, std::string insn,
                       std::vector<CallStackFrame> stack)
{
    const int      c = cpu ? 1 : 0;
    const uint64_t k = Key(pc, c);
    auto it = mIndex.find(k);
    if (it != mIndex.end())
    {
        AccessRecord& r = mRecords[it->second];
        ++r.count;
        r.lastFrame = frame;
        r.stack = std::move(stack);
        return;
    }
    AccessRecord r;
    r.pc = pc;
    r.cpu = c;
    r.count = 1;
    r.lastFrame = frame;
    r.insn = std::move(insn);
    r.stack = std::move(stack);
    mIndex.emplace(k, mRecords.size());
    mRecords.push_back(std::move(r));
}

void AccessLog::Clear()
{
    mRecords.clear();
    mIndex.clear();
}

}  // namespace sfe
