// WriteInterval tests: every rule of PLAN.md §5.1's debugger-write classification, and that the result
// is the same whichever capture is marker A.
#include "AIInvestigation/WriteInterval.h"

#include <cstdio>
#include <string>

using namespace sfe::ai;

namespace
{
int gFail = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; }
}

// A source observed throughout one session/epoch/connection, with `entries` writes accepted so far.
struct Scene
{
    LedgerSegment ledger;
    WriteCheckpoint early, late;
};

LedgerSegment MakeLedger(uint64_t entries, WriteOutcome outcome = WriteOutcome::Applied)
{
    LedgerSegment l;
    l.sessionId = 1;
    l.timelineEpoch = 1;
    l.connectionGeneration = 1;
    l.firstSeq = 0;
    l.nextSeq = entries;
    l.complete = true;
    for (uint64_t i = 0; i < entries; ++i)
    {
        LedgerEntry e;
        e.seq = i;
        e.region = sfe::RegionId::Hwram;
        e.offset = static_cast<uint32_t>(i * 4);
        e.length = 4;
        e.outcome = outcome;
        l.entries.push_back(e);
    }
    return l;
}

WriteCheckpoint MakeCheckpoint(uint64_t serial, uint64_t position)
{
    WriteCheckpoint c;
    c.valid = true;
    c.captureSerial = serial;
    c.sessionId = 1;
    c.timelineEpoch = 1;
    c.connectionGeneration = 1;
    c.ledgerPosition = position;
    c.serverCounters = ServerCounters::Known;
    c.serverPokesApplied = 7;
    c.serverPokesDropped = 1;
    return c;
}

// Two captures with `between` writes accepted between them, in a ledger holding `before + between` writes.
Scene MakeScene(uint64_t before, uint64_t between)
{
    Scene s;
    s.ledger = MakeLedger(before + between + 2);
    s.early = MakeCheckpoint(10, before);
    s.late = MakeCheckpoint(11, before + between);
    return s;
}

// Classify in both orders (the markers can be set either way round) and require one answer.
IntervalResult Both(const WriteCheckpoint& a, const WriteCheckpoint& b, const LedgerSegment& l, const char* what)
{
    const IntervalResult ab = ClassifyWriteInterval(a, b, l);
    const IntervalResult ba = ClassifyWriteInterval(b, a, l);
    if (ab.status != ba.status || ab.reason != ba.reason || ab.acceptedWrites != ba.acceptedWrites)
    {
        std::printf("FAIL: %s -- A/B swapped gives %s/%s, not %s/%s\n", what, WriteStatusName(ba.status), WriteReasonName(ba.reason),
                    WriteStatusName(ab.status), WriteReasonName(ab.reason));
        ++gFail;
    }
    return ab;
}

void Expect(const IntervalResult& r, WriteStatus s, WriteReason why, const char* what)
{
    if (r.status != s || r.reason != why)
    {
        std::printf("FAIL: %s -- got %s/%s, wanted %s/%s\n", what, WriteStatusName(r.status), WriteReasonName(r.reason),
                    WriteStatusName(s), WriteReasonName(why));
        ++gFail;
    }
}

// ---- detected ---------------------------------------------------------------------------------

