// EvidenceCapture -- the owned, immutable record of one bound source (PLAN.md §5.1).
//
// Evidence is never read from the current App context or from the driver's newest snapshot: at
// Generate, SE acquires one capture per bound source and every collector reads only from it, so nothing
// the user does afterwards (stepping, resuming, editing) can reach the package. This header is the
// contract those captures satisfy. It is types and validation only; acquiring one -- under a single
// begin_capture / end_capture boundary, with the identity guard checked before and after -- is M1.
//
// What a capture holds:
//   * its SourceKey, and for memory sources a write-tracking checkpoint (WriteInterval.h);
//   * memory: the bytes of each region its key names, and their hashes ARE the key's content hashes;
//   * both SH-2 register files with validity, the stop that halted it (reason, CPU, PC, stop_seq) and
//     the call stacks with confidence;
//   * for file kinds, the bytes of the selected ranges.
//
// Anything the capture could not take is UNAVAILABLE with a reason (the checklist shows it, §3.2). It is
// never filled from the live head or from a later frame: a collector asking a capture for registers
// it does not hold gets "unavailable", not the current registers.
//
// No ImGui, no platform headers.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "AIInvestigation/SourceKey.h"
#include "AIInvestigation/WriteInterval.h"
#include "Debug/MemoryCompare.h"

namespace sfe
{
namespace ai
{

struct Sh2RegisterFile
{
    bool valid = false;   // this CPU's registers were read; a CPU the emulator did not report is invalid
    uint32_t r[16] = {};
    uint32_t sr = 0, gbr = 0, vbr = 0, mach = 0, macl = 0, pr = 0, pc = 0;
};

struct CaptureRegisters
{
    bool available = false;
    std::string reason;          // why not, when !available
    Sh2RegisterFile cpu[2];      // [0] master, [1] slave
};

struct CaptureStop
{
    bool available = false;
    std::string reason;
    uint32_t stopReason = 0;     // SE_LIVE_STOP_*: 0 none/user, 1 execution breakpoint, 2 step, 3 DMA watch
    uint32_t cpu = 0;
    uint32_t pc = 0;
    uint32_t stopSeq = 0;
};

enum class FrameConfidence : uint8_t { Confirmed, Probable, Heuristic };

struct CaptureCallFrame
{
    uint32_t callSite = 0, function = 0, returnAddress = 0, stackPointer = 0;
    FrameConfidence confidence = FrameConfidence::Heuristic;
};

struct CaptureCallStacks
{
    bool available = false;
    std::string reason;
    std::vector<CaptureCallFrame> cpu[2];   // innermost first
};

using ByteBlob = std::shared_ptr<const std::vector<uint8_t>>;

// The pieces of evidence a collector can ask a capture for.
enum class CaptureItem : uint8_t { Memory, Registers, Stop, CallStacks, WriteTracking, FileBytes };

struct ItemAvailability
{
    bool available = false;
    std::string reason;
};

class EvidenceCapture
{
public:
    virtual ~EvidenceCapture() = default;

    virtual const SourceKey& Key() const = 0;
    virtual const WriteCheckpoint& Tracking() const = 0;
    // The bytes of 'region', or null when this capture holds none. Never reads anything live.
    virtual const std::vector<uint8_t>* RegionBytes(RegionId region) const = 0;
    virtual const std::vector<uint8_t>* FileBytes() const = 0;
    virtual const CaptureRegisters& Registers() const = 0;
    virtual const CaptureStop& Stop() const = 0;
    virtual const CaptureCallStacks& CallStacks() const = 0;
};

// Whether 'item' can be supplied, and why not. This is what the checklist's Unavailable state shows.
ItemAvailability Availability(const EvidenceCapture& capture, CaptureItem item);

// Everything an OwnedCapture is built from.
struct CaptureData
{
    SourceKey key;
    WriteCheckpoint tracking;
    ByteBlob regions[kRegionCount];   // null: not captured
    ByteBlob file;
    CaptureRegisters registers;
    CaptureStop stop;
    CaptureCallStacks callStacks;
};

// A capture that owns its bytes and cannot change after construction. Cheap to share by pointer.
class OwnedCapture final : public EvidenceCapture
{
public:
    explicit OwnedCapture(CaptureData data) : mData(std::move(data)) {}

    const SourceKey& Key() const override { return mData.key; }
    const WriteCheckpoint& Tracking() const override { return mData.tracking; }
    const std::vector<uint8_t>* RegionBytes(RegionId region) const override { return mData.regions[static_cast<size_t>(region)].get(); }
    const std::vector<uint8_t>* FileBytes() const override { return mData.file.get(); }
    const CaptureRegisters& Registers() const override { return mData.registers; }
    const CaptureStop& Stop() const override { return mData.stop; }
    const CaptureCallStacks& CallStacks() const override { return mData.callStacks; }

private:
    const CaptureData mData;
};

// Empty when the capture is internally consistent, else the first problem:
//   * the SourceKey is valid for its kind;
//   * every region the key hashes is present, is exactly the region's size, and hashes to the key's
//     value -- and no region is present that the key does not name -- so the key really identifies
//     the bytes; a file capture's bytes hash to its "file" entry;
//   * an unavailable item says why and carries no data, an available one carries data;
//   * a file capture has no registers, stop, call stacks or write tracking;
//   * a memory capture's tracking, when valid, agrees with the key on session, epoch and connection.
std::string ValidateCapture(const EvidenceCapture& capture);

// The values the identity guard compares before and after acquisition, as CaptureSnapshot does with
// {derive serial, source id}: any difference means memory moved underneath the capture and it is
// rejected. Frame and connection generation are included for live sources.
struct CaptureGuardSample
{
    uint64_t sourceId = 0;
    uint64_t deriveSerial = 0;
    uint64_t connectionGeneration = 0;
    uint64_t frame = 0;
};
bool GuardUnchanged(const CaptureGuardSample& before, const CaptureGuardSample& after);

}  // namespace ai
}  // namespace sfe
