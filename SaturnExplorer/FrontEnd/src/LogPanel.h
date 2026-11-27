// LogPanel — a structured, filterable event log (see EXECUTION_ACTIONS.md), not a
// scrolling text box. Each entry has a frame number, a category, a message, optional
// detail key/values, and an optional clickable source address (a tracepoint's
// instruction) that jumps the Assembly / Hex panels. System events (Connected,
// Loaded, warnings) and tracepoint fires both land here.
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "SeHwEvents.h"

namespace sfe
{

class Settings;

enum class LogCategory
{
    Info, Warning, Error, CPU, DMA, VDP, CD, SMPC, Script, Tracepoint, Input, Count
};

struct LogEvent
{
    uint64_t     seq = 0;                 // monotonic id (stable across filtering)
    uint32_t     frame = 0;
    std::string  time;                    // "HH:MM:SS.mmm" captured at push
    LogCategory  category = LogCategory::Info;
    std::string  message;

    bool         hasSource = false;       // a clickable instruction address
    int          cpu = 0;                 // 0 master, 1 slave (source)
    uint32_t     pc = 0;

    bool         pcApprox = false;        // the PC is where the CPU was when the event was noticed

    // Expandable detail rows (e.g. "r4" -> "182"), shown when the entry is selected.
    std::vector<std::pair<std::string, std::string>> detail;

    // Hardware stream rows (the budget below counts these apart from tracepoint/Info rows).
    // A record is kept raw and decoded into detail rows only when selected: at tens of thousands
    // of rows a vector of string pairs per row would dominate the memory for text nobody reads.
    bool         fromHw = false;
    bool         hasRecord = false;
    SeHwEvent    hw = SeHwEvent();
};

// What the panel needs to know about the connection to enable the hardware chips.
struct HwChipStatus
{
    bool     live = false;           // the data source is a live emulator
    bool     capable = false;        // the server has SE_LIVE_CAP_HW_EVENTS
    uint32_t applied = 0;            // the mask the emulator runs with (0 until a section arrives)
    uint32_t supported = 0xFFu;      // SE_HWEV_EN_* bits this emulator build has hooks for
    bool     statusValid = false;    // applied/supported came from a real section
};

class LogPanel
{
public:
    // What the App should act on after a Draw (a jump requested by a clicked entry).
    struct Request
    {
        bool     jumpAssembly = false;
        int      jumpCpu = 0;
        uint32_t jumpAddr = 0;
        bool     jumpHex = false;
        uint32_t hexAddr = 0;
    };

    void Draw(Request& req);

    // Hardware capture. The chips CPU/DMA/VDP/CD/SMPC arm sub-sources (SE_HWEV_EN_*); the union is
    // what the App asks the emulator for, every frame. The stored choice is independent of whether
    // the connection can honour it.
    uint32_t HwRequestMask() const { return mHwArmed; }
    void SetHwStatus(const HwChipStatus& s) { mHw = s; }
    // Settings section "Log", one key per sub-source.
    void Load(const Settings& s);
    void Store(Settings& s) const;
    bool ConsumeSettingsDirty() { const bool d = mSettingsDirty; mSettingsDirty = false; return d; }

    // Append a drain batch under one timestamp (one clock read, not one per event).
    void PushBatch(std::vector<LogEvent>& events);

    // For tests: what the last Draw submitted as rows, and where it put each chip.
    const std::vector<const LogEvent*>& LastSubmitted() const { return mLastSubmitted; }
    size_t FilteredCount() { RebuildIfDirty(); return mIndex.size(); }
    bool ChipRect(LogCategory c, float* minX, float* minY, float* maxX, float* maxY) const;
    bool Selected(uint64_t* seq) const { if (seq) *seq = mSelected; return mSelected != 0; }

    // Row budget: 50000 rows in all, split so a flood of hardware events cannot push out the
    // tracepoint/Info rows (the oldest hardware row is always the one to go).
    static constexpr size_t kHwCap = 40000;
    static constexpr size_t kOtherCap = 10000;

    // Append an event (source/detail default empty). Convenience wrappers below.
    void Push(LogCategory cat, const std::string& message, uint32_t frame = 0);
    void Info(const std::string& m, uint32_t frame = 0)    { Push(LogCategory::Info, m, frame); }
    void Warn(const std::string& m, uint32_t frame = 0)    { Push(LogCategory::Warning, m, frame); }
    void Error(const std::string& m, uint32_t frame = 0)   { Push(LogCategory::Error, m, frame); }

    // A tracepoint fired: message is the already-formatted output; source jumps to pc.
    void Tracepoint(uint32_t frame, int cpu, uint32_t pc, const std::string& message,
                    std::vector<std::pair<std::string, std::string>> detail);

    void Clear();

    // Visibility of one category's filter chip. Exposed so another panel can offer a
    // shortcut to the same filter rather than keeping a second, divergent toggle.
    bool& CategoryVisible(LogCategory c);
    size_t Size() const { return mEvents.size() + mHwEvents.size(); }

private:
    void EnsureFilterInit();
    void PushEvent(LogEvent e, const std::string& time);
    void RebuildIfDirty();
    const LogEvent* Find(uint64_t seq) const;
    void DrawChips();

    std::deque<LogEvent> mEvents;          // everything but the hardware stream
    std::deque<LogEvent> mHwEvents;        // hardware stream; each deque is ordered by seq
    uint64_t             mSeq = 0;
    std::vector<const LogEvent*> mIndex;   // rows that pass the filters, in seq order
    bool                 mDirty = true;
    std::string          mIndexSearch;     // the search text mIndex was built for
    bool                 mIndexShow[static_cast<int>(LogCategory::Count)] = {};
    std::vector<const LogEvent*> mLastSubmitted;

    uint32_t             mHwArmed = 0;     // SE_HWEV_EN_* the user wants captured
    HwChipStatus         mHw;
    bool                 mSettingsDirty = false;
    float                mChipRect[static_cast<int>(LogCategory::Count)][4] = {};
    bool                 mChipDrawn[static_cast<int>(LogCategory::Count)] = {};

    bool     mShow[static_cast<int>(LogCategory::Count)];
    bool     mFilterInit = false;
    bool     mAutoScroll = true;
    char     mSearch[64] = {};
    uint64_t mSelected = 0;                // seq of the selected row (0 = none)
};

}  // namespace sfe
