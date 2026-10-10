// MemoryCompare -- the logic behind Frame Memory Compare: an immutable snapshot of every captured
// memory region at one frame, and a byte-exact diff between two of them. No ImGui, no App, no
// emulator: it reads through IMemoryBackend and works on plain byte vectors, so it is unit-tested
// like MemorySearch. Design and rationale: Docs/MemoryCompare/SnapshotAndDiffEngine.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Debug/MemoryBackend.h"

namespace sfe
{

// ---- Regions -----------------------------------------------------------------------------------

// Order is the order of SaturnRegions() (SaturnRegions.h); a test pins the two together, so a
// region added there cannot silently lack traits here.
enum class RegionId : uint8_t
{
    Lwram, Hwram, SoundRam, Vdp1Ram, Vdp1Fb, Vdp1Regs, Vdp2Ram, Vdp2Cram, Vdp2Regs,
    Count
};
constexpr size_t kRegionCount = static_cast<size_t>(RegionId::Count);

// Whether a region takes part in a comparison. Sound RAM does not: the 68K sound CPU and the SCSP
// rewrite it constantly (samples, streaming buffers, voice state), so it differed in nearly every
// pair of frames and buried the changes the user was looking for. An excluded region keeps its
// RegionId and traits -- an address still resolves to it -- but a snapshot holds no bytes for it, a
// diff reports nothing for it, and neither the panel, the CSV nor the external diff folders list it.
inline bool InCompare(RegionId id) { return id != RegionId::SoundRam; }

// The captured regions are not all the same kind of thing, and the difference decides what the UI
// may offer: a watchpoint can never fire on the VDP1 frame buffer, which VDP1 drawing writes.
enum class AddressSpace : uint8_t
{
    Sh2Bus,         // canonical 27-bit bus address (cache/through mirrors folded)
    DeviceImage,    // app-derived image (VDP1 FB): offset-addressed, has no bus writer
    RegisterImage   // 16-bit register image as big-endian bytes, not bus bytes
};

enum RegionCaps : uint8_t
{
    kCapNavigate   = 1,   // the Memory tab can GoTo it
    kCapWatch      = 2,   // readable through IMemoryBackend, so a Watch can resolve it
    kCapBreakWrite = 4,   // an SH-2 / SCU-DMA write watchpoint can observe a write here
    kCapSh2Code    = 8    // may hold SH-2 code: View in Assembly is meaningful
};

struct RegionTraits
{
    RegionId     id;
    const char*  name;
    AddressSpace space;
    uint32_t     busBase;    // the Memory tab's address of offset 0
    uint32_t     size;
    uint8_t      caps;
};

const RegionTraits& Traits(RegionId id);

// Bytes a snapshot holds for 'id': the region's size, or none for one left out of comparisons.
inline uint32_t SnapshotSize(RegionId id) { return InCompare(id) ? Traits(id).size : 0; }

// Whether offsets in the region correspond to a bus address worth showing or navigating by. The
// VDP1 frame buffer is an app-derived image, so it has none.
inline bool HasBusAddress(const RegionTraits& t) { return t.space != AddressSpace::DeviceImage; }

// A location inside a region. Rows, selection and every action carry one of these instead of a
// bare address, which cannot say whether it names bus memory, an image or a register.
struct RegionRef
{
    RegionId id = RegionId::Lwram;
    uint32_t offset = 0;
};

inline bool operator==(const RegionRef& a, const RegionRef& b)
{ return a.id == b.id && a.offset == b.offset; }

uint32_t BusAddress(const RegionRef& r);                // busBase + offset
bool     Resolve(uint32_t busAddress, RegionRef* out);  // folds mirrors; false when unmapped

// ---- Snapshots ---------------------------------------------------------------------------------

// What a snapshot is *of*. Diff refuses two snapshots from different sessions, so unrelated
// captures cannot be combined however the caller got hold of them.
struct SnapshotOrigin
{
    uint64_t sessionId = 0;      // one emulator run / loaded source
    uint64_t timelineEpoch = 0;  // one rewind timeline within that session
    uint64_t frameNo = 0;
    bool     liveHead = false;   // captured at the live head rather than from a rewind frame
};

struct MemRegionImage
{
    RegionId             id = RegionId::Lwram;
    std::vector<uint8_t> bytes;   // exactly SnapshotSize(id), big-endian Saturn bytes
};

// Immutable once built: share it as shared_ptr<const MemSnapshot>. A deep copy -- nothing in it
// points at the rewind ring, the recorder's scratch or a context.
struct MemSnapshot
{
    SnapshotOrigin              origin;
    std::vector<MemRegionImage> regions;   // all kRegionCount, indexed by RegionId
};

// Values read from the context just before and after a capture. A capture whose before and after
// differ read memory that moved underneath it, and is discarded.
struct CaptureGuard
{
    uint64_t derivedSerial = 0;   // se_derive_serial(ctx)
    uint64_t sourceId = 0;        // IMemoryBackend::SourceId()
};

// Read every region through 'backend'. Null on any failure, with the reason in *error: a snapshot
// is complete or absent, never partial, and there is no "invalid snapshot" to display. 'guard' may
// be empty (no check). A frame number of 0 is rejected: it means the caller could not say which
// frame the bytes are from.
std::shared_ptr<const MemSnapshot> CaptureSnapshot(
    IMemoryBackend& backend, const SnapshotOrigin& origin,
    const std::function<CaptureGuard()>& guard, std::string* error);

// ---- Diff --------------------------------------------------------------------------------------

struct DiffOptions
{
    uint32_t mergeGap = 4;       // runs separated by fewer identical bytes merge (0 and 1: contiguous only)
    uint32_t contextRows = 2;    // identical rows kept either side of a change
    bool     changesOnly = true;
};

struct RegionDiff
{
    uint32_t changedBytes = 0;   // differing bytes
    uint32_t rangeCount = 0;     // runs of them, after merging runs closer than DiffOptions::mergeGap
};

struct DiffResult
{
    std::shared_ptr<const MemSnapshot> a, b;   // the exact data every later scan reads
    std::vector<RegionDiff>            regions;   // indexed by RegionId
    uint64_t TotalChangedBytes() const;
};

enum class DiffStatus { Ok, NullSnapshot, SessionMismatch, RegionMismatch };

DiffStatus Diff(const std::shared_ptr<const MemSnapshot>& a,
                const std::shared_ptr<const MemSnapshot>& b,
                const DiffOptions& opts, DiffResult* out);

// ---- Complete outputs --------------------------------------------------------------------------
// These scan the snapshots, so every changed byte is covered however many there are.

constexpr uint32_t kDiffRowBytes = 16;   // bytes per row of the hex diff

struct DiffRow
{
    RegionRef ref;             // kDiffRowBytes-aligned start of the row within the region
    uint16_t  validMask = 0;   // bytes of the row that lie inside the region
    uint16_t  changedMask = 0; // subset of validMask that differs
    bool      isContext = false;   // shown only as context around a change
    bool      gapBefore = false;   // identical rows were elided immediately before this one
};

std::vector<DiffRow> BuildRows(const DiffResult& diff, RegionId region, const DiffOptions& opts);

struct CsvSink
{
    virtual ~CsvSink() = default;
    virtual bool Write(const char* data, size_t len) = 0;   // false: stop
};

// Collects the CSV in memory (an export goes out through one SaveFile call).
struct StringCsvSink : CsvSink
{
    std::string text;
    bool Write(const char* data, size_t len) override { text.append(data, len); return true; }
};

enum class CsvResult { Ok, Cancelled, IntegrityError };

// One line per changed byte: region,offset,bus_address,old,new, preceded by a header naming both
// frames. 'only' null means every region. A worst case (every byte differs) is millions of lines, so
// the export is a job that runs in slices: a caller that must stay responsive calls Step() with a line
// budget each UI frame and can drop the job at any point. Counts per region are checked against the
// diff, so a short file is an IntegrityError, never a silent success.
class CsvExport
{
public:
    CsvExport(const DiffResult& diff, const RegionId* only);

