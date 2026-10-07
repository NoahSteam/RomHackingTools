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
#include "Debug/ExecutionActions.h"
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
        bool     viewHex = false;     // reveal hexAddr in the Hex Editor
        uint32_t hexAddr = 0;
        bool     editTracepoint = false;  // open the tracepoint editor for tpAddr/tpCpu
        uint32_t tpAddr = 0;
        int      tpCpu = 0;
        bool     findInData = false;      // search selected instruction bytes in the data dir
        std::vector<uint8_t> findBytes;   // the big-endian code bytes to search for
        std::string          findLabel;   // human label for the results window
        // Run control mirrored into the panel header (only while halted). The App resolves
        // these against the panel's CPU: continue / step one instruction / step over a call
        // / step out to the caller.
        bool     continueRun = false;
        bool     stepInto = false;
        bool     stepOver = false;
        bool     stepOut = false;
    };
    void Draw(se_context* ctx, IMemoryBackend& backend, BreakpointManager& bps,
              ExecutionActions& actions, WatchPanel& watch, bool live, Request& req);

    // The CPU the panel is currently viewing (0 master, 1 slave) — used by the App
    // to target run-control (Run to Here) at the right core.
    int Cpu() const { return mCpu; }

    // Tell the panel the emulator is halted on an execution breakpoint so it can tint
    // the hit instruction's row red. 'active' false clears it (running / other stop).
    // Called by the App each frame from the live stop state.
    void SetBreakpointStop(bool active, int cpu, uint32_t pc)
    { mBpStopActive = active; mBpStopCpu = cpu; mBpStopPc = pc; }

    // Jump the view to 'addr' on 'cpu' (the Log panel's "Jump to Assembly").
    void GoTo(int cpu, uint32_t addr);

    // Bring the panel forward on 'cpu' -- its dock tab is selected and, if the halt is on
    // the other SH-2, the view switches to it (the halted row is only tracked while the
    // panel's own CPU matches). Deliberately not GoTo: that turns Follow PC off and pushes
    // a history entry for a jump the user never made, and on a halt the panel is already
    // following the PC. Used when execution stops so the halted instruction is on screen.
    void Reveal(int cpu) { mCpu = cpu & 1; mFocusRequested = true; }

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

    // Live breakpoint-hit state (set by SetBreakpointStop): tint the halted row red.
    bool     mBpStopActive = false;
    int      mBpStopCpu = 0;
    uint32_t mBpStopPc = 0;
    bool     mAutoRefresh = true;      // re-read the code window every frame (live)
    uint32_t mWindowBase = 0;          // address of the first disassembled line
    // The address the view is framed on -- the PC while following, else the last navigation
    // target. Distinct from mWindowBase, which now sits a lead before it so there is code
    // above the anchor to scroll back into; this is what the history records.
    uint32_t mWindowAnchor = 0;
    // Instructions actually decoded. Not always kWinInstr: the window is trimmed to what the
    // anchor's region can serve, since the panel reads it in one request that would otherwise
    // be rejected whole.
    int      mWindowInstr = 0;
    bool     mWindowValid = false;
    uint32_t mLastPc = 0;
    // Pending scroll request, by address rather than "the PC": a navigation target is usually
    // NOT the PC (a caller's entry point, a branch target), and keying the scroll off the PC row
    // meant those jumps silently never scrolled -- no row matched, so nothing fired.
    bool     mScrollPending = false;
    uint32_t mScrollAddr = 0;
    float    mScrollAlign = 0.0f;      // 0 = target at the top of the view, 1 = bottom
    // Bumped by every request. Rendering compares it against the value it started with, so a
    // request raised DURING rendering -- Navigate() from a branch-operand click or Follow
    // Branch, which targets a window this frame has not decoded -- is not mistaken for a stale
    // one and discarded before it can ever be honoured.
    unsigned mScrollSeq = 0;

    // Set the pending scroll request. One place, so the sequence counter cannot be forgotten.
    void RequestScroll(uint32_t addr, float align)
    {
        mScrollPending = true;
        mScrollAddr = addr;
        mScrollAlign = align;
        ++mScrollSeq;
    }
    bool     mFocusRequested = false;  // bring the window forward after an external GoTo/Navigate
    std::vector<uint32_t> mBack, mFwd; // navigation history (current CPU)
    char     mGotoBuf[16] = {};
    std::vector<Line>    mLines;       // reused decode buffer

    // Instruction selection (by address) for "Find in data directory". Click an
    // address to select one instruction; shift-click another to extend a contiguous
    // range. Stored as an inclusive [lo, hi] address span with an anchor.
    bool     mHasSel = false;
    uint32_t mSelAnchorAddr = 0;
    uint32_t mSelLoAddr = 0;
    uint32_t mSelHiAddr = 0;

    // Frozen-window cache: when Auto Refresh is off (and the base hasn't moved) the
    // panel reuses these bytes instead of re-reading, so the view holds still.
    std::vector<uint8_t> mWindowBytes;
    uint32_t             mWindowBytesBase = 0;
    bool                 mHaveWindowBytes = false;
    uint8_t              mWindowPrev[2] = {};      // the instruction just before mWindowBase
    bool                 mHaveWindowPrev = false;
    uint64_t             mWindowBytesSource = 0;   // backend.SourceId() the bytes were read from

    // User comment store (address -> note), overlaid on the auto-generated comment
    // and persisted across sessions. Shared by both CPUs (they share the address map).
    std::unordered_map<uint32_t, std::string> mComments;
    // Which operand the row context menu was opened on, latched at open time (-1 when it
    // was opened from a cell other than the operands). The pointer has usually moved off
    // the operand by the time a menu item is picked, so it cannot be re-read then.
    int      mCtxOperand = -1;

    uint32_t mEditCommentAddr = 0;     // address whose comment cell is being edited
    bool     mEditingComment = false;
    bool     mCommentFocus = false;    // grab keyboard focus on the first edit frame
    char     mCommentBuf[128] = {};
};

}  // namespace sfe