void TestDetected()
{
    {
        Scene s = MakeScene(3, 1);
        const IntervalResult r = Both(s.early, s.late, s.ledger, "one accepted write between");
        Expect(r, WriteStatus::Detected, WriteReason::AcceptedWritesInInterval, "a recorded poke between the captures");
        Check(r.acceptedWrites == 1, "and it counts one");
    }
    {
        Scene s = MakeScene(0, 5);
        Check(Both(s.early, s.late, s.ledger, "five").acceptedWrites == 5, "five accepted writes are counted");
    }
    {
        // The write is the first the ledger ever saw, and the last.
        Scene s = MakeScene(0, 1);
        Expect(Both(s.early, s.late, s.ledger, "first"), WriteStatus::Detected, WriteReason::AcceptedWritesInInterval, "the first ledger entry");
    }
    {
        // Outcome does not matter: dropped, unconfirmed and lost writes are still accepted writes, and an
        // accepted write changed the displayed snapshot whether or not the emulator ever applied it.
        for (WriteOutcome o : { WriteOutcome::Applied, WriteOutcome::Dropped, WriteOutcome::Unconfirmed, WriteOutcome::Lost, WriteOutcome::Pending })
        {
            Scene s = MakeScene(2, 1);
            s.ledger = MakeLedger(5, o);
            Expect(Both(s.early, s.late, s.ledger, "outcome"), WriteStatus::Detected, WriteReason::AcceptedWritesInInterval,
                   "every outcome counts as detected");
        }
    }
    {
        // A queued edit visible in the later capture but not yet confirmed: the §12 case. The ledger shows
        // it accepted, and the checkpoint also says it is pending.
        Scene s = MakeScene(2, 1);
        s.late.pendingWrites = 1;
        Expect(Both(s.early, s.late, s.ledger, "pending in B"), WriteStatus::Detected, WriteReason::PendingWrites, "a pending write in the later capture");
    }
    {
        // Pending in the EARLIER capture only: the edit was accepted before it, so no entry lies between,
        // but the earlier snapshot already showed it.
        Scene s = MakeScene(3, 0);
        s.early.pendingWrites = 2;
        Expect(Both(s.early, s.late, s.ledger, "pending in A"), WriteStatus::Detected, WriteReason::PendingWrites,
               "a pending write in the earlier capture, none between");
    }
    {
        Scene s = MakeScene(3, 0);
        s.late.unconfirmedWrites = 1;
        Expect(Both(s.early, s.late, s.ledger, "unconfirmed in B"), WriteStatus::Detected, WriteReason::UnconfirmedWrites, "an unconfirmed write in the later capture");
        s = MakeScene(3, 0);
        s.early.unconfirmedWrites = 1;
        Expect(Both(s.early, s.late, s.ledger, "unconfirmed in A"), WriteStatus::Detected, WriteReason::UnconfirmedWrites, "and in the earlier one");
    }
    {
        // Positive evidence outranks gaps: a write between the captures is detected even when the ledger
        // is incomplete, the server is old, or a counter reset.
        Scene s = MakeScene(2, 2);
        s.ledger.complete = false;
        s.early.serverCounters = s.late.serverCounters = ServerCounters::Unsupported;
        Expect(Both(s.early, s.late, s.ledger, "gaps"), WriteStatus::Detected, WriteReason::AcceptedWritesInInterval,
               "an accepted write is detected despite tracking gaps");
    }
    {
        // And pending writes are detected even when the other capture recorded nothing.
        Scene s = MakeScene(2, 0);
        s.early.valid = false;
        s.late.pendingWrites = 1;
        Expect(Both(s.early, s.late, s.ledger, "untracked + pending"), WriteStatus::Detected, WriteReason::PendingWrites,
               "pending writes count though the other capture has no tracking");
    }
}

// ---- none_detected ----------------------------------------------------------------------------

