// WatchPanel — the Watch Window UI. Owns the WatchList, renders the toolbar +
// table, drives the ~10 Hz batched refresh through an IMemoryBackend, and does
// inline editing, change highlighting, context menu, and JSON import/export.
// Emulator-agnostic: it talks to IMemoryBackend / IExpressionResolver only.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "imgui.h"

#include "Debug/WatchList.h"
#include "Debug/MemoryBackend.h"
#include "Platform/IPlatform.h"

namespace sfe
{

class WatchPanel
{
public:
    WatchPanel();

    // Draw the "Watch" window. 'dt' is the frame delta (seconds) for the refresh
    // timer + highlight fade. Reads values via 'backend', resolves expressions via
    // 'resolver', and uses 'platform' for import/export file dialogs.
    void Draw(IMemoryBackend& backend, IExpressionResolver& resolver,
              IPlatform& platform, float dt);

    // Session persistence: load/save the watch list to a file in the working dir.
    void LoadSession();
    void SaveSession() const;

private:
    struct Row
    {
        bool        seeded = false;
        char        nameBuf[128] = {};
        char        exprBuf[64] = {};
        // Resolution + last read.
        bool        resolved = false;
        uint32_t    address = 0;
        std::string error;
        WatchValue  value;
        bool        hasValue = false;
        // Change highlight.
        bool        hasLast = false;
        long long   lastNumeric = 0;
        float       highlight = 0.0f;     // seconds remaining (fades 1 -> 0)
        ImU32       highlightCol = 0;
    };

    void Refresh(IMemoryBackend& backend, IExpressionResolver& resolver);
    Row& RowFor(const WatchEntry& e);
    void DoImport(IPlatform& platform);
    void DoExport(IPlatform& platform);

    WatchList mList;
    std::unordered_map<uint64_t, Row> mRows;   // by entry id
    float     mRefreshAccum = 0.0f;
    float     mRefreshHz = 10.0f;
    int       mContextRow = -1;                // row index whose menu is open
    int       mFocusNameRow = -1;              // "Edit" jumped focus here
    std::string mNotice;                        // one-line status (import/export)
    float     mNoticeTime = 0.0f;
};

}  // namespace sfe
