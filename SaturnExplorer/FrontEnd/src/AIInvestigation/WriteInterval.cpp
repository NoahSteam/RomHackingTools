#include "AIInvestigation/WriteInterval.h"

namespace sfe
{
namespace ai
{
namespace
{

IntervalResult Result(WriteStatus s, WriteReason r, uint64_t accepted, std::string message)
{
    IntervalResult out;
    out.status = s;
    out.reason = r;
    out.acceptedWrites = accepted;
    out.message = std::move(message);
    return out;
}

IntervalResult Unknown(WriteReason r, const char* message) { return Result(WriteStatus::Unknown, r, 0, message); }

// Entries must be ascending and gapless from firstSeq to nextSeq: the proof that "no entry between the
// checkpoints" means no write rather than a write that was never recorded.
bool LedgerIsWellFormed(const LedgerSegment& l)
{
    if (l.nextSeq < l.firstSeq) return false;
    if (l.entries.size() != l.nextSeq - l.firstSeq) return false;
    for (size_t i = 0; i < l.entries.size(); ++i)
        if (l.entries[i].seq != l.firstSeq + i) return false;
    return true;
}

// 'e' is the earlier state and 'l' the later; both already known to be valid trackers (the caller
// supplies the ordering). 'sinceStart' marks 'e' as the segment's origin rather than a capture.
IntervalResult Classify(const WriteCheckpoint& e, const WriteCheckpoint& l, const LedgerSegment& ledger)
{
    // Positive evidence first. It does not depend on the rest of the tracking being sound.
    if (e.pendingWrites || l.pendingWrites)
        return Result(WriteStatus::Detected, WriteReason::PendingWrites, 0,
                      "a capture holds accepted writes the emulator has not confirmed");
    if (e.unconfirmedWrites || l.unconfirmedWrites)
        return Result(WriteStatus::Detected, WriteReason::UnconfirmedWrites, 0,
                      "a capture holds writes whose outcome is unknown");

    if (e.sessionId != l.sessionId) return Unknown(WriteReason::DifferentSession, "the captures are from different sessions");
    if (e.timelineEpoch != l.timelineEpoch) return Unknown(WriteReason::DifferentEpoch, "the captures are from different timeline epochs");
    if (e.connectionGeneration != l.connectionGeneration)
        return Unknown(WriteReason::DifferentConnection, "the captures are from different connections");
    if (ledger.sessionId != l.sessionId || ledger.timelineEpoch != l.timelineEpoch ||
        ledger.connectionGeneration != l.connectionGeneration)
        return Unknown(WriteReason::LedgerMismatch, "the ledger segment is not the one these captures lie in");

    if (e.ledgerPosition > l.ledgerPosition)
        return Unknown(WriteReason::OrderInconsistent, "the later capture is at an earlier ledger position");
    if (l.ledgerPosition > e.ledgerPosition)
        return Result(WriteStatus::Detected, WriteReason::AcceptedWritesInInterval, l.ledgerPosition - e.ledgerPosition,
                      "SE accepted writes between the captures");

    // No accepted write separates them. That is "none detected" only if the record is whole.
    if (!ledger.complete) return Unknown(WriteReason::LedgerIncomplete, "SE did not observe every write in this segment");
    if (ledger.firstSeq > e.ledgerPosition || e.ledgerPosition > ledger.nextSeq || l.ledgerPosition > ledger.nextSeq)
        return Unknown(WriteReason::LedgerMismatch, "the ledger segment does not cover the captures");
    if (!LedgerIsWellFormed(ledger)) return Unknown(WriteReason::LedgerCorrupt, "the ledger segment is out of order or has a gap");
    if (e.stateLoads != l.stateLoads) return Unknown(WriteReason::StateLoadInInterval, "a state load happened between the captures");

    if (e.serverCounters == ServerCounters::Unsupported || l.serverCounters == ServerCounters::Unsupported)
        return Unknown(WriteReason::ServerCountersUnsupported, "the server is too old to report poke counters");
    if (e.serverCounters != l.serverCounters)
        return Unknown(WriteReason::ServerCountersInconsistent, "only one capture saw a live server");
    if (e.serverCounters == ServerCounters::Known)
    {
        if (l.serverPokesApplied < e.serverPokesApplied || l.serverPokesDropped < e.serverPokesDropped)
            return Unknown(WriteReason::ServerCounterReset, "a server poke counter went backwards (reset or wrap)");
        // The ledger holds no write between these captures, so any rise was caused by someone else.
        if (l.serverPokesApplied != e.serverPokesApplied || l.serverPokesDropped != e.serverPokesDropped)
            return Unknown(WriteReason::UnexplainedServerWrites, "the server applied or dropped writes SE did not make");
    }
    return Result(WriteStatus::NoneDetected, WriteReason::CompleteTracking, 0,
                  "complete tracking found no accepted write between the captures");
}

bool Same(const WriteCheckpoint& a, const WriteCheckpoint& b)
{
    return a.valid == b.valid && a.captureSerial == b.captureSerial && a.sessionId == b.sessionId && a.timelineEpoch == b.timelineEpoch &&
           a.connectionGeneration == b.connectionGeneration && a.ledgerPosition == b.ledgerPosition && a.pendingWrites == b.pendingWrites &&
           a.unconfirmedWrites == b.unconfirmedWrites && a.serverCounters == b.serverCounters && a.serverPokesApplied == b.serverPokesApplied &&
           a.serverPokesDropped == b.serverPokesDropped && a.stateLoads == b.stateLoads;
}

}  // namespace

const char* WriteStatusName(WriteStatus s)
{
    switch (s)
    {
    case WriteStatus::Detected: return "detected";
    case WriteStatus::NoneDetected: return "none_detected";
    case WriteStatus::Unknown: return "unknown";
    }
    return "?";
}

const char* WriteReasonName(WriteReason r)
{
    switch (r)
    {
    case WriteReason::AcceptedWritesInInterval: return "accepted_writes_in_interval";
    case WriteReason::PendingWrites: return "pending_writes";
    case WriteReason::UnconfirmedWrites: return "unconfirmed_writes";
    case WriteReason::CompleteTracking: return "complete_tracking";
    case WriteReason::NoTracking: return "no_tracking";
    case WriteReason::OrderInconsistent: return "order_inconsistent";
    case WriteReason::DifferentSession: return "different_session";
    case WriteReason::DifferentEpoch: return "different_epoch";
    case WriteReason::DifferentConnection: return "different_connection";
    case WriteReason::LedgerMismatch: return "ledger_mismatch";
    case WriteReason::LedgerIncomplete: return "ledger_incomplete";
    case WriteReason::LedgerCorrupt: return "ledger_corrupt";
    case WriteReason::StateLoadInInterval: return "state_load_in_interval";
    case WriteReason::ServerCountersUnsupported: return "server_counters_unsupported";
    case WriteReason::ServerCountersInconsistent: return "server_counters_inconsistent";
    case WriteReason::ServerCounterReset: return "server_counter_reset";
    case WriteReason::UnexplainedServerWrites: return "unexplained_server_writes";
    }
    return "?";
}

IntervalResult ClassifyWriteInterval(const WriteCheckpoint& x, const WriteCheckpoint& y, const LedgerSegment& ledger)
{
    // Positive evidence from either capture stands even when the other recorded no tracking at all.
    // Pending is tested across both captures before unconfirmed, so the reason does not depend on
    // which capture was passed first.
    const bool pending = (x.valid && x.pendingWrites) || (y.valid && y.pendingWrites);
    const bool unconfirmed = (x.valid && x.unconfirmedWrites) || (y.valid && y.unconfirmedWrites);
    if (pending)
        return Result(WriteStatus::Detected, WriteReason::PendingWrites, 0,
                      "a capture holds accepted writes the emulator has not confirmed");
    if (unconfirmed)
        return Result(WriteStatus::Detected, WriteReason::UnconfirmedWrites, 0,
                      "a capture holds writes whose outcome is unknown");
    if (!x.valid || !y.valid) return Unknown(WriteReason::NoTracking, "a capture recorded no write tracking");
    // Order by the serial the process gave each capture. The same serial is the same capture, so two
    // checkpoints sharing one must be identical; if they are not, the order is unknowable and the answer
    // must not depend on which was passed first.
    if (x.captureSerial == y.captureSerial && !Same(x, y))
        return Unknown(WriteReason::OrderInconsistent, "two different checkpoints claim the same capture serial");
    const bool xFirst = x.captureSerial <= y.captureSerial;
    const WriteCheckpoint& e = xFirst ? x : y;
    const WriteCheckpoint& l = xFirst ? y : x;
    if (e.captureSerial == 0)
        return Unknown(WriteReason::OrderInconsistent, "a capture has no serial, so the order of the captures is unknown");
    return Classify(e, l, ledger);
}

IntervalResult ClassifySinceSourceStart(const WriteCheckpoint& capture, const LedgerSegment& ledger)
{
    if (!capture.valid) return Unknown(WriteReason::NoTracking, "the capture recorded no write tracking");
    if (!ledger.beginsAtSourceStart || !ledger.origin.valid)
        return Unknown(WriteReason::LedgerIncomplete, "SE did not observe this source from the moment it was loaded or connected");
    if (ledger.origin.ledgerPosition != ledger.firstSeq)
        return Unknown(WriteReason::LedgerCorrupt, "the segment's origin is not at its first sequence number");
    return Classify(ledger.origin, capture, ledger);
}

OutcomeCounts CountOutcomes(const LedgerSegment& ledger, uint64_t fromSeq, uint64_t toSeq)
{
    OutcomeCounts c;
    for (const LedgerEntry& e : ledger.entries)
    {
        if (e.seq < fromSeq || e.seq >= toSeq) continue;
        switch (e.outcome)
        {
        case WriteOutcome::Pending: ++c.pending; break;
        case WriteOutcome::Applied: ++c.applied; break;
        case WriteOutcome::Dropped: ++c.dropped; break;
        case WriteOutcome::Unconfirmed: ++c.unconfirmed; break;
        case WriteOutcome::Lost: ++c.lost; break;
        }
    }
    return c;
}

}  // namespace ai
}  // namespace sfe