void TestNoneDetected()
{
    {
        Scene s = MakeScene(4, 0);
        const IntervalResult r = Both(s.early, s.late, s.ledger, "clean");
        Expect(r, WriteStatus::NoneDetected, WriteReason::CompleteTracking, "complete tracking with no writes");
        Check(r.acceptedWrites == 0, "no accepted writes");
    }
    {
        Scene s = MakeScene(0, 0);   // an empty ledger: the user never edited anything
        Expect(Both(s.early, s.late, s.ledger, "empty"), WriteStatus::NoneDetected, WriteReason::CompleteTracking, "an empty ledger");
    }
    {
        // A savestate or dump has no live server: edits are snapshot-local and the ledger sees them all.
        Scene s = MakeScene(2, 0);
        s.early.serverCounters = s.late.serverCounters = ServerCounters::NotApplicable;
        Expect(Both(s.early, s.late, s.ledger, "offline"), WriteStatus::NoneDetected, WriteReason::CompleteTracking, "a source with no server");
    }
    {
        // Writes before the earlier capture, long since applied, do not count against the interval.
        Scene s = MakeScene(6, 0);
        Expect(Both(s.early, s.late, s.ledger, "old edits"), WriteStatus::NoneDetected, WriteReason::CompleteTracking,
               "confirmed writes made before both captures");
    }
    {
        // The same capture compared with itself.
        Scene s = MakeScene(3, 0);
        Expect(ClassifyWriteInterval(s.early, s.early, s.ledger), WriteStatus::NoneDetected, WriteReason::CompleteTracking, "a capture against itself");
    }
    {
        // A ledger that starts after sequence 0 is fine as long as it covers both captures.
        Scene s = MakeScene(0, 0);
        s.ledger = MakeLedger(9);
        s.ledger.firstSeq = 4;
        s.ledger.entries.erase(s.ledger.entries.begin(), s.ledger.entries.begin() + 4);
        s.early = MakeCheckpoint(10, 6);
        s.late = MakeCheckpoint(11, 6);
        Expect(Both(s.early, s.late, s.ledger, "later segment"), WriteStatus::NoneDetected, WriteReason::CompleteTracking, "a segment covering both captures");
    }
}

// ---- unknown ----------------------------------------------------------------------------------