    // Appends up to maxLines more lines (0: no limit) to the sink. Returns true once the export has
    // finished, successfully or not; Result() then says which.
    bool Step(CsvSink& sink, uint32_t maxLines);

    CsvResult Result() const { return mResult; }
    uint64_t  TotalLines() const { return mTotalLines; }
    uint64_t  LinesWritten() const { return mLines; }
    uint64_t  TotalBytes() const { return mTotalBytes; }   // the exact size of the finished CSV
    float     Progress() const { return mTotalLines ? static_cast<float>(static_cast<double>(mLines) / static_cast<double>(mTotalLines)) : 1.0f; }

private:
    DiffResult mDiff;           // keeps both snapshots alive however the shown comparison changes
    bool       mHasOnly = false;
    RegionId   mOnly = RegionId::Lwram;
    size_t     mRegion = 0;     // where the next step resumes: region...
    size_t     mOffset = 0;     // ...and byte offset in it
    uint64_t   mRegionLines = 0, mLines = 0, mTotalLines = 0, mTotalBytes = 0;
    bool       mHeaderWritten = false, mFinished = false;
    CsvResult  mResult = CsvResult::Ok;
};

// The whole export in one call (tests, small diffs).
CsvResult WriteCsv(const DiffResult& diff, const RegionId* only, CsvSink& sink);

// The file name an export of 'only' (null: every region) is saved under.
std::string CsvFileName(const DiffResult& diff, const RegionId* only);

}  // namespace sfe
