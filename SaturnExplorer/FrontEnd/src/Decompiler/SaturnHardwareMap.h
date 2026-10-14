// SaturnHardwareMap — the Saturn's memory-mapped hardware registers for the decompiler: their
// names, widths and access kinds, and the address ranges where nothing is memory.
//
// Game code reaches VDP1, VDP2, the SCU, the SMPC, the SCSP, the CD block and the SH-2's own
// peripherals through pointers loaded from literal pools, so once the literal folds the engine
// prints "*(ushort *)0x25f80000". Registering a global symbol at each register's address makes
// that "VDP2_TVMD" instead (PLAN.md Step 4).
//
// A name alone is not enough: a named global is an ordinary variable to the engine, so a write
// followed by a read of the same register folds to the written value, a poll becomes an endless
// loop, a write that is overwritten disappears and a read nobody uses is removed. Every access to
// a register has to stay where the code put it. So the decompiler marks every register symbol
// volatile, and every range below volatile too: those cover accesses wider than one register,
// unnamed addresses (SCSP slots, the FIFOs) and every alias the bus decodes to the same register.
// A symbol's own flags win over a range's (Scope::queryProperties), which is why both are needed.
//
// Names are the register names of Sega's and Hitachi's manuals (the ones SBL/SGL headers use),
// prefixed with the chip. Bus registers are named at their cached (0x0xxxxxxx) and their
// cache-through (0x2xxxxxxx) address; the SH-2's own registers at 0xFFFFFE00-0xFFFFFFFF.
// No Ghidra, no ImGui: tables and lookups, testable on their own.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sfe
{
namespace decomp
{

// What the hardware does with an access. Registers that clear flags or start work on access
// need no kind of their own: every access to every register is kept as it is.
enum class RegisterAccess : uint8_t
{
    ReadWrite,
    ReadOnly,    // a write does nothing
    WriteOnly,   // a read does not return what was written
};

struct HardwareRegister
{
    uint32_t address;   // canonical: a cached-partition bus address, or 0xFFFFFExxx on-chip
    uint8_t size;       // the documented access width in bytes: 1, 2 or 4
    const char* name;   // "VDP2_TVMD"
    RegisterAccess access;
};

// Every register, sorted by address, none overlapping.
const HardwareRegister* SaturnHardwareRegisters(size_t& count);

// The canonical address 'address' reaches if it is in a register area: the cached, cache-through
// and 0xA partitions, bits 28-27 (not decoded), and each chip's own mirror (SMPC every 128 bytes,
// SCSP every 4 KiB, VDP2 every 512 bytes, SCU every 256 bytes, the CPU-to-CPU signals across their
// 8 MiB); on-chip, the SH-2's partial decode of 0xE0000000-0xFFFFFFFF. False outside them, or where
// the bus returns garbage rather than a register (Docs/Saturn/SaturnMemory.txt).
bool HardwareAlias(uint32_t address, uint32_t& canonical);

// The register at 'address' through any alias, the address anywhere inside its width; or null.
const HardwareRegister* FindHardwareRegister(uint32_t address);

// The registers an access of 'size' bytes at 'address' touches, through any alias, in order.
std::vector<const HardwareRegister*> HardwareRegistersCovering(uint32_t address, uint32_t size);

// Inclusive address ranges where every access goes to hardware, every alias included.
struct HardwareRange
{
    uint32_t first;
    uint32_t last;
};
const std::vector<HardwareRange>& HardwareRanges();
bool InHardwareRange(uint32_t address);

}  // namespace decomp
}  // namespace sfe
