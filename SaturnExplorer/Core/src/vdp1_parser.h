// Vdp1Parser — walks the VDP1 command table in VRAM into a flat list of
// se_command records. The VDP1 begins fetching command tables at VRAM offset 0
// and follows each entry's jump/link until an end command (CMDCTRL END bit) or
// a broken/looping link. Layout and bit fields per Docs/Saturn/VDP1.pdf ch.6.
#pragma once

#include <cstdint>
#include <vector>

#include "saturnexplorer/se_types.h"

namespace se
{

class Vdp1Parser
{
public:
    // Parse the command list out of VDP1 VRAM into 'out' (cleared first).
    static void Parse(const std::vector<uint8_t>& vdp1Vram,
                      std::vector<se_command>& out);
};

}  // namespace se
