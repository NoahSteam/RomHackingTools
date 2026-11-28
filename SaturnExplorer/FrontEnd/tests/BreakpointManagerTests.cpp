// Unit tests for BreakpointManager — in particular that execution breakpoints are shared
// across both SH-2s (CPU-01): setting one from "either CPU" must dedup to a single entry
// keyed by address alone, and the lookups the stop handler relies on must see it regardless
// of which CPU argument a caller used to get there historically.
#include "Debug/BreakpointEdit.h"
#include "Debug/BreakpointManager.h"

#include <cstdio>

using namespace sfe;

namespace
{
int gFail = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; }
}
}  // namespace

int main()
{
    // Toggling an execution BP at an address, then "toggling from the other CPU" at the
    // same address, must be seen as the SAME breakpoint (add, then remove) rather than
    // stacking a second entry — there is no cpu argument any more to tell them apart with.
    {
        BreakpointManager bps;
        Check(bps.ToggleExecution(0x1000) == true, "first toggle adds a BP");
        Check(bps.All().size() == 1, "exactly one BP exists after adding");
        Check(bps.HasExecutionAt(0x1000), "HasExecutionAt sees the new BP");
        Check(bps.ExecutionAt(0x1000) != nullptr, "ExecutionAt finds the new BP");

        // A second toggle at the same address must remove it, not add a duplicate — this is
        // the address-only dedup CPU-01 asks for: two "different CPU" toggles at the same PC
        // must not resolve to two breakpoints.
        Check(bps.ToggleExecution(0x1000) == false, "second toggle removes it");
        Check(bps.All().empty(), "no BP remains after the second toggle");
        Check(!bps.HasExecutionAt(0x1000), "HasExecutionAt no longer sees it");
        Check(bps.ExecutionAt(0x1000) == nullptr, "ExecutionAt no longer finds it");
    }

    // A halt is explained by the user's breakpoints only if one is ARMED there: a disabled entry is in
    // the list but installs nothing in the emulator.
    {
        BreakpointManager bps;
        bps.ToggleExecution(0x1800);
        Check(bps.HasEnabledExecutionAt(0x1800), "an enabled BP is armed");
        bps.SetEnabled(bps.ExecutionAt(0x1800)->id, false);
        Check(bps.HasExecutionAt(0x1800), "a disabled BP still exists");
        Check(!bps.HasEnabledExecutionAt(0x1800), "but is not armed");
        Check(!bps.HasEnabledExecutionAt(0x1802), "nothing is armed elsewhere");
    }

    // A PC breakpoint fires "on whichever CPU reaches it" — ConditionalExecutionAt and
    // IsAccessLogHalt (used by the stop handler) must see a BP set once, address-only, with
    // no notion of which CPU it was "for".
    {
        BreakpointManager bps;
        bps.ToggleExecution(0x2000);
        const Breakpoint* bp = bps.ExecutionAt(0x2000);
        Check(bp != nullptr, "BP present before setting a condition");
        bps.SetCondition(bp->id, "r4 == 0x1234");

        const Breakpoint* guarded = bps.ConditionalExecutionAt(0x2000);
        Check(guarded != nullptr, "ConditionalExecutionAt finds the guarded BP by address");
        Check(guarded->condition == "r4 == 0x1234", "the guard round-trips");

        // Enabled execution BP at this PC means it owns the halt, not a logging watchpoint.
        Check(!bps.IsAccessLogHalt(0x2000), "an enabled execution BP owns the halt at its PC");
        // No watchpoints at all: OnlyLoggingWatchpoints has nothing to call "logging", so a
        // halt elsewhere isn't treated as an access-log event either.
        Check(!bps.IsAccessLogHalt(0x3000), "no owning BP and no watchpoints is not a log halt");

        // Add a logging memory watchpoint: now a halt with no owning execution BP (at any
        // other address) IS an access-log event.
        const uint64_t wpId = bps.AddMemory(0x7000, 2, BpKind::MemWrite);
        bps.SetLogAccess(wpId, true);
        Check(bps.IsAccessLogHalt(0x3000), "a logging watchpoint with no halting one present "
                                           "makes an unowned halt an access-log event");
        // But the shared execution BP at 0x2000 still wins over the logging watchpoint.
        Check(!bps.IsAccessLogHalt(0x2000), "an execution BP still owns its own PC's halt");
    }

    // Setting an execution BP never touches the cpu field in a way that would make two
    // breakpoints at different addresses look related, and memory breakpoints (which may
    // still use kind/size/address per-entry) are unaffected by AddMemory's own dedup.
    {
        BreakpointManager bps;
        bps.ToggleExecution(0x4000);
        bps.ToggleExecution(0x5000);
        Check(bps.All().size() == 2, "two distinct addresses stay two distinct BPs");

        const uint64_t generationBefore = bps.Generation();
        const uint64_t id1 = bps.AddMemory(0x6000, 2, BpKind::MemWrite);
        const uint64_t id2 = bps.AddMemory(0x6000, 2, BpKind::MemWrite);
        Check(id1 == id2, "AddMemory dedups an identical memory watchpoint (unchanged behavior)");
        Check(bps.Generation() == generationBefore + 1,
              "the duplicate AddMemory call did not bump the generation again");
    }

    // Every Execution entry leaves cpu at 0, whatever route created it. This is the
    // invariant the wire packer leans on: it tags SE_LIVE_BP_CPU_SLAVE straight from b.cpu,
    // and that bit is reserved for execution descriptors, so a nonzero cpu here would put a
    // slave flag on the wire that an installer is required to ignore -- an inconsistency the
    // emulator side would be right to trip over.
    {
        BreakpointManager bps;
        bps.ToggleExecution(0x2000);
        bps.ToggleExecution(0x2004);
        bps.ToggleExecution(0x2004);        // remove
        bps.ToggleExecution(0x2004);        // and re-add: a fresh entry, still shared
        const uint64_t memId = bps.AddMemory(0x3000, 4, BpKind::MemReadWrite);
        bps.SetCondition(memId, "r0 == 1");
        bool allExecShared = true;
        for (const Breakpoint& b : bps.All())
        {
            if (b.kind == BpKind::Execution && b.cpu != 0) allExecShared = false;
        }
        Check(allExecShared, "every execution breakpoint carries cpu 0 (shared across both SH-2s)");
    }

    // "Set Execution Breakpoint" adds or arms, and never removes what is there.
    {
        BreakpointManager bps;
        bps.EnableExecution(0x4000);
        Check(bps.HasEnabledExecutionAt(0x4000) && bps.All().size() == 1, "Enable adds a breakpoint");
        bps.SetCondition(bps.All()[0].id, "r0 == 1");
        const uint64_t gen = bps.Generation();
        bps.EnableExecution(0x4000);
        Check(bps.HasEnabledExecutionAt(0x4000) && bps.All().size() == 1,
              "Enable on an existing breakpoint keeps it");
        Check(bps.All()[0].condition == "r0 == 1", "and keeps its condition");
        Check(bps.Generation() == gen, "and changes nothing, so nothing is re-sent");
        bps.SetEnabled(bps.All()[0].id, false);
        bps.EnableExecution(0x4000);
        Check(bps.HasEnabledExecutionAt(0x4000) && bps.All().size() == 1,
              "Enable arms a disabled breakpoint rather than deleting it");
        Check(bps.All()[0].condition == "r0 == 1", "with its condition intact");
    }

    // Which watchpoints could be behind a halt: independent of any execution breakpoint there.
    {
        BreakpointManager bps;
        bps.ToggleExecution(0x5000);
        const uint64_t halting = bps.AddMemory(0x06001000u, 4, BpKind::MemWrite);
        const uint64_t logging = bps.AddMemory(0x06002000u, 4, BpKind::MemRead);
        bps.SetLogAccess(logging, true);
        auto all = [](uint32_t, uint32_t, bool, bool) { return true; };
        auto none = [](uint32_t, uint32_t, bool, bool) { return false; };
        auto onlyFirst = [](uint32_t a, uint32_t, bool, bool) { return a == 0x06001000u; };
        // Direction reaches the predicate: a write watchpoint asks about writes only.
        bool sawWriteOnly = false;
        bps.WatchCausesFor([&](uint32_t a, uint32_t, bool rd, bool wr) {
            if (a == 0x06001000u) sawWriteOnly = !rd && wr;
            return false;
        });
        Check(sawWriteOnly, "a write watchpoint passes read=false, write=true");
        BreakpointManager::WatchCauses c = bps.WatchCausesFor(all);
        Check(c.halting && c.logging, "both kinds may have hit");
        c = bps.WatchCausesFor(none);
        Check(!c.halting && !c.logging, "an instruction that touches neither stops for neither");
        c = bps.WatchCausesFor(onlyFirst);
        Check(c.halting && !c.logging, "only the watchpoint in range counts");
        bps.SetEnabled(halting, false);
        c = bps.WatchCausesFor(all);
        Check(!c.halting && c.logging, "a disabled watchpoint is not a cause");
    }

    // ---- Edit ----
    {
        BreakpointManager bps;
        bps.ToggleExecution(0x1000);
        const uint64_t w = bps.AddMemory(0x06000000u, 4, BpKind::MemWrite);
        const uint64_t r = bps.AddMemory(0x06000010u, 4, BpKind::MemRead);
        const uint64_t ex = bps.All()[0].id;

        Check(bps.Edit(999, BpKind::MemRead, 0, 1) == EditResult::NotFound, "Edit: unknown id is NotFound");

        uint64_t g = bps.Generation();
        Check(bps.Edit(ex, BpKind::Execution, 0x1001, 2) == EditResult::Invalid, "Edit: odd exec address is Invalid");
        Check(bps.Edit(w, BpKind::MemWrite, 0x06000000u, 0) == EditResult::Invalid, "Edit: size 0 is Invalid");
        Check(bps.Edit(w, BpKind::MemWrite, 0xFFFFFFFEu, 4) == EditResult::Invalid, "Edit: wrapping range is Invalid");
        Check(bps.Generation() == g, "Edit: Invalid does not bump generation");
        Check(bps.Edit(w, BpKind::MemWrite, 0x06000010u, 4) == EditResult::Ok,
              "Edit: same address and size but a different kind is not a duplicate");
        Check(bps.Edit(w, BpKind::MemWrite, 0x06000000u, 4) == EditResult::Ok, "Edit: put it back");
        g = bps.Generation();
        Check(bps.Edit(w, BpKind::MemRead, 0x06000010u, 4) == EditResult::Duplicate, "Edit: duplicate memory BP");
        Check(bps.Generation() == g && bps.All()[1].kind == BpKind::MemWrite && bps.All()[1].address == 0x06000000u,
              "Edit: a rejected edit modifies nothing");
        Check(bps.Edit(r, BpKind::MemRead, 0xFFFFFFFCu, 4) == EditResult::Ok, "Edit: range ending at 0xFFFFFFFF is Ok");
        Check(bps.Edit(r, BpKind::MemRead, 0x06000010u, 4) == EditResult::Ok, "Edit: move it back");

        g = bps.Generation();
        Check(bps.Edit(w, BpKind::MemWrite, 0x06000000u, 4) == EditResult::Ok, "Edit: no-op is Ok");
        Check(bps.Generation() == g, "Edit: no-op does not bump generation");
        Check(bps.Edit(ex, BpKind::Execution, 0x1000, 99) == EditResult::Ok && bps.Generation() == g,
              "Edit: exec size is forced to 2, so this is still a no-op");

        bps.SetEnabled(w, false);
        g = bps.Generation();
        Check(bps.Edit(w, BpKind::MemReadWrite, 0x06000100u, 8) == EditResult::Ok, "Edit: real change is Ok");
        Check(bps.Generation() == g + 1, "Edit: real change bumps generation once");
        const Breakpoint& e = bps.All()[1];
        Check(e.id == w && !e.enabled && e.kind == BpKind::MemReadWrite && e.address == 0x06000100u && e.size == 8,
              "Edit: id, enabled state and list position kept");
    }
    {
        BreakpointManager bps;
        bps.ToggleExecution(0x2000);
        bps.ToggleExecution(0x3000);
        const uint64_t a = bps.All()[0].id;
        Check(bps.Edit(a, BpKind::Execution, 0x3000, 2) == EditResult::Duplicate, "Edit: duplicate execution BP");
        Check(bps.Edit(a, BpKind::Execution, 0x2000, 2) == EditResult::Ok, "Edit: exec to its own address is Ok");
        Check(bps.Edit(a, BpKind::Execution, 0x4000, 2) == EditResult::Ok && bps.All()[0].address == 0x4000u &&
              bps.All()[0].size == 2 && bps.All()[0].cpu == 0, "Edit: exec moves");

        bps.SetCondition(a, "r4 == 1");
        Check(bps.Edit(a, BpKind::Execution, 0x4002, 2) == EditResult::Ok && bps.All()[0].condition == "r4 == 1",
              "Edit: same kind keeps the condition");
        Check(bps.Edit(a, BpKind::MemWrite, 0x4002, 4) == EditResult::Ok && bps.All()[0].condition.empty(),
              "Edit: to a memory kind clears the condition");
        bps.SetLogAccess(a, true);
        Check(bps.Edit(a, BpKind::MemRead, 0x4002, 4) == EditResult::Ok && bps.All()[0].logAccess,
              "Edit: memory to memory keeps logAccess");
        Check(bps.Edit(a, BpKind::Execution, 0x4002, 7) == EditResult::Ok && !bps.All()[0].logAccess &&
              bps.All()[0].size == 2, "Edit: to Execution clears logAccess and forces size 2");
        Check(bps.Edit(a, BpKind::MemRead, 0x4003, 1) == EditResult::Ok, "Edit: odd address is fine for memory");
    }
    {
        uint32_t v = 0;
        Check(ParseBpAddress("06000000", v) && v == 0x06000000u, "addr: plain hex");
        Check(ParseBpAddress("  0x1fF  ", v) && v == 0x1FFu, "addr: 0x prefix, spaces, mixed case");
        Check(ParseBpAddress("0XFFFFFFFF", v) && v == 0xFFFFFFFFu, "addr: max");
        Check(ParseBpAddress("0000000000FF", v) && v == 0xFFu, "addr: many leading zeros");
        Check(!ParseBpAddress("", v) && !ParseBpAddress("   ", v) && !ParseBpAddress("0x", v), "addr: empty rejected");
        Check(!ParseBpAddress("xyz", v) && !ParseBpAddress("12G4", v) && !ParseBpAddress("-1", v) &&
              !ParseBpAddress("12 34", v), "addr: garbage rejected");
        Check(!ParseBpAddress("100000000", v), "addr: overflow rejected");
        Check(ParseBpByteCount("16", v) && v == 16, "count: decimal");
        Check(ParseBpByteCount("0299", v) && v == 299, "count: leading zero is decimal, not octal");
        Check(ParseBpByteCount(" 0x10 ", v) && v == 16, "count: 0x is hex");
        Check(ParseBpByteCount("0", v) && v == 0, "count: zero parses (the model rejects it)");
        Check(!ParseBpByteCount("", v) && !ParseBpByteCount("0x", v) && !ParseBpByteCount("1A", v) &&
              !ParseBpByteCount("-4", v) && !ParseBpByteCount("4294967296", v) && !ParseBpByteCount("0x100000000", v),
              "count: empty/garbage/overflow rejected");
        Check(ParseBpByteCount("4294967295", v) && v == 0xFFFFFFFFu, "count: max");
    }

    if (gFail == 0) std::printf("All BreakpointManager tests passed.\n");
    return gFail == 0 ? 0 : 1;
}