void TestUnknown()
{
    {
        Scene s = MakeScene(3, 0);
        s.late.sessionId = 2;
        Expect(Both(s.early, s.late, s.ledger, "session"), WriteStatus::Unknown, WriteReason::DifferentSession, "different sessions");
    }
    {
        Scene s = MakeScene(3, 0);
        s.late.timelineEpoch = 2;
        Expect(Both(s.early, s.late, s.ledger, "epoch"), WriteStatus::Unknown, WriteReason::DifferentEpoch, "different timeline epochs");
    }
    {
        // Debugger-write status across a reconnect is unknown (§12).
        Scene s = MakeScene(3, 0);
        s.late.connectionGeneration = 2;
        Expect(Both(s.early, s.late, s.ledger, "reconnect"), WriteStatus::Unknown, WriteReason::DifferentConnection, "a reconnect between the captures");
    }
    {
        Scene s = MakeScene(3, 0);
        s.late.valid = false;
        Expect(Both(s.early, s.late, s.ledger, "untracked"), WriteStatus::Unknown, WriteReason::NoTracking, "a capture with no tracking");
        s.early.valid = false;
        Expect(Both(s.early, s.late, s.ledger, "both untracked"), WriteStatus::Unknown, WriteReason::NoTracking, "neither tracked");
    }
    {
        // An older server without poke counters: its own writes (another client's included) cannot be ruled out.
        Scene s = MakeScene(3, 0);
        s.early.serverCounters = s.late.serverCounters = ServerCounters::Unsupported;
        Expect(Both(s.early, s.late, s.ledger, "old server"), WriteStatus::Unknown, WriteReason::ServerCountersUnsupported, "a server older than v23");
        s.early.serverCounters = ServerCounters::Known;
        Expect(Both(s.early, s.late, s.ledger, "old server, one side"), WriteStatus::Unknown, WriteReason::ServerCountersUnsupported, "or on one side only");
    }
    {
        Scene s = MakeScene(3, 0);
        s.early.serverCounters = ServerCounters::NotApplicable;
        Expect(Both(s.early, s.late, s.ledger, "mixed"), WriteStatus::Unknown, WriteReason::ServerCountersInconsistent,
               "one capture saw a server and the other did not");
    }
    {
        // History SE did not observe.
        Scene s = MakeScene(3, 0);
        s.ledger.complete = false;
        Expect(Both(s.early, s.late, s.ledger, "incomplete"), WriteStatus::Unknown, WriteReason::LedgerIncomplete, "an incomplete ledger");
    }
    {
        Scene s = MakeScene(3, 0);
        s.ledger.firstSeq = 5;   // starts after the captures
        s.ledger.nextSeq = 9;
        s.ledger.entries.clear();
        for (uint64_t i = 5; i < 9; ++i) { LedgerEntry e; e.seq = i; s.ledger.entries.push_back(e); }
        Expect(Both(s.early, s.late, s.ledger, "late segment"), WriteStatus::Unknown, WriteReason::LedgerMismatch, "a segment that starts after the captures");
        Scene t = MakeScene(3, 0);
        t.ledger.nextSeq = 2;   // ends before them
        t.ledger.entries.resize(2);
        Expect(Both(t.early, t.late, t.ledger, "short segment"), WriteStatus::Unknown, WriteReason::LedgerMismatch, "a segment that ends before the captures");
    }
    {
        Scene s = MakeScene(3, 0);
        s.ledger.sessionId = 9;
        Expect(Both(s.early, s.late, s.ledger, "foreign ledger"), WriteStatus::Unknown, WriteReason::LedgerMismatch, "a ledger from another session");
        s = MakeScene(3, 0);
        s.ledger.connectionGeneration = 9;
        Expect(Both(s.early, s.late, s.ledger, "foreign connection"), WriteStatus::Unknown, WriteReason::LedgerMismatch, "or another connection");
    }
    {
        Scene s = MakeScene(3, 0);
        s.ledger.entries[2].seq = 7;   // a hole
        Expect(Both(s.early, s.late, s.ledger, "hole"), WriteStatus::Unknown, WriteReason::LedgerCorrupt, "an entry out of sequence");
        s = MakeScene(3, 0);
        s.ledger.entries.pop_back();
        Expect(Both(s.early, s.late, s.ledger, "short"), WriteStatus::Unknown, WriteReason::LedgerCorrupt, "a missing entry");
    }
    {
        // A state load between the captures replaces the machine state.
        Scene s = MakeScene(3, 0);
        s.late.stateLoads = 1;
        Expect(Both(s.early, s.late, s.ledger, "restore"), WriteStatus::Unknown, WriteReason::StateLoadInInterval, "a state load between the captures");
    }
    {
        // Two different checkpoints that both claim one capture serial: the order is unknowable, and the
        // answer is the same whichever is passed first.
        Scene s = MakeScene(3, 0);
        s.late.captureSerial = s.early.captureSerial;
        s.late.serverPokesApplied += 1;
        Expect(Both(s.early, s.late, s.ledger, "same serial"), WriteStatus::Unknown, WriteReason::OrderInconsistent, "different checkpoints with one serial");
        s = MakeScene(3, 0);
        s.late.captureSerial = s.early.captureSerial;
        s.late.ledgerPosition = 4;
        Expect(Both(s.early, s.late, s.ledger, "same serial, other position"), WriteStatus::Unknown, WriteReason::OrderInconsistent,
               "even when one position is later");
    }
    {
        // Capture order and ledger position contradict each other.
        Scene s = MakeScene(3, 0);
        s.early.ledgerPosition = 4;
        s.late.ledgerPosition = 3;
        Expect(Both(s.early, s.late, s.ledger, "inverted"), WriteStatus::Unknown, WriteReason::OrderInconsistent, "the later capture is at an earlier position");
        s = MakeScene(3, 0);
        s.early.captureSerial = 0;
        Expect(Both(s.early, s.late, s.ledger, "no serial"), WriteStatus::Unknown, WriteReason::OrderInconsistent, "a capture with no serial");
    }
}

// ---- server counters --------------------------------------------------------------------------

