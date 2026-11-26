// AssemblyPanel — the live SH-2 disassembly view. Reads the selected CPU's
// registers (se_get_sh2_regs) + code memory (via IMemoryBackend), disassembles a
// window around the PC (Sh2 disassembler), and renders it with Follow-PC, branch
// navigation, gutter breakpoints, register/memory hover, and Add-Operand-to-Watch.
// Emulator-agnostic: talks to the core context + backend + breakpoint manager.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "saturnexplorer/SaturnExplorer.h"

#include "AsmCodeWindow.h"
#include "Debug/MemoryBackend.h"
#include "Debug/BreakpointManager.h"
#include "Debug/ExecutionActions.h"
#include "Debug/Sh2Asm.h"
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
        bool     runTo = false;       // run until runToAddr (a transient, retired on arrival)
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
        bool     decompile = false;       // show the function containing decompileAddr in SH-2 Pseudocode
        uint32_t decompileAddr = 0;
        bool     showPseudocode = false;  // show the line of the instruction at pseudoAddr in SH-2 Pseudocode (A7-5)
        uint32_t pseudoAddr = 0;
        // An in-place edit was written this frame (instruction or literal). 'asmAddr' is where the
        // bytes went, 'asmSize' how many (2 for an instruction, 2 or 4 for a literal), asmOld/asmNew
        // the big-endian values. 'asmNote' is the one-line outcome the panel also shows, ready for
        // the App's Log.
        bool        assembled = false;
        uint32_t    asmAddr = 0;
        uint32_t    asmSize = 0;
        uint32_t    asmOld = 0;
        uint32_t    asmNew = 0;
        std::string asmNote;
    };
    void Draw(se_context* ctx, IMemoryBackend& backend, BreakpointManager& bps,
              ExecutionActions& actions, WatchPanel& watch, bool live, Request& req);

    // Offer "Decompile Here" and "Show in Pseudocode" in the row menu: only builds with the decompiler have the panel they open.
    void SetOfferDecompile(bool offer) { mOfferDecompile = offer; }

    // Resolves a symbol typed into the assembler (branch targets, PC-relative operands). Unset: symbols
    // are rejected with the assembler's own message. Same setter style as SetOfferDecompile.
    void SetSymbolResolver(std::function<bool(const std::string&, uint32_t&, std::string&)> r)
    { mResolveSymbol = std::move(r); }

    // Auto Refresh is otherwise a checkbox only; exposed so a host (and the tests) can start with it off.
    void SetAutoRefresh(bool on) { mAutoRefresh = on; }

    // In-place editing state, read-only: whether an edit box is open, its current text, and the last
    // outcome line (a refusal, an error or "Wrote ..."), which the panel also draws under the header.
    bool               Editing() const { return mEdit.kind != EditKind::None; }
    std::string        EditText() const { return mEdit.buf; }
    const std::string& Notice() const { return mNotice; }
    bool               NoticeIsError() const { return mNoticeLevel == 2; }

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
    // panel's own CPU matches). Resume Follow PC and force a scroll even if this halt
    // is at the previous PC: the user may have browsed or scrolled away in between.
    // Deliberately not GoTo, which disables Follow PC and pushes navigation history.
    void Reveal(int cpu)
    {
        mCpu = cpu & 1;
        mFollowPc = true;
        mWindowValid = false;
        mFocusRequested = true;
    }

    // The user's notes (address -> text), shared by both CPUs. Read by Data > Dump SH-2, which
    // writes them in place of the generated comments exactly as the panel shows them.
    const std::unordered_map<uint32_t, std::string>& UserComments() const { return mComments; }

    // Persist / restore the user comment store (address -> note). Called by the App
    // at startup / shutdown, mirroring WatchPanel session persistence.
    // Load replaces the notes; a missing file is no notes. False, with 'error', and the notes as they were, when the file is
    // there and was not read (not a regular file, over 8 MiB, unreadable). Lines that are not "<hex-address> <note>"
    // are skipped and counted (SkippedCommentLines). Save writes the file whole or not at all.
    bool LoadComments(std::string* error = nullptr);
    bool SaveComments(std::string* error = nullptr) const;
    size_t SkippedCommentLines() const { return mSkippedCommentLines; }
    // Where a failed save after an edit is reported (the App's Log), with the reason (A9-27). Unset: not reported.
    void SetSaveErrorHandler(std::function<void(const std::string&)> h) { mOnSaveError = std::move(h); }

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
    bool     mOfferDecompile = false;
    bool     mFollowPc = true;

    // Live breakpoint-hit state (set by SetBreakpointStop): tint the halted row red.
    bool     mBpStopActive = false;
    int      mBpStopCpu = 0;
    uint32_t mBpStopPc = 0;
    bool     mAutoRefresh = true;      // re-read the code bytes every frame (live)
    // The address the view is framed on -- the PC while following, else the last navigation
    // target. It picks which captured region the list spans (see AsmCodeWindow.h); the list
    // itself is that whole region, only the rows on screen are read and decoded.
    uint32_t mWindowAnchor = 0;
    // False forces the next frame to re-frame on the PC and scroll to it (CPU switch, Follow PC
    // re-enabled, Go to PC).
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

    // Decoded rows, cached a page at a time (kPageRows instructions, aligned to the span's base; see the .cpp)
    // so a frame reads a couple of small pages rather than a row at a time. Cleared every frame
    // while Auto Refresh is on; held otherwise, which is what lets the disassembly hold still.
    struct Page { std::vector<Line> lines; };
    std::unordered_map<uint32_t, Page> mPages;   // keyed by the page's first address
    uint64_t mPagesSource = 0;                   // backend.SourceId() the pages were read from
    void        LoadPages(IMemoryBackend& backend, const AsmSpan& span, uint32_t rowFirst, uint32_t rowEnd);
    const Line* LineAt(const AsmSpan& span, uint32_t row) const;

    // Instruction selection (by address) for "Find in data directory". Click an
    // address to select one instruction; shift-click another to extend a contiguous
    // range. Stored as an inclusive [lo, hi] address span with an anchor.
    bool     mHasSel = false;
    uint32_t mSelAnchorAddr = 0;
    uint32_t mSelLoAddr = 0;
    uint32_t mSelHiAddr = 0;

    // User comment store (address -> note), overlaid on the auto-generated comment
    // and persisted across sessions. Shared by both CPUs (they share the address map).
    std::unordered_map<uint32_t, std::string> mComments;
    size_t mSkippedCommentLines = 0;
    mutable std::map<uint32_t, std::string> mBase;   // the notes the file held at Load or at the last Save
    std::string mRefusedWhy;                          // why LoadComments refused the file (A9-19)
    std::function<void(const std::string&)> mOnSaveError;
    void SaveAfterEdit();
    // Which operand the row context menu was opened on, latched at open time (-1 when it
    // was opened from a cell other than the operands). The pointer has usually moved off
    // the operand by the time a menu item is picked, so it cannot be re-read then.
    int      mCtxOperand = -1;

    // The code bytes the row context menu's "Find in data directory" will search, read once when
    // the menu opens: the selection can span rows that are not on screen, and re-reading it
    // every frame the menu stays open would be a request per frame.
    std::vector<uint8_t> mCtxFindBytes;
    uint32_t             mCtxFindStart = 0;

    // ---- In-place editing (instruction text, or a PC-relative load's literal) ----
    enum class EditKind { None, Instruction, Literal };
    struct AsmEdit
    {
        EditKind kind = EditKind::None;
        uint32_t rowAddr = 0;        // the instruction row the box sits in
        uint32_t target = 0;         // where the bytes go: the row itself, or the literal's address
        uint32_t size = 2;           // 2, or 2/4 for a literal
        int      cpu = 0;
        uint64_t sourceId = 0;       // the source the edit was opened on; another one cancels it
        bool     focusPending = false;
        unsigned beganFrame = 0;     // ImGui frame count at open, so the not-drawn check skips that frame
        char     buf[128] = {};
        uint32_t oldValue = 0;       // what the target held when opened (preview only; re-read at commit)
        Sh2AsmContext ctx;           // context built when opened (preview only; rebuilt at commit)
        // Live preview of 'previewFor'.
        std::string previewFor;
        bool        previewReady = false;
        bool        previewOk = false;
        uint32_t    previewValue = 0;
        std::string previewMsg;      // the error with its column, or the warnings
        bool        previewWarn = false;
        // Literal edits: other instructions loading the same slot, found when opened.
        size_t   sharers = 0;
        uint32_t firstSharer = 0;
    };
    AsmEdit     mEdit;
    std::function<bool(const std::string&, uint32_t&, std::string&)> mResolveSymbol;
    std::string mNotice;             // last edit outcome, shown under the header
    int         mNoticeLevel = 0;    // 0 ok, 1 warning, 2 error
    float       mNoticeAge = 0.0f;
    // A write changed bytes the cached pages hold. Applied after the row loop, not at the write: the
    // loop holds references into the pages, so clearing them mid-frame would leave dangling rows.
    bool        mDropPages = false;
    void SetNotice(const std::string& text, int level)
    { mNotice = text; mNoticeLevel = level; mNoticeAge = 12.0f; }
    void CancelEdit() { mEdit = AsmEdit(); }
    void BeginInstructionEdit(IMemoryBackend& backend, const Line& ln);
    void BeginLiteralEdit(IMemoryBackend& backend, const Line& ln);
    void UpdatePreview();
    void CommitEdit(IMemoryBackend& backend, Request& req);
    bool BuildAsmContext(IMemoryBackend& backend, uint32_t addr, Sh2AsmContext& ctx, uint16_t& curOp);

    uint32_t mEditCommentAddr = 0;     // address whose comment cell is being edited
    bool     mEditingComment = false;
    bool     mCommentFocus = false;    // grab keyboard focus on the first edit frame
    char     mCommentBuf[128] = {};
};

}  // namespace sfe
