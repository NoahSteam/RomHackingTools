// The decisions App makes around the pseudocode panel's captured inspection, kept free of App so
// tests can exercise them: when a view may be inspected, and how the panel's requests are routed.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

namespace sfe {

struct InspectionReadiness
{
    bool ready = true;
    const char* reason = "";
};

// A recorded frame or a static dump is its own capture and stays inspectable whatever the live
// emulator is doing. A live view is not while a state load is unresolved: a load still being
// applied, or one the emulator cannot confirm (a timeout does not clear that), leaves the captured
// state undetermined.
inline InspectionReadiness InspectionReadinessFor(bool liveSource, int restoreOutstanding, bool restoreUnconfirmable)
{
    InspectionReadiness r;
    if (liveSource && (restoreOutstanding > 0 || restoreUnconfirmable))
    {
        r.ready = false;
        r.reason = "A restore is unresolved; captured values are unavailable.";
    }
    return r;
}

// "Open Registers" selects the pseudocode panel's CPU; anything else is the master.
inline int RegistersWindowCpu(int panelCpu) { return panelCpu == 1 ? 1 : 0; }

// A fixed-address Watch is added by the same bare-hex expression the other Add-to-Watch routes use.
inline std::string WatchExpressionFor(uint32_t canonicalAddress)
{
    char text[16];
    std::snprintf(text, sizeof text, "%08X", canonicalAddress);
    return text;
}

}  // namespace sfe
