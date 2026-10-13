// SaturnHardwareMap — the Saturn's memory-mapped hardware registers, by name, for the decompiler.
//
// Game code reaches VDP1, VDP2, the SCU, the SMPC and the SCSP through pointers loaded from
// literal pools, so once the literal folds the engine prints "*(ushort *)0x25f80000". Registering
// a global symbol at each register's address makes that "VDP2_TVMD" instead (PLAN.md Step 4).
//
// Names are the register names of Sega's hardware manuals (the ones SBL/SGL headers use),
// prefixed with the chip. Each register is registered at its cached (0x0xxxxxxx) and its
// cache-through (0x2xxxxxxx) address, because code uses either and the engine resolves a
// symbol only at the exact address the code computes.
//
// The map names memory the snapshot does not hold, so nothing here folds: reads stay reads.
// No Ghidra, no ImGui: a table and a lookup, testable on its own.
#pragma once

#include <cstddef>
#include <cstdint>

namespace sfe
{
namespace decomp
{

struct HardwareRegister
{
    uint32_t address;   // canonical (cached partition) bus address
    uint8_t size;       // access width in bytes: 1, 2 or 4
    const char* name;   // "VDP2_TVMD"
};

// Every register, sorted by address, none overlapping.
const HardwareRegister* SaturnHardwareRegisters(size_t& count);

// The register covering 'address' (either partition), or null.
const HardwareRegister* FindHardwareRegister(uint32_t address);

}  // namespace decomp
}  // namespace sfe
