// HexEditorPanel — the "Memory" window: a raw memory viewer/editor. Reads through the
// same IMemoryBackend the Watch/Assembly panels use (so no emulator-specific code), and
// renders the classic address / hex-grid / text layout with change highlighting, a byte
// selection + value readout, and inline editing that writes back through the backend.
//
// Like Yabause's memory editor it has a tab per Saturn region (the first, "All", spans the
// whole CPU address space); the whole region scrolls virtually (ImGuiListClipper) rather
// than a fixed small window, so there is no size to choose. Emulator-agnostic.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Debug/MemoryBackend.h"

namespace sfe
{

class HexEditorPanel
{
public:
    // Draw the "Memory" window. 'live' enables Auto Refresh; the panel reads the visible
    // rows each frame (when refreshing) via 'backend'. 'dt' is the frame delta for the
    // change-highlight fade.
    void Draw(IMemoryBackend& backend, bool live, float dt);

    // Jump the view to 'address' (from the Watch / Assembly "View in Memory"): selects the
    // region tab containing it and scrolls it into view.
    void GoTo(uint32_t address);

    // Jump to 'address' and highlight a [address, address+length) span (e.g. a call
    // stack frame's stack range).
    void Select(uint32_t address, uint32_t length);

    // A "find these bytes in the data directory" request raised from the grid's
    // right-click menu (the current byte selection). The App polls this after Draw and
    // runs the data-directory search. Returns true once per request, moving the selected
    // bytes + a human label out; false when nothing is pending.
    bool TakeSearchRequest(std::vector<uint8_t>& outBytes, std::string& outLabel);

    // A "set a memory breakpoint here" request raised from the grid's right-click menu
    // (Add breakpoint -> Read/Write/Read or Write -> Byte/Short/Long). The App polls this
    // after Draw and installs it in the BreakpointManager. 'kind' is the wire kind: 1 read,
    // 2 write, 3 read/write. Returns true once per request, moving the request out.
    struct BreakpointRequest { uint32_t address = 0; uint32_t size = 1; int kind = 0; };
    bool TakeBreakpointRequest(BreakpointRequest& out);

    // A "locate this selection in the game files and record it for patching" request (the
    // Patch feature). Unlike TakeSearchRequest it carries the selection's address + length so
    // the App can add surrounding context bytes and map the match into the PatchLibrary.
    // Returns true once per request, moving it out.
    struct LocateRequest { uint32_t address = 0; uint32_t length = 0; };
    bool TakeLocateRequest(LocateRequest& out);

    // One selectable region: a CPU-address span served by the backend. Index 0 is "All".
    struct Region { const char* name; uint32_t base; uint32_t size; };

private:
    static const std::vector<Region>& Regions();
    // Index of the region containing 'addr' (1..N), or 0 ("All") when none matches.
    static int RegionForAddr(uint32_t addr);

    int  mTab = 0;                     // active region index (0 = All)
    int  mSelectTab = -1;              // request to switch tabs (GoTo / initial), -1 = none
    bool mScrollPending = true;        // scroll mScrollAddr into view next draw
    uint32_t mScrollAddr = 0x06000000; // open on high work RAM, not the unmapped 0 (BIOS)

    int  mEncoding = 0;                // 0 = ASCII, 1 = Shift-JIS (text pane)
    bool mAutoRefresh = true;
    bool mHighlightChanges = true;
    bool mFocusRequested = false;      // bring the window forward after a GoTo
    char mAddrBuf[16] = "06000000";

    // Selection + edit are keyed by absolute address so they survive scrolling.
    int64_t mSelStart = -1;            // selection anchor (absolute address, -1 = none)
    int64_t mSelEnd   = -1;            // selection end (inclusive)
    bool    mSelecting = false;
    int64_t mEditAddr = -1;            // address being typed over (-1 = none)
    char    mEditBuf[3] = {};
    bool    mEditFocus = false;
    bool    mEditSelectAll = false;    // select-all on focus (double-click) vs caret-at-end (type)
    bool    mEditFlow = false;         // edit started by typing: auto-commit at 2 digits + advance
    float   mModifiedFlash = 0.0f;

    bool mConnected = false;

    // Change highlighting across scrolling: the previous value and the fade timer per
    // address touched. Bounded (cleared when it grows large or the tab changes).
    std::unordered_map<uint32_t, uint8_t> mPrevByte;
    std::unordered_map<uint32_t, float>   mChangeAge;

    // Pending "find selection in data directory" request (see TakeSearchRequest).
    bool                 mSearchRequested = false;
    std::vector<uint8_t> mSearchBytes;
    std::string          mSearchLabel;

    // Pending "add memory breakpoint" request (see TakeBreakpointRequest).
    bool               mBpRequested = false;
    BreakpointRequest  mBpRequest;

    // Pending "locate selection in game files for patching" request (see TakeLocateRequest).
    bool               mLocateRequested = false;
    LocateRequest      mLocateRequest;
};

}  // namespace sfe
