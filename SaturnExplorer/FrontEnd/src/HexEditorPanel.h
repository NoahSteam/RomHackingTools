// HexEditorPanel — a memory hex viewer/editor. Reads a window through the same
// IMemoryBackend the Watch/Assembly panels use (so no emulator-specific code), and
// renders the classic address / hex-grid / text layout with change highlighting,
// a byte selection + value readout, and (Step 4) inline editing that writes back
// through the backend. Emulator-agnostic.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Debug/MemoryBackend.h"

namespace sfe
{

class HexEditorPanel
{
public:
    // Draw the "Hex Editor" window. 'live' enables Auto Refresh; the panel reads a
    // window of memory each frame (when refreshing) via 'backend'. 'dt' is the frame
    // delta for the change-highlight fade.
    void Draw(IMemoryBackend& backend, bool live, float dt);

    // Jump the view to 'address' (from the Watch / Assembly "View in Hex Editor").
    void GoTo(uint32_t address);

private:
    void Refresh(IMemoryBackend& backend);

    uint32_t mBase = 0x06000000;       // first address shown
    int      mSize = 0x100;            // bytes in the window
    int      mEncoding = 0;            // 0 = ASCII, 1 = Shift-JIS (text pane)
    bool     mAutoRefresh = true;
    bool     mHighlightChanges = true;
    bool     mFocusRequested = false;  // bring the window forward after a GoTo
    char     mAddrBuf[16] = "06000000";

    std::vector<uint8_t> mBytes;       // current window (big-endian Saturn bytes)
    std::vector<uint8_t> mPrev;        // previous window, for change detection
    std::vector<float>   mChangeAge;   // per-byte highlight seconds remaining (fades)
    bool                 mHavePrev = false;
    bool                 mConnected = false;

    int  mSelStart = -1;               // selection anchor (byte index in window)
    int  mSelEnd = -1;                 // selection end (inclusive)
    bool mSelecting = false;

    // Inline editing: the byte index being typed over, its 2-hex-digit buffer, and
    // a short "Modified" flash after a successful write.
    int   mEditIdx = -1;
    char  mEditBuf[3] = {};
    bool  mEditFocus = false;
    float mModifiedFlash = 0.0f;
};

}  // namespace sfe
