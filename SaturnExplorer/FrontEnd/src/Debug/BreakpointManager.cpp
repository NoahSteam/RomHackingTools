#include "Debug/BreakpointManager.h"

namespace sfe
{

Breakpoint* BreakpointManager::Find(int cpu, uint32_t addr, BpKind kind, uint32_t size)
{
    for (Breakpoint& b : mBps)
    {
        if (b.kind == kind && b.address == addr && b.size == size &&
            (kind != BpKind::Execution || b.cpu == cpu))
        {
            return &b;
        }
    }
    return nullptr;
}

bool BreakpointManager::ToggleExecution(int cpu, uint32_t addr)
{
    for (std::size_t i = 0; i < mBps.size(); ++i)
    {
        if (mBps[i].kind == BpKind::Execution && mBps[i].cpu == cpu &&
            mBps[i].address == addr)
        {
            mBps.erase(mBps.begin() + i);
            ++mGeneration;
            return false;
        }
    }
    Breakpoint b;
    b.id = mNextId++;
    b.kind = BpKind::Execution;
    b.cpu = cpu;
    b.address = addr;
    b.size = 2;
    b.enabled = true;
    mBps.push_back(b);
    ++mGeneration;
    return true;
}

bool BreakpointManager::HasExecutionAt(int cpu, uint32_t addr) const
{
    for (const Breakpoint& b : mBps)
    {
        if (b.kind == BpKind::Execution && b.cpu == cpu && b.address == addr)
        {
            return true;
        }
    }
    return false;
}

const Breakpoint* BreakpointManager::ExecutionAt(int cpu, uint32_t addr) const
{
    for (const Breakpoint& b : mBps)
    {
        if (b.kind == BpKind::Execution && b.cpu == cpu && b.address == addr)
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
    if (Breakpoint* existing = Find(0, addr, rw, size))
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