void TestServerCounters()
{
    {
        // The server applied pokes that the ledger does not hold: another client's writes. No edit in
        // SE's ledger, and still not "none".
        Scene s = MakeScene(3, 0);
        s.late.serverPokesApplied = s.early.serverPokesApplied + 1;
        Expect(Both(s.early, s.late, s.ledger, "applied rose"), WriteStatus::Unknown, WriteReason::UnexplainedServerWrites,
               "applied rose with no ledger entry to explain it");
    }
    {
        Scene s = MakeScene(3, 0);
        s.late.serverPokesDropped = s.early.serverPokesDropped + 1;
        Expect(Both(s.early, s.late, s.ledger, "dropped rose"), WriteStatus::Unknown, WriteReason::UnexplainedServerWrites,
               "dropped rose with no ledger entry to explain it");
    }
    {
        // A counter going backwards is a server restart (it resets at server start) or a wrap.
        Scene s = MakeScene(3, 0);
        s.late.serverPokesApplied = s.early.serverPokesApplied - 1;
        Expect(Both(s.early, s.late, s.ledger, "applied fell"), WriteStatus::Unknown, WriteReason::ServerCounterReset, "applied went backwards");
        s = MakeScene(3, 0);
        s.late.serverPokesDropped = 0;
        Expect(Both(s.early, s.late, s.ledger, "dropped fell"), WriteStatus::Unknown, WriteReason::ServerCounterReset, "dropped went backwards");
    }
    {
        // Wrap at 2^32: the later value is smaller, which is indistinguishable from a reset.
        Scene s = MakeScene(3, 0);
        s.early.serverPokesApplied = 0xFFFFFFFFu;
        s.late.serverPokesApplied = 2;
        Expect(Both(s.early, s.late, s.ledger, "wrap"), WriteStatus::Unknown, WriteReason::ServerCounterReset, "a u32 wrap is treated as a reset");
    }
    {
        // When the ledger does hold the writes, the answer is detected -- the counters are only a
        // cross-check, and a rise the ledger explains is not a gap.
        Scene s = MakeScene(3, 2);
        s.late.serverPokesApplied = s.early.serverPokesApplied + 2;
        Expect(Both(s.early, s.late, s.ledger, "explained"), WriteStatus::Detected, WriteReason::AcceptedWritesInInterval, "a rise the ledger explains");
    }
    {
        // Flat counters with a clean ledger are the none_detected case.
        Scene s = MakeScene(3, 0);
        Expect(Both(s.early, s.late, s.ledger, "flat"), WriteStatus::NoneDetected, WriteReason::CompleteTracking, "flat counters");
    }
}

// ---- A/B symmetry -----------------------------------------------------------------------------

// The same result whichever capture is marker A, over the whole matrix of small perturbations. 'Both'
// already enforces it on every case above; this sweeps combinations they do not name.
void TestSymmetrySweep()
{
    int cases = 0;
    for (int pending = 0; pending < 2; ++pending)
    for (int unconfirmed = 0; unconfirmed < 2; ++unconfirmed)
    for (int between = 0; between < 3; ++between)
    for (int valid = 0; valid < 2; ++valid)
    for (int session = 0; session < 2; ++session)
    for (int counters = 0; counters < 4; ++counters)
    for (int loads = 0; loads < 2; ++loads)
    for (int incomplete = 0; incomplete < 2; ++incomplete)
    {
        Scene s = MakeScene(3, between);
        s.early.pendingWrites = pending;
        s.late.unconfirmedWrites = unconfirmed;
        s.late.valid = valid != 0;
        s.late.sessionId = session ? 2 : 1;
        s.late.serverPokesApplied += counters == 1 ? 1 : 0;
        s.late.serverPokesApplied -= counters == 2 ? 1 : 0;
        if (counters == 3) s.early.serverCounters = ServerCounters::Unsupported;
        s.late.stateLoads = loads;
        s.ledger.complete = !incomplete;
        Both(s.early, s.late, s.ledger, "sweep");
        ++cases;
    }
    Check(cases == 2 * 2 * 3 * 2 * 2 * 4 * 2 * 2, "the sweep ran every combination");

    // Frame numbers are not an order: captures are ordered by serial. Two checkpoints given in either
    // order, with the LATER serial first, must still give the same answer.
    Scene s = MakeScene(3, 1);
    const IntervalResult a = ClassifyWriteInterval(s.early, s.late, s.ledger);
    const IntervalResult b = ClassifyWriteInterval(s.late, s.early, s.ledger);
    Check(a.status == b.status && a.reason == b.reason && a.acceptedWrites == b.acceptedWrites, "argument order does not matter");
}

