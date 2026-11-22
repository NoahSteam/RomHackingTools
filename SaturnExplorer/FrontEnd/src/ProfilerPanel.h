// Profiler panel: draws what ProfilerModel holds. Four tabs --
//   Frame      where each emulated frame's time went: both SH-2s (busy / stalled / asleep / off),
//              VDP1 drawing, the SCU DSP and the 68000, as budget bars plus a history graph
//   Buses      SH-2 accesses per device grouped by bus, and SCU DMA traffic by level and route
//   Memory     how full each fixed RAM is and which parts are being rewritten, as block maps
//   Functions  the functions the SH-2s spent their time in (opt-in: it slows the emulator)
//
// The panel owns only view state (averaging window, which CPU to list). Everything it shows comes
// in through Draw's arguments and everything it wants done goes out through Host, so it draws
// headlessly under ImGuiHarness with a hand-filled model.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "Debug/ProfilerModel.h"

namespace sfe
{

class ProfilerPanel
{
public:
    struct Host
    {
        bool live = false;            // a live emulator is the source
        bool hasCounters = false;     // ...and it produces profiler counters (SE_LIVE_CAP_PROFILER)
        bool canFunctions = false;    // ...and can attribute time to functions (a debugger build)
        // Display name of the function entered at 'address' ("sub_06004000" or the user's name).
        std::function<std::string(uint32_t address)> functionName;
        // Entry of the function containing 'pc', when the caller can find one.
        std::function<bool(uint32_t pc, uint32_t* entry)> functionContaining;
        std::function<void(int cpu, uint32_t address)> goToCode;      // show in SH-2 Assembly
        std::function<void(uint32_t address)> goToMemory;             // show in Memory
    };

    // Returns true if the user asked for the history to be cleared.
    bool Draw(const ProfilerModel& model, const MemoryPoolTracker& pools, const Host& host);

    // What the panel needs gathered for it, read by the owner every frame.
    bool WantsFunctions() const { return mFunctions; }    // keep function attribution on
    bool WantsPools() const { return mPoolsVisible; }     // the Memory tab is showing
    void SetWantsFunctions(bool on) { mFunctions = on; }
    // The panel was not drawn this frame (hidden or collapsed): nothing is showing.
    void NotDrawn() { mPoolsVisible = false; }

    size_t WindowFrames() const;

private:
    void DrawFrameTab(const ProfilerModel& model);
    void DrawBusTab(const ProfilerModel& model);
    void DrawMemoryTab(const MemoryPoolTracker& pools, const Host& host);
    void DrawFunctionsTab(const ProfilerModel& model, const Host& host);

    int  mWindow = 2;            // index into the averaging-window choices (1 second)
    bool mFunctions = false;
    bool mPoolsVisible = false;
    int  mFnCpu = 0;             // 0 both, 1 master, 2 slave
};

}  // namespace sfe
