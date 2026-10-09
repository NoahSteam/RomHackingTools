# Snapshot and diff engine

`FrontEnd/src/Debug/MemoryCompare.{h,cpp}`. Pure C++14-compatible logic: no ImGui, no `App`, no
emulator. It sits beside `MemorySearch` and `AccessLog` in `Debug/` and follows their pattern.
Part of [Frame Memory Compare](README.md).

## Responsibilities

1. Hold an immutable copy of memory at one frame (`MemSnapshot`).
2. Diff two snapshots into per-region summaries and changed ranges.
3. Produce the list of display rows the panel renders, honouring Changes Only and context lines.
4. Serialise a diff to CSV.

## Types (the shared contract)

```cpp
struct MemRegionImage {
    std::string          name;     // "HWRAM", from SaturnRegions()
    uint32_t             base;     // CPU-visible base address
    std::vector<uint8_t> bytes;    // big-endian Saturn bytes, exactly region size
};

struct MemSnapshot {               // immutable once built; share via shared_ptr<const>
    uint64_t                    frameNo = 0;
    std::vector<MemRegionImage> regions;
};

struct ChangedRange { uint32_t address; uint32_t length; };

struct RegionDiff {
    size_t                    regionIndex;
    uint32_t                  changedBytes;     // exact, independent of range merging
    std::vector<ChangedRange> ranges;           // merged, ascending
    bool                      rangesTruncated;  // hit kMaxRanges
};

struct DiffOptions {
    uint32_t mergeGap      = 4;       // ranges closer than this are merged
    uint32_t contextRows   = 2;       // rows kept around a change
    bool     changesOnly   = true;
};

struct DiffRow {
    uint32_t address;        // 16-byte aligned
    uint16_t changedMask;    // bit i set: byte i differs
    bool     isContext;      // shown only as context around a change
    bool     gapBefore;      // an elided run of identical rows precedes this row
};

struct DiffResult {
    uint64_t                 frameA, frameB;
    std::vector<RegionDiff>  regions;    // parallel to the snapshots' regions
};

DiffResult Diff(const MemSnapshot& a, const MemSnapshot& b, const DiffOptions&);
std::vector<DiffRow> BuildRows(const MemSnapshot& a, const MemSnapshot& b,
                               size_t regionIndex, const DiffOptions&);
std::string ToCsv(const MemSnapshot& a, const MemSnapshot& b, const DiffResult&,
                  size_t regionIndexOrAll);
```

Names and exact signatures are settled when `MemoryCompare.h` is written; the shape is the
contract.

## Capture

`CaptureSnapshot(IMemoryBackend&, uint64_t frameNo)` is declared here but only needs
`ReadRegionBytes` (inline in `MemoryBackend.h`), so the engine's tests need not link the core. It
walks `SaturnRegions()` and reads each region through the backend. Because `mMemBackend` follows
the live or scrub context, capture reads whatever the app is currently showing.

- Returns null if any region read fails. `ReadRegionBytes` already clears its output on failure, so
  a partial region is never kept. A snapshot is complete or absent.
- Region sizes are from `SaturnRegions()`: about 3.5 MB total (LWRAM 1 MiB, HWRAM 1 MiB,
  Sound RAM 512 KiB, VDP1 RAM 512 KiB, VDP2 RAM 512 KiB, VDP1 FB 256 KiB, CRAM 4 KiB, plus the two
  register images).

## Diff semantics

- Compare byte for byte over the region. Snapshots of the same region have equal length by
  construction; mismatched region sets are a programming error and `Diff` asserts.
- `changedBytes` counts every differing byte. A fast path compares 8 bytes at a time and only
  descends to bytes when a word differs.
- Ranges are maximal runs of differing bytes, merged when the gap between runs is below
  `mergeGap`. At most `kMaxRanges` (100,000) per region are stored; past that,
  `rangesTruncated` is set and counts stay exact.
- `BuildRows` emits one row per 16 bytes containing a change, plus `contextRows` rows either side
  when `changesOnly`. With `changesOnly` off it emits every row of the region. Adjacent context
  windows merge; a skipped run sets `gapBefore` on the next row so the panel can draw a separator.
- The row list is built per selected region on demand, not for every region up front.

## CSV export

One line per changed byte: `region,address,old,new` (address as `0x%08X`, values as `0x%02X`),
preceded by a header with both frame numbers. A single region or all regions. This is plain text
generation only; where the file goes is the panel/App's concern and should reuse the existing
export/save path the Dump SH-2 and report features use.

## Tests

New `FrontEnd/tests/MemoryCompareTests.cpp`, registered in `CMakeLists.txt` like
`MemorySearchTests`:

- identical snapshots: zero changes, no ranges, no rows in Changes Only mode
- single byte, first byte and last byte of a region
- change spanning a 16-byte row boundary; two changes within and beyond `mergeGap`
- word fast path: change at each offset 0..7 within a word, at the tail of a non-multiple-of-8
  region
- every byte differs: exact count, `rangesTruncated` honoured, no unbounded allocation
- context windows: overlapping windows merge, `gapBefore` set exactly where rows are elided, window
  clipped at region start and end
- A/B swap mirrors the diff (counts equal, old and new swapped in CSV)
- capture: failed read returns null; success copies and later mutation of the source does not
  affect the snapshot (the immutability guarantee)
- CSV golden output for a small fixture
