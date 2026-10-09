# Snapshot and diff engine

`FrontEnd/src/Debug/MemoryCompare.{h,cpp}`. Pure C++14-compatible logic: no ImGui, no `App`, no
emulator. It sits beside `MemorySearch` and `AccessLog` in `Debug/` and follows their pattern.
Part of [Frame Memory Compare](README.md).

## Responsibilities

1. Describe each captured region unambiguously (id, address space, capabilities).
2. Hold an immutable copy of memory at one frame, with the identity needed to refuse stale or
   unrelated comparisons (`MemSnapshot`).
3. Capture a snapshot that is provably from one frame of one session.
4. Diff two snapshots into per-region summaries.
5. Produce *complete* row lists and CSV output by scanning the snapshots (never from the summaries).

## Region identity and addressing

`SaturnRegions()` (`SaturnRegions.h`) gives a name, a CPU-visible base and a size. That is not
enough to drive navigation, watches and breakpoints, because the regions are not all the same kind
of thing:

- **VDP1 FB** is the app's reconstructed *drawn output* image (`SE_VRAM_KIND_VDP1_FB`), written by
  VDP1 drawing, not by bus writes. It has an address in the Memory tab, but no bus writer.
- **VDP1/VDP2 Regs** are 16-bit register *images* assembled from `se_get_vdpN_register`
  (`ContextBackend::ReadOne`), not bus bytes. Write-only registers read back their stored value,
  which a bus read would not return. Their sizes (0x18 and 0x120 bytes) are not multiples of 16.
- **Sound RAM** is visible to the SH-2 at `0x05A00000`, but its writer is the 68K. The 68K sees
  the same bytes at offset 0.

So every region carries an explicit id, space and capability set, and every location is a
`RegionRef`, never a bare `uint32_t`:

```cpp
enum class RegionId : uint8_t {          // order == SaturnRegions(); static_assert on the count
    Lwram, Hwram, SoundRam, Vdp1Ram, Vdp1Fb, Vdp1Regs, Vdp2Ram, Vdp2Cram, Vdp2Regs, Count };

enum class AddressSpace : uint8_t {
    Sh2Bus,          // canonical 27-bit bus address (cache/through mirrors folded)
    DeviceImage,     // app-derived image (VDP1 FB): offset-addressed, has no bus writer
    RegisterImage    // 16-bit register image, big-endian bytes
};

enum RegionCaps : uint8_t {
    kNavigate   = 1,   // Memory tab GoTo is valid
    kWatch      = 2,   // readable through IMemoryBackend, so a Watch can resolve it
    kBreakWrite = 4,   // an SH-2 / SCU-DMA write watchpoint can observe a write here
    kSh2Code    = 8    // may hold SH-2 code: View in Assembly is meaningful
};

struct RegionTraits { RegionId id; const char* name; AddressSpace space;
                      uint32_t busBase; uint32_t size; uint8_t caps; };

struct RegionRef { RegionId id; uint32_t offset; };   // the key for rows, selection and export
uint32_t BusAddress(RegionRef);                        // busBase + offset (display, GoTo, Watch)
bool     Resolve(uint32_t busAddress, RegionRef* out); // via SaturnRegionIndex()
```

The capability table is the single source for what the panel's context menu offers; see
[ComparePanel.md](ComparePanel.md). It is derived from `SaturnRegions()` by index, with the
`static_assert`, so a region added there cannot silently lack traits.

## Snapshot and identity

```cpp
struct MemRegionImage { RegionId id; std::vector<uint8_t> bytes; };   // exactly traits.size

struct SnapshotOrigin {
    uint64_t sessionId;      // which emulator run / loaded source (see below)
    uint64_t timelineEpoch;  // which rewind timeline within that session
    uint64_t frameNo;        // the frame the bytes are from
    bool     liveHead;       // captured at the live head rather than from a rewind frame
};

struct MemSnapshot {         // immutable once built; shared as shared_ptr<const MemSnapshot>
    SnapshotOrigin              origin;
    std::vector<MemRegionImage> regions;
};
```

