#include "ExecutionActions.h"

namespace sfe
{

bool ExecutionActions::HasLogAt(int cpu, uint32_t addr) const
{
    return LogAt(cpu, addr) != nullptr;
}

const ExecutionAction* ExecutionActions::LogAt(int cpu, uint32_t addr) const
{
    for (const ExecutionAction& a : mActions)
        if (a.type == ActionType::Log && a.cpu == cpu && a.address == addr)
            return &a;
    return nullptr;
}

bool ExecutionActions::ToggleLog(int cpu, uint32_t addr)
{
    for (size_t i = 0; i < mActions.size(); ++i)
    {
        const ExecutionAction& a = mActions[i];
        if (a.type == ActionType::Log && a.cpu == cpu && a.address == addr)
        {
            mActions.erase(mActions.begin() + static_cast<long>(i));
            ++mGeneration;
            return false;
        }
    }
    ExecutionAction a;
    a.type = ActionType::Log;
    a.cpu = cpu;
    a.address = addr;
    a.format = "PC={pc}";   // a sensible starting template; user edits it
    Add(a);
    return true;
}

uint64_t ExecutionActions::Add(const ExecutionAction& in)
{
    ExecutionAction a = in;
    a.id = mNextId++;
    a.hits = 0;
    mActions.push_back(a);
    ++mGeneration;
    return a.id;
}

void ExecutionActions::Update(const ExecutionAction& in)
{
    for (ExecutionAction& a : mActions)
    {
        if (a.id == in.id)
        {
            const uint64_t hits = a.hits;   // preserve the runtime hit count
            a = in;
            a.hits = hits;
            ++mGeneration;
            return;
        }
    }
}

ExecutionAction* ExecutionActions::Get(uint64_t id)
{
    for (ExecutionAction& a : mActions)
        if (a.id == id) return &a;
    return nullptr;
}

const ExecutionAction* ExecutionActions::Get(uint64_t id) const
{
    for (const ExecutionAction& a : mActions)
        if (a.id == id) return &a;
    return nullptr;
}

void ExecutionActions::SetEnabled(uint64_t id, bool enabled)
{
    if (ExecutionAction* a = Get(id))
    {
        if (a->enabled != enabled) { a->enabled = enabled; ++mGeneration; }
    }
}

void ExecutionActions::Remove(uint64_t id)
{
    for (size_t i = 0; i < mActions.size(); ++i)
    {
        if (mActions[i].id == id)
        {
            mActions.erase(mActions.begin() + static_cast<long>(i));
            ++mGeneration;
            return;
        }
    }
}

void ExecutionActions::Clear()
{
    if (!mActions.empty()) { mActions.clear(); ++mGeneration; }
}

void ExecutionActions::RecordHit(uint64_t id)
{
    if (ExecutionAction* a = Get(id))
    {
        ++a->hits;
        if (a->repeat == RepeatMode::Once) a->enabled = false;   // fire-once
    }
}

}  // namespace sfe
