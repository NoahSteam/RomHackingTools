// MemoryComparePanel -- the "Memory Compare" window: the byte-exact difference between two frames'
// memory, as a per-region summary above a side-by-side hex diff. It owns no memory and runs no
// diff: App hands it a DiffResult (which holds the two immutable snapshots) each frame, and the
// panel reads bytes from those. Design: Docs/MemoryCompare/ComparePanel.md.
//
// Like HexEditorPanel it does not act on the rest of the app. A context-menu choice (Go to Memory,
// Add to Watch, ...) is raised as a request that App polls after Draw and carries out through the
// existing component.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Debug/MemoryCompare.h"

namespace sfe
{

// The colour of side 0 (A, the baseline: blue) or 1 (B, the comparison: orange), packed as ImGui's
// IM_COL32 does. Shared by the panel and the timeline markers so a glance says which side is which.
inline uint32_t CompareSideColor(int side, uint32_t alpha = 255)
{
    const uint32_t rgb = side == 0 ? 0x6199F2u : 0xF29E40u;
    return (alpha << 24) | ((rgb & 0xFFu) << 16) | (rgb & 0xFF00u) | ((rgb >> 16) & 0xFFu);
}

class MemoryComparePanel
{
public:
    enum class Side { A, B };
    enum class Action
    {
        GoToMemory,       // Memory tab at the byte (scrub to that side's frame first when attached)
        AddWatch,
        BreakOnWrite,
        ViewInAssembly,
        ExportDiff,       // allRegions, or ref.id
        GoToFrame         // scrub the timeline to that side's frame
    };
    struct Request
    {
        Action    action = Action::GoToMemory;
        Side      side = Side::A;
        RegionRef ref;                 // the selected byte (unused by ExportDiff and GoToFrame)
        uint32_t  size = 1;            // selection length rounded down to 1, 2 or 4 (watch/breakpoint)
        bool      allRegions = false;  // ExportDiff: every region, else exportRegion
        RegionId  exportRegion = RegionId::Lwram;
    };

    // Draw the window. 'diff' null means no comparison is set up yet. A side is "attached" while
    // its frame is still on the rewind timeline: only then can the panel offer to go to it.
    void Draw(const DiffResult* diff, bool aAttached, bool bAttached);

    // Returns true once per request, moving it out; false when nothing is pending.
    bool TakeRequest(Request& out);

    // Bring the window forward (App calls this when the Compare button opens it).
    void RequestFocus() { mFocusRequested = true; }

    // Show 'ref' in the diff: selects its region (leaving "All" alone), turns Changes Only off if
    // that is what hides its row, scrolls to it and selects the byte.
    void GoTo(const RegionRef& ref);

    // Whether the context menu offers 'action' on a byte in 'region'. The menu reads this, so a
    // test can hold the capability table to it without driving a popup.
    static bool ActionEnabled(Action action, RegionId region);

    // "06034F20" (a bus address) or "HWRAM+34F20" / "VDP1 FB+0x1230" (region and offset). False when
    // the text is neither, or names a place outside the captured regions.
    static bool ParseLocation(const char* text, RegionRef* out);

    // The grid's layout rule, from the current font and style, public so tests can hold the real
    // table to it.
    struct GridMetrics { float byteW = 0.0f; float asciiW = 0.0f; float rowH = 0.0f; };
    static GridMetrics Metrics();

    // State a test (or App) may read.
    int       SelectedRegion() const { return mRegionSel; }      // -1 = All Memory
    bool      ChangesOnly() const { return mChangesOnly; }
    size_t    LineCount() const { return mLines.size(); }
    bool      HasSelection() const { return mSelValid; }
    RegionRef SelectionStart() const { return { mSelRegion, mSelLo }; }
    uint32_t  SelectionLength() const { return mSelValid ? mSelHi - mSelLo + 1 : 0; }

private:
    // One displayed line of the diff table. All lines are one row tall so the clipper stays exact.
    struct Line
    {
        enum Kind : uint8_t { Header, Gap, Row } kind = Row;
        RegionId region = RegionId::Lwram;
        uint32_t count = 0;       // Gap: identical rows elided; Header: changed bytes in the region
        DiffRow  row;             // Row
    };

    // What the line list is built from, besides the snapshot pair: one comparison per key.
    struct BuildKey
    {
        int  region = -1;
        bool changesOnly = true;
        bool context = true;     // only meaningful while Changes Only is on; otherwise every row shows
        bool operator==(const BuildKey& o) const
        { return region == o.region && changesOnly == o.changesOnly && context == o.context; }
    };
    BuildKey Key() const { return { mRegionSel, mChangesOnly, mChangesOnly && mShowContext }; }

    void DrawBody(const DiffResult* diff);
    void EnsureLines(const DiffResult& diff);
    void SelectByte(const RegionRef& ref, bool extend);
    bool InSelection(RegionId region, uint32_t offset) const;
    const float* AsciiAdvance();   // mAsciiAdvance, refreshed when the font or size changed
    void DrawCards(const DiffResult& diff);
    void DrawToolbar();
    void DrawSummary(const DiffResult& diff, float height);
    void DrawGrid(const DiffResult& diff);
    void DrawContextMenu();
    void Raise(Action action, Side side);
    void RaiseExport(bool allRegions, RegionId region);
    int  FindLine(const RegionRef& ref) const;

    bool     mAttached[2] = { false, false };   // per side: still on the rewind timeline (set by Draw)
    float    mAsciiAdvance[95] = {};            // advance of each printable ASCII char, for the font below
    const void* mAdvanceFont = nullptr;
    float    mAdvanceSize = 0.0f;

    int      mRegionSel = -1;            // -1 = All Memory, else a RegionId
    bool     mChangesOnly = true;
    bool     mShowContext = true;
    bool     mFocusRequested = false;

    // Which pair the view belongs to. Held weakly: identity without ownership, and unlike a raw
    // address it cannot be mistaken for a later snapshot allocated in the same place.
    std::weak_ptr<const MemSnapshot> mPairA, mPairB;
    BuildKey  mBuilt;
    bool      mLinesValid = false;
    std::vector<Line> mLines;

    bool      mSelValid = false;
    RegionId  mSelRegion = RegionId::Lwram;
    uint32_t  mSelLo = 0, mSelHi = 0;    // inclusive offsets within mSelRegion
    uint32_t  mSelAnchor = 0;
    Side      mSelSide = Side::A;

    int       mScrollLine = -1;          // line to bring to the top on the next grid draw
    RegionRef mPendingJump;              // a GoTo waiting for its region's lines to be built
    bool      mHavePendingJump = false;
    float     mSplitTop = 0.0f;          // height of the summary section; 0 until first drawn
    char      mJumpBuf[32] = "";
    std::string mStatus;                 // one line under the toolbar (bad address, mode switch)

    bool      mRequested = false;
    Request   mRequest;
};

}  // namespace sfe
