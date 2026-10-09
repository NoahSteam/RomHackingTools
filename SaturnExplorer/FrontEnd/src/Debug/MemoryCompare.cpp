#include "Debug/MemoryCompare.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "SaturnRegions.h"

namespace sfe
{

namespace
{
// Per-region facts SaturnRegions() does not carry, indexed by RegionId. Name, base and size come
// from SaturnRegions() itself so there is still one table of addresses.
struct Extra { AddressSpace space; uint8_t caps; };
constexpr Extra kExtra[] = {
    /* Lwram    */ { AddressSpace::Sh2Bus,        kCapNavigate | kCapWatch | kCapBreakWrite | kCapSh2Code },
    /* Hwram    */ { AddressSpace::Sh2Bus,        kCapNavigate | kCapWatch | kCapBreakWrite | kCapSh2Code },
    // Written by the 68K, which the emulator's SH-2/SCU-DMA watchpoints do not see.
    /* SoundRam */ { AddressSpace::Sh2Bus,        kCapNavigate | kCapWatch },
    /* Vdp1Ram  */ { AddressSpace::Sh2Bus,        kCapNavigate | kCapWatch | kCapBreakWrite },
    // A derived image written by VDP1 drawing, not a bus write.
    /* Vdp1Fb   */ { AddressSpace::DeviceImage,   kCapNavigate | kCapWatch },
    /* Vdp1Regs */ { AddressSpace::RegisterImage, kCapNavigate | kCapWatch | kCapBreakWrite },
    /* Vdp2Ram  */ { AddressSpace::Sh2Bus,        kCapNavigate | kCapWatch | kCapBreakWrite },
    /* Vdp2Cram */ { AddressSpace::Sh2Bus,        kCapNavigate | kCapWatch | kCapBreakWrite },
    /* Vdp2Regs */ { AddressSpace::RegisterImage, kCapNavigate | kCapWatch | kCapBreakWrite },
};
static_assert(sizeof(kExtra) / sizeof(kExtra[0]) == kRegionCount,
              "every RegionId needs an address space and capabilities");

const RegionTraits* BuildTraits()
{
    static RegionTraits table[kRegionCount];
    size_t count = 0;
    const SaturnRegion* regions = SaturnRegions(count);
    for (size_t i = 0; i < kRegionCount && i < count; ++i)
    {
        table[i] = { static_cast<RegionId>(i), regions[i].name, kExtra[i].space,
                     regions[i].base, regions[i].size, kExtra[i].caps };
    }
    return table;
}

size_t Index(RegionId id) { return static_cast<size_t>(id); }

constexpr uint32_t kRowBytes = 16;

uint16_t MaskOfFirst(uint32_t n)   // n in 0..16
{
    return n >= kRowBytes ? 0xFFFFu : static_cast<uint16_t>((1u << n) - 1u);
}

// Byte differences of one region, with a word fast path that only drops to bytes when a word
// differs. Calls onByte(offset) for every differing byte in ascending order.
template <class F>
void ScanDifferences(const uint8_t* a, const uint8_t* b, size_t n, F onByte)
{
    size_t i = 0;
    while (i < n)
    {
        if (i + 8 <= n)
        {
            uint64_t wa, wb;
            std::memcpy(&wa, a + i, 8);
            std::memcpy(&wb, b + i, 8);
            if (wa == wb) { i += 8; continue; }
            for (size_t k = 0; k < 8; ++k)
                if (a[i + k] != b[i + k]) onByte(i + k);
            i += 8;
            continue;
        }
        if (a[i] != b[i]) onByte(i);
        ++i;
    }
}
}  // namespace

const RegionTraits& Traits(RegionId id)
{
    static const RegionTraits* table = BuildTraits();
    return table[Index(id)];
}

uint32_t BusAddress(const RegionRef& r) { return Traits(r.id).busBase + r.offset; }

bool Resolve(uint32_t busAddress, RegionRef* out)
{
    const int i = SaturnRegionIndex(busAddress);
    if (i < 0 || static_cast<size_t>(i) >= kRegionCount) return false;
    if (out)
    {
        out->id = static_cast<RegionId>(i);
        out->offset = (busAddress & 0x07FFFFFFu) - Traits(out->id).busBase;
    }
    return true;
}

// ---- Capture -----------------------------------------------------------------------------------

std::shared_ptr<const MemSnapshot> CaptureSnapshot(
    IMemoryBackend& backend, const SnapshotOrigin& origin,
    const std::function<CaptureGuard()>& guard, std::string* error)
{
    auto fail = [&](const char* why) -> std::shared_ptr<const MemSnapshot>
    {
        if (error) *error = why;
        return nullptr;
    };
    if (origin.frameNo == 0) return fail("the frame number is unknown");
    if (!backend.Connected()) return fail("no memory source is connected");

    CaptureGuard before;
    if (guard) before = guard();

    auto snap = std::make_shared<MemSnapshot>();
    snap->origin = origin;
    snap->regions.resize(kRegionCount);
    for (size_t i = 0; i < kRegionCount; ++i)
    {
        const RegionTraits& t = Traits(static_cast<RegionId>(i));
        MemRegionImage& img = snap->regions[i];
        img.id = t.id;
        // ReadRegionBytes clears its output on any failure, so a half-read region is never kept.
        if (!ReadRegionBytes(backend, t.busBase, t.size, img.bytes) || img.bytes.size() != t.size)
            return fail("a memory region could not be read");
    }

    if (guard)
    {
        const CaptureGuard after = guard();
        if (after.derivedSerial != before.derivedSerial || after.sourceId != before.sourceId)
            return fail("memory changed during capture");
    }
    return snap;
}

// ---- Diff --------------------------------------------------------------------------------------

uint64_t DiffResult::TotalChangedBytes() const
{
    uint64_t n = 0;
    for (const RegionDiff& r : regions) n += r.changedBytes;
    return n;
}

DiffStatus Diff(const std::shared_ptr<const MemSnapshot>& a,
                const std::shared_ptr<const MemSnapshot>& b,
                const DiffOptions& opts, DiffResult* out)
{
    if (!a || !b || !out) return DiffStatus::NullSnapshot;
    if (a->origin.sessionId != b->origin.sessionId) return DiffStatus::SessionMismatch;
    if (a->regions.size() != kRegionCount || b->regions.size() != kRegionCount)
        return DiffStatus::RegionMismatch;
    for (size_t i = 0; i < kRegionCount; ++i)
    {
        const MemRegionImage& ra = a->regions[i];
        const MemRegionImage& rb = b->regions[i];
        if (ra.id != static_cast<RegionId>(i) || rb.id != ra.id ||
            ra.bytes.size() != rb.bytes.size() || ra.bytes.size() != Traits(ra.id).size)
            return DiffStatus::RegionMismatch;
    }

    // Contiguous differing bytes always merge; mergeGap widens that to nearby runs.
    const uint32_t maxGap = opts.mergeGap ? opts.mergeGap - 1 : 0;

    out->a = a;
    out->b = b;
    out->regions.assign(kRegionCount, RegionDiff());
    for (size_t i = 0; i < kRegionCount; ++i)
    {
        RegionDiff& rd = out->regions[i];
        rd.id = static_cast<RegionId>(i);
        bool open = false;
        uint32_t start = 0, end = 0;
        auto close = [&]()
        {
            if (!open) return;
            ++rd.rangeCount;
            if (rd.ranges.size() < kMaxStoredRanges) rd.ranges.push_back({ start, end - start });
            open = false;
        };
        ScanDifferences(a->regions[i].bytes.data(), b->regions[i].bytes.data(),
                        a->regions[i].bytes.size(), [&](size_t off)
        {
            const uint32_t o = static_cast<uint32_t>(off);
            ++rd.changedBytes;
            if (open && o - end <= maxGap) { end = o + 1; return; }
            close();
            open = true;
            start = o;
            end = o + 1;
        });
        close();
        rd.rangesTruncated = rd.rangeCount > rd.ranges.size();
    }
    return DiffStatus::Ok;
}

// ---- Rows --------------------------------------------------------------------------------------

std::vector<DiffRow> BuildRows(const DiffResult& diff, RegionId region, const DiffOptions& opts)
{
    std::vector<DiffRow> rows;
    if (!diff.a || !diff.b) return rows;
    const std::vector<uint8_t>& A = diff.a->regions[Index(region)].bytes;
    const std::vector<uint8_t>& B = diff.b->regions[Index(region)].bytes;
    const size_t n = A.size();
    const size_t rowCount = (n + kRowBytes - 1) / kRowBytes;

    std::vector<uint16_t> changed(rowCount, 0);
    ScanDifferences(A.data(), B.data(), n, [&](size_t off)
    {
        changed[off / kRowBytes] |= static_cast<uint16_t>(1u << (off % kRowBytes));
    });

    std::vector<uint8_t> keep(rowCount, opts.changesOnly ? 0 : 1);
    if (opts.changesOnly)
    {
        for (size_t r = 0; r < rowCount; ++r)
        {
            if (!changed[r]) continue;
            const size_t lo = r >= opts.contextRows ? r - opts.contextRows : 0;
            const size_t hi = std::min(rowCount - 1, r + static_cast<size_t>(opts.contextRows));
            for (size_t k = lo; k <= hi; ++k) keep[k] = 1;
        }
    }

    bool havePrev = false;
    size_t prev = 0;
    for (size_t r = 0; r < rowCount; ++r)
    {
        if (!keep[r]) continue;
        DiffRow row;
        row.ref = { region, static_cast<uint32_t>(r * kRowBytes) };
        row.validMask = MaskOfFirst(static_cast<uint32_t>(std::min<size_t>(kRowBytes, n - r * kRowBytes)));
        row.changedMask = changed[r];
        row.isContext = changed[r] == 0;
        row.gapBefore = havePrev ? r != prev + 1 : r != 0;
        rows.push_back(row);
        havePrev = true;
        prev = r;
    }
    return rows;
}

// ---- CSV ---------------------------------------------------------------------------------------

CsvResult WriteCsv(const DiffResult& diff, const RegionId* only, CsvSink& sink)
{
    if (!diff.a || !diff.b) return CsvResult::IntegrityError;

    std::string buf;
    buf.reserve(1u << 16);
    bool cancelled = false;
    auto flush = [&]()
    {
        if (buf.empty()) return;
        if (!sink.Write(buf.data(), buf.size())) cancelled = true;
        buf.clear();
    };

    char line[160];
    auto origin = [&](const char* tag, const SnapshotOrigin& o)
    {
        const int n = std::snprintf(line, sizeof(line),
            "# %s: frame %llu, session %llu, timeline %llu, %s\n", tag,
            static_cast<unsigned long long>(o.frameNo), static_cast<unsigned long long>(o.sessionId),
            static_cast<unsigned long long>(o.timelineEpoch), o.liveHead ? "live head" : "rewind frame");
        buf.append(line, static_cast<size_t>(n));
    };
    buf += "# Saturn Explorer memory diff\n";
    origin("A", diff.a->origin);
    origin("B", diff.b->origin);
    buf += "region,offset,bus_address,old,new\n";

    for (size_t i = 0; i < kRegionCount && !cancelled; ++i)
    {
        const RegionId id = static_cast<RegionId>(i);
        if (only && *only != id) continue;
        const RegionTraits& t = Traits(id);
        const std::vector<uint8_t>& A = diff.a->regions[i].bytes;
        const std::vector<uint8_t>& B = diff.b->regions[i].bytes;
        uint64_t written = 0;
        ScanDifferences(A.data(), B.data(), A.size(), [&](size_t off)
        {
            if (cancelled) return;
            int n;
            if (t.space == AddressSpace::DeviceImage)
                n = std::snprintf(line, sizeof(line), "%s,0x%08X,,0x%02X,0x%02X\n", t.name,
                                  static_cast<unsigned>(off), A[off], B[off]);
            else
                n = std::snprintf(line, sizeof(line), "%s,0x%08X,0x%08X,0x%02X,0x%02X\n", t.name,
                                  static_cast<unsigned>(off),
                                  static_cast<unsigned>(t.busBase + off), A[off], B[off]);
            buf.append(line, static_cast<size_t>(n));
            ++written;
            if (buf.size() >= (1u << 16)) flush();
        });
        if (cancelled) break;
        // A short file must never pass for a complete export.
        if (diff.regions.size() > i && written != diff.regions[i].changedBytes)
        {
            flush();
            return CsvResult::IntegrityError;
        }
    }
    flush();
    return cancelled ? CsvResult::Cancelled : CsvResult::Ok;
}

}  // namespace sfe