- `sessionId` is assigned by `App` (see [TimelineMarkers.md](TimelineMarkers.md#identity-lifecycle)).
  `Diff` refuses two snapshots with different `sessionId` and returns an error, so unrelated
  captures can never be combined however the markers got there. Different `timelineEpoch` within
  one session is allowed: both are real memory of the same run, and comparing across an abandoned
  branch is legitimate. The UI labels it.
- Snapshots are deep copies. Nothing in one points at the rewind ring, the recorder's scratch or a
  context.

## Capture consistency

Verified against the code:

1. **Per-region reads are coherent.** `se_begin_frame` copies every region and register into the
   context's `HardwareSnapshot` inside the driver's `begin_capture`/`end_capture` pin
   (`Core/src/HardwareSnapshot.cpp:56-70`; the live driver pins the newest snapshot per thread,
   `Drivers/Live/src/LiveDriver.cpp:1620`). `se_read_vram` and `se_get_vdpN_register` then read that
   immutable copy (`Core/src/Context.h:458`, `:378`). Reading nine regions through `IMemoryBackend`
   therefore cannot straddle two emulator frames, **at the live head too**, as long as nothing
   re-derives the context between the reads. The live driver publishing a newer snapshot on its
   poll thread does not affect an already-begun context.
2. **What can change the context between reads** is `se_begin_frame`, a scrub seek,
   `se_write_vram` and `se_set_vdpN_register` (`se_derive_serial`, `SeHost.h`). Capture runs
   synchronously on the UI thread in one function and calls none of them, but it does not rely on
   that.
3. **Defence in depth.** `CaptureSnapshot` reads `se_derive_serial(ctx)` and
   `ContextBackend::SourceId()` before the first read and again after the last. If either moved,
   the capture is discarded and reported as failed (`"memory changed during capture"`). A future
   change that lets another thread touch the context fails loudly instead of producing a torn
   snapshot.
4. **The frame number must come from the same source as the bytes.**
   - At the live head: `se_frame_number(ctx)` returns the frame the *capture* came from
     (`CapturedFrame`, `Core/src/HostAbi.cpp:582`), not the newest the emulator has reached.
   - On a scrubbed frame: the scrub data source has no `frame_number` callback, so
     `se_frame_number` returns 0 there. The number must come from the recorder.
   - Deriving it afterwards from `FrameNumber(mScrubShownIndex)` is not safe: that is a deque
     *index*, and the ring evicts from the front (the worker on publish, or `Configure`), so the
     index can name a different frame than the one decompressed into the scratch. Add
     `FrameRecorder::SelectedFrameNumber()`, set under `mRingMtx` in `Select()` from the frame it
     actually decompressed (0 when nothing is selected or the last `Select` was refused), and use
     that. Capture also fails when it is 0.
5. **Pending scrub edits.** The scrub source is writable: `CbEditVram`/`CbEditMain` update the
   context's visible copy (`Context::WriteVram`) *and* record a pending poke
   (`FrameRecorder.cpp:318`, `App::RecordPendingEdit`). So the context the backend reads can differ
   from the recorded frame. Rule: **capture is refused while the shown scrub frame has pending
   edits.** The caller checks; the engine just receives bytes. Recorded history, not hypothetical
   edits, is what a frame comparison is about. At the live head there is no pending state: a poke
   is applied to the running emulator and the snapshot is whatever the Memory tab currently shows.

`CaptureSnapshot(IMemoryBackend&, const SnapshotOrigin&, guard, std::string* error)` needs only
`ReadRegionBytes` (inline in `MemoryBackend.h`) plus a `guard` callback returning the serial and
source id, so its tests link neither the core nor an emulator. It returns null on any failure, and a partial region is never
kept (`ReadRegionBytes` clears its output on failure). A snapshot is complete or absent, so there
is no "invalid snapshot" state to display.

Region sizes come from `SaturnRegions()`: about 3.5 MB per snapshot in total.

## Diff

```cpp
struct ChangedRange { uint32_t offset; uint32_t length; };

struct RegionDiff {
    RegionId                  id;
    uint32_t                  changedBytes;     // EXACT
    uint32_t                  rangeCount;       // EXACT count of merged ranges
    std::vector<ChangedRange> ranges;           // SUMMARY ONLY: first kMaxStoredRanges
    bool                      rangesTruncated;  // rangeCount > ranges.size()
};

struct DiffResult {
    std::shared_ptr<const MemSnapshot> a, b;    // the exact data every later scan reads
    std::vector<RegionDiff>            regions;
};

enum class DiffStatus { Ok, SessionMismatch, RegionMismatch };
DiffStatus Diff(a, b, const DiffOptions&, DiffResult* out);
```

- Byte-for-byte over each region, with an 8-byte word fast path that descends to bytes only when a
  word differs.
- Ranges are maximal runs of differing bytes, merged when the gap is below `mergeGap` (default 4).
  `kMaxStoredRanges` is 100,000 per region. Past that, ranges stop being stored but
  `changedBytes` and `rangeCount` stay exact and `rangesTruncated` is set.
- **`ranges` is a summary for display and nothing else.** It feeds the "Changed Ranges" column. No
  other function reads it.

## Complete outputs: rows and CSV rescan the snapshots

`BuildRows` and `WriteCsv` take the two snapshots (via `DiffResult::a`/`b`) and scan the region
bytes directly. They never consume `RegionDiff::ranges`, so the storage cap cannot omit anything.

```cpp
struct DiffRow {
    RegionRef ref;           // 16-byte-aligned row start within the region
    uint16_t  validMask;     // bytes of the row inside the region (a 0x18-byte region ends mid-row)
    uint16_t  changedMask;   // subset of validMask that differs
    bool      isContext;     // shown only as context around a change
    bool      gapBefore;     // identical rows were elided before this one
};
std::vector<DiffRow> BuildRows(const DiffResult&, RegionId, const DiffOptions&);

struct CsvSink { virtual bool Write(const char* data, size_t len) = 0; };   // false: stop/cancel
bool WriteCsv(const DiffResult&, const RegionId* only /*null = all*/, CsvSink&);
```

- `BuildRows` emits every row containing a changed byte, plus `contextRows` either side when
  `changesOnly`; with it off, every row. Adjacent context windows merge; an elided run sets
  `gapBefore`. One pass over at most 1 MiB, so it is cheap enough to run on demand per selected
  region. The worst case (every byte differs) is 65,536 rows for a 1 MiB region.
- `WriteCsv` **streams** through a sink instead of building one string: an all-bytes-differ export
  is millions of lines. One line per changed byte: `region,offset,bus_address,old,new`. Header: both
  frame numbers, both session ids and the timeline epochs. `bus_address` is empty for regions with
  no bus address meaning (`DeviceImage`). It returns false if the sink stops it.
- Integrity check: `WriteCsv` counts the lines it wrote per region and compares with
  `RegionDiff::changedBytes`. A mismatch is a bug, reported as an export failure, never a silent
  short file.
- "All Memory" in the panel is a sequence of per-region sections (`BuildRows` per region, built
  lazily), not one merged list.

## Tests

New `FrontEnd/tests/MemoryCompareTests.cpp`, registered in `CMakeLists.txt` like
`MemorySearchTests`:

- identical snapshots: zero changes, no ranges, no rows in Changes Only mode
- single byte at the first and last byte of a region; change spanning a row boundary
- two changes inside and beyond `mergeGap`
- word fast path: change at each offset 0..7 in a word. Every region size is a multiple of 8, so the
  byte-at-a-time tail path is not reachable through the public API; the 0x18-byte register region
  covers the partial *row* case instead (`validMask`)
- **truncation**: more than `kMaxStoredRanges` isolated changes in one region.
  `rangesTruncated` is set, `rangeCount` and `changedBytes` are exact, and **the union of
  `BuildRows` masks and the CSV lines each equal the true set of differing bytes** (compared against
  a brute-force oracle)
- context windows merge, `gapBefore` appears exactly where rows are elided, windows clip at region
  edges
- `Diff` returns `SessionMismatch` for different `sessionId`, and `Ok` across different
  `timelineEpoch`
- A/B swap mirrors the diff, with old and new swapped in CSV
- capture: a failed read returns null; success copies, and later mutation of the source does not
  change the snapshot; a changed `se_derive_serial` or source id during capture is rejected; a zero
  frame number is rejected
- region table: the `static_assert` and capability flags match the table above (guards against a
  region being added without traits)
- CSV golden output for a small fixture; a sink that cancels stops cleanly
