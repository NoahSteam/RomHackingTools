// AssemblyPanel — the live SH-2 disassembly view. Reads the selected CPU's
// registers (se_get_sh2_regs) + code memory (via IMemoryBackend), disassembles a
// window around the PC (Sh2 disassembler), and renders it with Follow-PC, branch
// navigation, gutter breakpoints, register/memory hover, and Add-Operand-to-Watch.
// Emulator-agnostic: talks to the core context + backend + breakpoint manager.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "saturnexplorer/SaturnExplorer.h"

#include "Debug/MemoryBackend.h"
#include "Debug/BreakpointManager.h"
#include "Debug/Sh2Disasm.h"

namespace sfe
{

class WatchPanel;

class AssemblyPanel
{
public:
    // Draw the "SH-2 Assembly" window. 'ctx' is the current core context (live or
    // savestate); 'live' enables the run-control actions (step/run-to). Returns via
    // out-params any control request the App should act on this frame.
    struct Request
    {
        bool     runTo = false;       // run until runToAddr (sets a breakpoint there)
        uint32_t runToAddr = 0;
    };
    void Draw(se_context* ctx, IMemoryBackend& backend, BreakpointManager& bps,
              WatchPanel& watch, bool live, Request& req);

    // The CPU the panel is currently viewing (0 master, 1 slave) — used by the App
    // to target run-control (Run to Here) at the right core.
    int Cpu() const { return mCpu; }

    // Persist / restore the user comment store (address -> note). Called by the App
    // at startup / shutdown, mirroring WatchPanel session persistence.
    void LoadComments();
    void SaveComments() const;

private:
    struct Line
    {
        uint32_t                addr = 0;
        uint16_t                op = 0;
        bool                    readable = false;
        DisassembledInstruction ins;
    };

    void Navigate(uint32_t addr, bool pushHistory);

    int      mCpu = 0;                 // 0 master, 1 slave
    bool     mFollowPc = true;
    bool     mAutoRefresh = true;      // re-read the code window every frame (live)
    uint32_t mWindowBase = 0;          // address of the first disassembled line
    bool     mWindowValid = false;
    uint32_t mLastPc = 0;
    bool     mScrollToPc = false;      // request a scroll-to-PC next frame
    std::vector<uint32_t> mBack, mFwd; // navigation history (current CPU)
    char     mGotoBuf[16] = {};
    std::vector<Line>    mLines;       // reused decode buffer

    // Frozen-window cache: when Auto Refresh is off (and the base hasn't moved) the
    // panel reuses these bytes instead of re-reading, so the view holds still.
    std::vector<uint8_t> mWindowBytes;
    uint32_t             mWindowBytesBase = 0;
    bool                 mHaveWindowBytes = false;

    // User comment store (address -> note), overlaid on the auto-generated comment
    // and persisted across sessions. Shared by both CPUs (they share the address map).
    std::unordered_map<uint32_t, std::string> mComments;
    uint32_t mEditCommentAddr = 0;     // address whose comment cell is being edited
    bool     mEditingComment = false;
    bool     mCommentFocus = false;    // grab keyboard focus on the first edit frame
    char     mCommentBuf[128] = {};
};

}  // namespace sfe