// ---- A single source: "since it was loaded or connected" --------------------------------------

void TestSinceSourceStart()
{
    auto scene = [](uint64_t entries) {
        Scene s;
        s.ledger = MakeLedger(entries);
        s.ledger.beginsAtSourceStart = true;
        s.ledger.origin = MakeCheckpoint(0, 0);
        s.ledger.origin.captureSerial = 1;
        s.late = MakeCheckpoint(2, entries);
        return s;
    };
    {
        Scene s = scene(0);
        Expect(ClassifySinceSourceStart(s.late, s.ledger), WriteStatus::NoneDetected, WriteReason::CompleteTracking, "never edited since loaded");
    }
    {
        Scene s = scene(2);
        const IntervalResult r = ClassifySinceSourceStart(s.late, s.ledger);
        Expect(r, WriteStatus::Detected, WriteReason::AcceptedWritesInInterval, "edited since loaded");
        Check(r.acceptedWrites == 2, "two writes since the start");
    }
    {
        Scene s = scene(0);
        s.ledger.beginsAtSourceStart = false;
        Expect(ClassifySinceSourceStart(s.late, s.ledger), WriteStatus::Unknown, WriteReason::LedgerIncomplete, "a segment that began mid-session");
    }
    {
        Scene s = scene(0);
        s.late.serverPokesApplied += 1;   // someone else poked since this connection began
        Expect(ClassifySinceSourceStart(s.late, s.ledger), WriteStatus::Unknown, WriteReason::UnexplainedServerWrites, "another client's pokes since connect");
    }
    {
        Scene s = scene(0);
        s.late.valid = false;
        Expect(ClassifySinceSourceStart(s.late, s.ledger), WriteStatus::Unknown, WriteReason::NoTracking, "no tracking");
    }
    {
        Scene s = scene(0);
        s.late.pendingWrites = 1;
        Expect(ClassifySinceSourceStart(s.late, s.ledger), WriteStatus::Detected, WriteReason::PendingWrites, "a pending write");
    }
    {
        Scene s = scene(0);
        s.late.connectionGeneration = 2;   // reconnected after the segment began
        Expect(ClassifySinceSourceStart(s.late, s.ledger), WriteStatus::Unknown, WriteReason::DifferentConnection, "reconnected since");
    }
}

// ---- Outcomes are reported beside the status --------------------------------------------------

void TestOutcomes()
{
    LedgerSegment l = MakeLedger(6);
    l.entries[0].outcome = WriteOutcome::Applied;
    l.entries[1].outcome = WriteOutcome::Dropped;
    l.entries[2].outcome = WriteOutcome::Unconfirmed;
    l.entries[3].outcome = WriteOutcome::Lost;
    l.entries[4].outcome = WriteOutcome::Pending;
    l.entries[5].outcome = WriteOutcome::Applied;
    OutcomeCounts c = CountOutcomes(l, 0, 6);
    Check(c.applied == 2 && c.dropped == 1 && c.unconfirmed == 1 && c.lost == 1 && c.pending == 1 && c.Total() == 6, "all outcomes counted");
    c = CountOutcomes(l, 1, 4);
    Check(c.applied == 0 && c.dropped == 1 && c.unconfirmed == 1 && c.lost == 1 && c.Total() == 3, "a sub-interval");
    Check(CountOutcomes(l, 6, 9).Total() == 0, "nothing past the end");
    Check(std::string(WriteStatusName(WriteStatus::Detected)) == "detected" &&
              std::string(WriteStatusName(WriteStatus::NoneDetected)) == "none_detected" &&
              std::string(WriteStatusName(WriteStatus::Unknown)) == "unknown", "status names are the plan's");
}
}  // namespace

int main()
{
    TestDetected();
    TestNoneDetected();
    TestUnknown();
    TestServerCounters();
    TestSymmetrySweep();
    TestSinceSourceStart();
    TestOutcomes();
    if (gFail == 0) std::printf("WriteIntervalTests: all passed\n");
    return gFail == 0 ? 0 : 1;
}
