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

namespace sfe
{

enum class LogCategory
{
    Info, Warning, Error, CPU, DMA, VDP, CD, Script, Tracepoint, Input, Count
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

    // Expandable detail rows (e.g. "r4" -> "182"), shown when the entry is selected.
    std::vector<std::pair<std::string, std::string>> detail;
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
    size_t Size() const { return mEvents.size(); }

private:
    void EnsureFilterInit();
    void PushEvent(LogEvent e);

    std::deque<LogEvent> mEvents;
    uint64_t             mSeq = 0;
    size_t               mCap = 5000;      // ring cap; oldest dropped past this

    bool     mShow[static_cast<int>(LogCategory::Count)];
    bool     mFilterInit = false;
    bool     mAutoScroll = true;
    char     mSearch[64] = {};
    uint64_t mSelected = 0;                // seq of the selected row (0 = none)
};

}  // namespace sfe
