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
#include <string>
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
    KeyedWrite,  // read here; written only by a keyed word at its write port (the watchdog)
};

// What an access narrower than a register does where the bus does not simply take the bytes it
// carries -- the access-kind table's second axis; NarrowAccessEffects says it. Mednafen is the
// reference: sh7095.inc (on-chip), ss.c (SMPC), vdp1.c and vdp2.c.
enum class NarrowAccess : uint8_t
{
    Part,            // the bytes it carries are written or read (SCU, SCSP, the CD block, byte registers)
    WholeOnByte,     // a byte write writes the whole 16-bit register; the other byte is the last on the data bus (VDP1, VDP2)
    ByteLane,        // a byte device on the low lane of the 16-bit bus: a word or long reaches it by lane, a byte at the even address does not (SMPC)
    OnChipLong,      // a 32-bit on-chip register (0xFFFFFF00 and up): a word write takes it whole at its own address and does
                     // nothing at +2, a word read returns the low half at both, a byte access is an address error
    OnChipKeyedLong, // ...and it takes no word write at all: a 4-byte write with 0xA55A in its upper half sets it (the bus state controller)
    OnChipWord,      // a 16-bit on-chip register at 0xFFFFFF00 and up: a byte access is an address error
};

struct HardwareRegister
{
    uint32_t address;   // canonical: a cached-partition bus address, or on-chip at 0xFFFFFE00 and up
    uint8_t size;       // the documented access width in bytes: 1, 2 or 4
    const char* name;   // "VDP2_TVMD"
    RegisterAccess access;
    // Set when a read and a write reach different fields (a command and its response, a write
    // that clears flags, a port's output and its pins): what each does. Reading it back after a
    // write does not return what was written, though the read means something.
    const char* split;
    const char* onRead;   // set when a read does more than return a value (a FIFO pop, a latch)
    const char* about;    // set for a register only the emulator has: said of every access
    NarrowAccess narrow = NarrowAccess::Part;
    const char* onWrite = nullptr;   // set when a write does more than store a value (a division starts)
};

// Every register, sorted by address, none overlapping.
const HardwareRegister* SaturnHardwareRegisters(size_t& count);

// The canonical address 'address' reaches if it is in a register area: the cached and
// cache-through partitions, bits 28-27 (not decoded), and each chip's own mirror (SMPC every 128
// bytes, SCSP every 4 KiB, VDP2 every 512 bytes, SCU every 256 bytes, the CD block's 64-byte
// window, the CPU-to-CPU signals across their 8 MiB); on-chip, the SH-2's partial decode of
// 0xE0000000-0xFFFFFFFF, the divider's second copy at 0xFFFFFF20 and the watchdog's at 0xFFFFFE88. False outside them, or where
// the bus returns garbage rather than a register (Docs/Saturn/SaturnMemory.txt). The 0xA
// partition is not one: Mednafen sends it to the associative purge (see HardwareRanges).
bool HardwareAlias(uint32_t address, uint32_t& canonical);

// The register at 'address' through any alias, the address anywhere inside its width; or null.
const HardwareRegister* FindHardwareRegister(uint32_t address);

// The registers an access of 'size' bytes at 'address' touches, through any alias, in order.
std::vector<const HardwareRegister*> HardwareRegistersCovering(uint32_t address, uint32_t size);

// The SMPC's registers as a 16-bit access sees them: each byte register sits on the low lane of
// its bus word, so a word at the even address beside it reaches it (ss.c decodes (A & 0x7F) >> 1
// and drops the high lane). One entry per register, at the even address, 2 bytes wide, with the
// register's name: the decompiler symbols them too, so a word access prints as the register.
// Sorted by address; they overlap the table above and are not part of it.
const HardwareRegister* SaturnHardwareWordViews(size_t& count);

// What an access of 'size' bytes at 'address' (a bus address, any alias) does to the registers
// it reaches that its width does not say, by their NarrowAccess kind: a byte write to a 16-bit
// VDP register writes the whole register, a word write to a 32-bit on-chip register takes it
// whole (or does nothing), a byte access to one is an address error, a word at an SMPC register
// is a byte write to it. One entry per register, in order; empty when the access is the plain
// access the code wrote. 'text' is a caution naming the register and the width. A byte at an
// even SMPC address reaches no register and does nothing: it is reported against the register
// beside it.
struct NarrowEffect
{
    const HardwareRegister* reg = nullptr;
    std::string text;
};
std::vector<NarrowEffect> NarrowAccessEffects(uint32_t address, uint32_t size, bool write);

// The documented name of a 4-byte access that spans a register pair the manuals allow to be
// accessed whole (VDP2 scroll and table addresses, the UBC's halves); null otherwise.
const char* DocumentedWideAccess(uint32_t address, uint32_t size);

// A word write to the watchdog's write ports (FE80, FE82), which select their register by the
// value's upper byte: true, with what it does in 'text', and 'ignored' when the key is wrong.
// 'valueKnown' false when the value is not a constant.
bool KeyedWrite(uint32_t address, uint32_t size, bool valueKnown, uint32_t value, std::string& text, bool& ignored);

// Inclusive address ranges where every access goes to hardware, or to memory another processor
// always shares (sound RAM), every alias included.
struct HardwareRange
{
    uint32_t first;
    uint32_t last;
};
const std::vector<HardwareRange>& HardwareRanges();

// Every range a user's volatile memory [first, last] is reached through: widened to whole
// longwords (an access touching the range starts in it), then on the bus both partitions, bits
// 28-27 and the 1 MiB repeats of high work RAM (0x06000000-0x07FFFFFF) and low work RAM
// (0x00200000-0x003FFFFF). Empty for an inverted range (last < first), which the
// caller is expected to have rejected (ParseAddressRanges does).
std::vector<HardwareRange> VolatileAliases(uint32_t first, uint32_t last);
bool InHardwareRange(uint32_t address);

}  // namespace decomp
}  // namespace sfe
