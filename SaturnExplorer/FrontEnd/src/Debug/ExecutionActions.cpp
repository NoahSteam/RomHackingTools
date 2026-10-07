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

namespace
{
// What the editor offers is what happens. The other checkboxes are stored (so the editor and the
// wire format do not change when they are wired), but nothing acts on them yet, so a tracepoint must
// not claim them: one that says "Pause Emulator" and runs on is a lie the user finds out about late.
void Normalize(ExecutionAction& a)
{
    constexpr int kMaxEveryN = 0xFFFFFF;   // what SE_LIVE_TP_EVERY_SHIFT leaves room for
    if (a.repeatN < 1) a.repeatN = 1;
    if (a.repeatN > kMaxEveryN) a.repeatN = kMaxEveryN;
    a.effects.pauseEmulator = false;
    a.effects.screenshot    = false;
    a.effects.memSnapshot   = false;
    a.effects.playSound     = false;
    a.effects.runScript     = false;
}
}  // namespace

uint64_t ExecutionActions::Add(const ExecutionAction& in)
{
    ExecutionAction a = in;
    Normalize(a);
    a.id = mNextId++;
    a.hits = 0;
    a.seen = 0;
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
            Normalize(a);
            a.hits = hits;
            a.seen = 0;                     // the policy may have changed: count afresh
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
        if (a->enabled != enabled)
        {
            a->enabled = enabled;
            a->rearm = static_cast<uint8_t>((a->rearm + 1u) & 0xFu);
            if (enabled) a->seen = 0;   // switched back on: a fire-once one is armed again
            ++mGeneration;
        }
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

bool ExecutionActions::AcceptHit(uint64_t id, bool emulatorApplies)
{
    ExecutionAction* a = Get(id);
    if (!a || !a->enabled) return false;

    bool act = true;
    if (!emulatorApplies)
    {
        // Count the executions that qualified: the condition held, or there is none.
        ++a->seen;
        switch (a->repeat)
        {
            case RepeatMode::EveryTime: act = true; break;
            case RepeatMode::Once:      act = a->seen == 1; break;
            case RepeatMode::EveryN:
                act = a->repeatN <= 1 || (a->seen % static_cast<uint64_t>(a->repeatN)) == 0;
                break;
        }
    }
    if (!act) return false;

    ++a->hits;
    if (a->repeat == RepeatMode::Once)
    {
        // Disabled through the generation, not just locally: the emulator only learns of it when the
        // set is re-sent, and until then it keeps reporting an action the client has stopped
        // honouring.
        a->enabled = false;
        a->rearm = static_cast<uint8_t>((a->rearm + 1u) & 0xFu);
        ++mGeneration;
    }
    return true;
}

void ExecutionActions::ResetCounts()
{
    for (ExecutionAction& a : mActions) a.seen = 0;
}

}  // namespace sfe
