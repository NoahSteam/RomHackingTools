// AssemblyPanel — the live SH-2 disassembly view. Reads the selected CPU's
// registers (se_get_sh2_regs) + code memory (via IMemoryBackend), disassembles a
// window around the PC (Sh2 disassembler), and renders it with Follow-PC, branch
// navigation, gutter breakpoints, register/memory hover, and Add-Operand-to-Watch.
// Emulator-agnostic: talks to the core context + backend + breakpoint manager.
#pragma once

#include <cstdint>
#include <string>
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
        bool     stepInto = false;    // single-step the emulated frame's CPU
        bool     runTo = false;       // run until runToAddr (temp breakpoint)
        uint32_t runToAddr = 0;
        bool     resume = false;
    };
    void Draw(se_context* ctx, IMemoryBackend& backend, BreakpointManager& bps,
              WatchPanel& watch, bool live, Request& req);

private:
    struct Line
    {
        uint32_t                addr = 0;
        uint16_t                op = 0;
        bool                    readable = false;
        DisassembledInstruction ins;
    };

    void Navigate(uint32_t addr, bool pushHistory);
    void RebuildWindow(IMemoryBackend& backend, uint32_t pc, bool havePc);

    int      mCpu = 0;                 // 0 master, 1 slave
    bool     mFollowPc = true;
    uint32_t mWindowBase = 0;          // address of the first disassembled line
    bool     mWindowValid = false;
    uint32_t mLastPc = 0;
    bool     mScrollToPc = false;      // request a scroll-to-PC next frame
    std::vector<uint32_t> mBack, mFwd; // navigation history (current CPU)
    char     mGotoBuf[16] = {};
    std::vector<uint8_t> mCode;        // reused code buffer
    std::vector<Line>    mLines;       // reused decode buffer
};

}  // namespace sfe
