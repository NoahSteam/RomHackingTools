// WriteInterval -- whether a debugger write could account for a difference between two captures of the
// same source (PLAN.md §5.1), as a pure function over data.
//
// Why it is about an interval and not a snapshot: the Hex Editor changes the displayed snapshot the
// moment an edit is accepted, before the emulator has confirmed it (Core/src/Context.h WriteVram), so
// the server's applied/dropped counters cannot describe an edited snapshot. "Was this snapshot edited?"
// has no stable answer -- the same snapshot compared against a different partner changes its status --
// so the question is asked of the comparison: between capture E and capture L, could SE's own writes
// have changed the bytes?
//
// Inputs: one WriteCheckpoint per capture, and the segment of SE's debugger-write ledger that covers
// them. The ledger lists every write SE ACCEPTED (Hex Editor writes, pokes, LST edits) in order, with
// its later outcome. Building the real ledger and checkpoints is M1; this file is the contract and the
// rules, and M1's recorder has to satisfy exactly these inputs.
//
// Result, evaluated in CAPTURE order (the earlier capture first), so it is the same whichever capture
// the user marked A:
//
//   detected       SE accepted at least one write between the captures, or either capture still held a
//                  pending or unconfirmed write. Positive evidence wins over any gap in the tracking:
//                  a write is a write however poorly the rest was observed.
//   none_detected  both checkpoints lie in one complete, ordered ledger segment (same session, timeline
//                  epoch and connection generation), nothing was pending or unconfirmed at either,
//                  no state load happened in between, and the server's applied/dropped counters did
//                  not move. A counter increase that the ledger does not explain -- another client's
//                  pokes, say -- is a tracking gap and prevents this answer even though the ledger holds
//                  no edit; so does a counter that went backwards (a reset or a wrap).
//   unknown        anything else: a capture with no tracking, captures from different sessions, epochs
//                  or connections (a reconnect), a server too old to report counters, history SE did
//                  not observe, or inputs that contradict each other.
//
// Application outcome (applied / dropped / unconfirmed) is NOT part of the status; CountOutcomes()
// reports it beside it.
//
// No ImGui, no platform headers, no I/O.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Debug/MemoryCompare.h"   // RegionId

namespace sfe
{
namespace ai
{

// What the checkpoint knows about the live server's pokes_applied / pokes_dropped (control block v23+).
enum class ServerCounters : uint8_t
{
    NotApplicable,   // the source has no live server (a savestate or dump): edits are snapshot-local
    Unsupported,     // a live server older than v23: it reports no counters, so its writes cannot be ruled out
    Known,           // the cached values below are the server's, as of the capture
};

// The state of write tracking at the moment of one capture. Taken inside the capture boundary, so it
// describes the same instant as the bytes.
struct WriteCheckpoint
{
    bool valid = false;               // false: the capture recorded no tracking at all
    uint64_t captureSerial = 0;       // order in which this process took captures; never 0 when valid
    uint64_t sessionId = 0;
    uint64_t timelineEpoch = 0;
    uint64_t connectionGeneration = 0;

    // Position in the ledger: the sequence number the next accepted write will get, i.e. the count of
    // writes accepted so far in this ledger. Two checkpoints of one ledger with different positions
    // are separated by at least one accepted write.
    uint64_t ledgerPosition = 0;

    // Accepted writes the emulator has not confirmed applied (queued, or in flight) at capture time, and
    // those whose outcome is unknown (the connection failed before the reply). Either makes the
    // captured bytes differ from the emulator's, or may.
    uint32_t pendingWrites = 0;
    uint32_t unconfirmedWrites = 0;

    ServerCounters serverCounters = ServerCounters::NotApplicable;
    uint32_t serverPokesApplied = 0;   // the server's own, u32 as on the wire, as last reported
    uint32_t serverPokesDropped = 0;

