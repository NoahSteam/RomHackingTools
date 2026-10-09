// The words for an emulator edit that did not (or may not have) reached the running game.
#pragma once

#include <cstdint>
#include <string>

namespace sfe
{

// 'refused' - the emulator received the poke and could not write it (no writer wired, no room).
// 'undelivered' - the poke was never sent (the connection dropped with it queued, or the server is
//                 too old for the verb).
// 'unconfirmed' - the poke was on the wire when the connection failed before the reply; the
//                 emulator may have applied it, and nothing here can tell. Said as uncertainty: a
//                 definite "it never arrived" for an edit that did would send the user looking for a
//                 bug that is not there.
// Empty when there is nothing to report.
inline std::string DescribePokeLoss(uint32_t refused, uint32_t undelivered, uint32_t unconfirmed)
{
    if (refused + undelivered + unconfirmed == 0) return std::string();
    std::string out = "Emulator edit status: ";
    bool first = true;
    auto add = [&](uint32_t n, const char* what)
    {
        if (!n) return;
        if (!first) out += "; ";
        first = false;
        out += std::to_string(n) + " " + what;
    };
    add(undelivered, "never reached the emulator");
    add(refused, "could not be written by it");
    add(unconfirmed, "could not be confirmed (the connection closed before the emulator replied)");
    if (undelivered + refused)
        out += ". The view still shows the edit; the emulator's memory does not have it";
    else
        out += ". The view still shows the edit; the emulator may or may not have applied it";
    out += unconfirmed && (undelivered + refused) ? " (apart from the unconfirmed ones)." : ".";
    return out;
}

}  // namespace sfe
