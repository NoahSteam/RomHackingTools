#include "Debug/BreakpointManager.h"

namespace sfe
{

Breakpoint* BreakpointManager::Find(uint32_t addr, BpKind kind, uint32_t size)
{
    for (Breakpoint& b : mBps)
    {
        if (b.kind == kind && b.address == addr && b.size == size)
        {
            return &b;
        }
    }
    return nullptr;
}

bool BreakpointManager::ToggleExecution(uint32_t addr)
{
    // Address alone: execution BPs are shared across both SH-2s (see the header), so a
    // second toggle at the same PC — from either the master or slave Assembly view — must
    // remove the one entry that's already there, not add a sibling that only that view sees.
    for (std::size_t i = 0; i < mBps.size(); ++i)
    {
        if (mBps[i].kind == BpKind::Execution && mBps[i].address == addr)
        {
            mBps.erase(mBps.begin() + i);
            ++mGeneration;
            return false;
        }
    }
    Breakpoint b;
    b.id = mNextId++;
    b.kind = BpKind::Execution;
    b.cpu = 0;   // unused for Execution — shared across both SH-2s (see the header)
    b.address = addr;
    b.size = 2;
    b.enabled = true;
    mBps.push_back(b);
    ++mGeneration;
    return true;
}

void BreakpointManager::EnableExecution(uint32_t addr)
{
    if (const Breakpoint* b = ExecutionAt(addr)) SetEnabled(b->id, true);   // no-op when armed
    else ToggleExecution(addr);                                              // none there: the add
}

bool BreakpointManager::HasExecutionAt(uint32_t addr) const
{
    return ExecutionAt(addr) != nullptr;   // same address-keyed lookup, not a second copy of it
}

bool BreakpointManager::HasEnabledExecutionAt(uint32_t addr) const
{
    for (const Breakpoint& b : mBps)
    {
        if (b.kind == BpKind::Execution && b.enabled && b.address == addr) { return true; }
    }
    return false;
}

const Breakpoint* BreakpointManager::ExecutionAt(uint32_t addr) const
{
    for (const Breakpoint& b : mBps)
    {
        if (b.kind == BpKind::Execution && b.address == addr)
        {
            return &b;
        }
    }
    return nullptr;
}

const Breakpoint* BreakpointManager::ConditionalExecutionAt(uint32_t addr) const
{
    for (const Breakpoint& b : mBps)
    {
        if (b.enabled && b.kind == BpKind::Execution && b.address == addr &&
            !b.condition.empty())
        {
            return &b;
        }
    }
    return nullptr;
}

uint64_t BreakpointManager::AddMemory(uint32_t addr, uint32_t size, BpKind rw)
{
    if (Breakpoint* existing = Find(addr, rw, size))
    {
        return existing->id;   // dedup
    }
    Breakpoint b;
    b.id = mNextId++;
    b.kind = rw;
    b.address = addr;
    b.size = size;
    b.enabled = true;
    mBps.push_back(b);
    ++mGeneration;
    return b.id;
}

void BreakpointManager::SetEnabled(uint64_t id, bool enabled)
{
    for (Breakpoint& b : mBps)
    {
        if (b.id == id && b.enabled != enabled)
        {
            b.enabled = enabled;
            ++mGeneration;
            return;
        }
    }
}

void BreakpointManager::SetCondition(uint64_t id, const std::string& cond)
{
    // No generation bump: the guard is evaluated client-side on halt, so the emulator's
    // installed breakpoint set is unchanged.
    for (Breakpoint& b : mBps)
    {
        if (b.id == id) { b.condition = cond; return; }
    }
}

void BreakpointManager::SetLogAccess(uint64_t id, bool on)
{
    // No generation bump: log mode only changes how the client reacts to the watchpoint's
    // halt; the installed watchpoint (address/size/kind) is unchanged.
    for (Breakpoint& b : mBps)
    {
        if (b.id == id) { b.logAccess = on; return; }
    }
}

bool BreakpointManager::OnlyLoggingWatchpoints() const
{
    const WatchCauses c = WatchCausesFor([](uint32_t, uint32_t, bool, bool) { return true; });
    return c.logging && !c.halting;
}

bool BreakpointManager::IsAccessLogHalt(uint32_t pc) const
{
    // An *enabled* execution BP at this PC (either SH-2 — they're shared) owns the halt, not a
    // watchpoint. A disabled one can't have produced the halt, so it must not suppress the
    // access-log record + auto-resume (matches ConditionalExecutionAt, which also gates on enabled).
    for (const Breakpoint& b : mBps)
        if (b.enabled && b.kind == BpKind::Execution && b.address == pc) return false;
    return OnlyLoggingWatchpoints();
}

void BreakpointManager::Remove(uint64_t id)
{
    for (std::size_t i = 0; i < mBps.size(); ++i)
    {
        if (mBps[i].id == id)
        {
            mBps.erase(mBps.begin() + i);
            ++mGeneration;
            return;
        }
    }
}

void BreakpointManager::Clear()
{
    if (!mBps.empty())
    {
        mBps.clear();
        ++mGeneration;
    }
}

}  // namespace sfe