    // State loads (LST / ELS), applied or refused, accepted by this source so far.
    uint64_t stateLoads = 0;
};

enum class WriteOutcome : uint8_t
{
    Pending,       // accepted, not yet reported applied or dropped
    Applied,
    Dropped,       // the server counted it dropped
    Unconfirmed,   // sent, and the connection failed before the answer
    Lost,          // accepted by the UI and never sent
};

struct LedgerEntry
{
    uint64_t seq = 0;       // 0-based position in the ledger
    RegionId region = RegionId::Lwram;
    uint32_t offset = 0;
    uint32_t length = 0;
    WriteOutcome outcome = WriteOutcome::Pending;
};

// A contiguous stretch of the ledger, with the identity it was recorded under.
struct LedgerSegment
{
    uint64_t sessionId = 0;
    uint64_t timelineEpoch = 0;
    uint64_t connectionGeneration = 0;

    uint64_t firstSeq = 0;   // the segment covers sequence numbers [firstSeq, nextSeq)
    uint64_t nextSeq = 0;
    // True when every write accepted in [firstSeq, nextSeq) is in 'entries': nothing was dropped from
    // the record, and SE was observing throughout.
    bool complete = false;
    std::vector<LedgerEntry> entries;   // ascending seq, contiguous from firstSeq

    // For "since this source was loaded or connected": the segment began at that moment, and 'origin'
    // is the tracking state then (counters and state loads at the start).
    bool beginsAtSourceStart = false;
    WriteCheckpoint origin;
};

enum class WriteStatus : uint8_t { Detected, NoneDetected, Unknown };
const char* WriteStatusName(WriteStatus status);   // "detected", "none_detected", "unknown"

// Why the status is what it is. Machine-readable; the message in IntervalResult is for people.
enum class WriteReason : uint8_t
{
    // detected
    AcceptedWritesInInterval,
    PendingWrites,
    UnconfirmedWrites,
    // none_detected
    CompleteTracking,
    // unknown
    NoTracking,              // a capture recorded no tracking
    OrderInconsistent,       // capture order and ledger position disagree
    DifferentSession,
    DifferentEpoch,
    DifferentConnection,     // includes a reconnect between the captures
    LedgerMismatch,          // the segment is not the one the captures lie in, or does not cover them
    LedgerIncomplete,
    LedgerCorrupt,           // entries out of order or with a hole
    StateLoadInInterval,
    ServerCountersUnsupported,
    ServerCountersInconsistent,   // one capture saw a server, the other did not
    ServerCounterReset,      // a counter went backwards
    UnexplainedServerWrites, // a counter rose and the ledger holds no write to explain it
};
const char* WriteReasonName(WriteReason reason);

struct IntervalResult
{
    WriteStatus status = WriteStatus::Unknown;
    WriteReason reason = WriteReason::NoTracking;
    uint64_t acceptedWrites = 0;   // by ledger position, when known; 0 otherwise
    std::string message;
};

// The interval between two captures of one source, whichever is marker A. Captures are ordered by
// captureSerial (frame numbers are not an order: a rewind can make the later capture the earlier
// frame); two checkpoints with the same serial are the same capture.
IntervalResult ClassifyWriteInterval(const WriteCheckpoint& x, const WriteCheckpoint& y, const LedgerSegment& ledger);

// A single-source package: "since this source was loaded or connected". Needs a segment that
// begins at the source's start; otherwise the history before the segment was not observed.
IntervalResult ClassifySinceSourceStart(const WriteCheckpoint& capture, const LedgerSegment& ledger);

// Outcomes of the writes accepted in [fromSeq, toSeq) that the ledger holds. Reported beside the
// status, never folded into it.
struct OutcomeCounts
{
    uint32_t pending = 0, applied = 0, dropped = 0, unconfirmed = 0, lost = 0;
    uint32_t Total() const { return pending + applied + dropped + unconfirmed + lost; }
};
OutcomeCounts CountOutcomes(const LedgerSegment& ledger, uint64_t fromSeq, uint64_t toSeq);

}  // namespace ai
}  // namespace sfe
