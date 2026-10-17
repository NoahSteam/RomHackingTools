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

// The register families of the width table (see "The width table" below): which of Mednafen's
// handlers serves a register decides what an access of each width does to it. Reference:
// sh7095.inc (the on-chip registers; the CPU's bus, where a long on a 16-bit bus is two accesses
// and a byte or word on the 32-bit buses merges into the last data on the bus), ss.c (SMPC, the
// signal region), scu.inc (the SCU's registers, the A bus, the B bus), vdp1.c, vdp2.c, cdb.c.
enum class NarrowAccess : uint8_t
{
    Part,            // the bytes it carries are written (SCU, SCSP); a read is a 32-bit bus read
    WholeOnByte,     // a byte write writes the whole 16-bit register; the other byte is the last on the data bus (VDP1, VDP2)
    ByteLane,        // a byte device on the low lane of the 16-bit bus: a word or long reaches it by lane, a byte at the even address does not (SMPC)
    OnChipLong,      // a 32-bit on-chip register (0xFFFFFF00 and up): a word write takes it whole at its own address and does
                     // nothing at +2, a word read returns the low half at both, a byte access is an address error
    OnChipKeyedLong, // ...and it takes no word write at all: a 4-byte write with 0xA55A in its upper half sets it (the bus state controller)
    OnChipDmac,      // the DMA controller (SAR0 .. DMAOR): as OnChipLong, but a word read at +2 reaches nothing and reads 0: sh7095.inc has no case at 0x82 .. 0xB2 (A5-18)
    OnChipWord,      // a 16-bit on-chip register at 0xFFFFFF00 and up (the UBC): a byte access is an address error
    OnChipByte,      // an 8-bit on-chip register below 0xFFFFFF00: a word reaches the even one only, a long is an address error
    OnChipHalf,      // a 16-bit on-chip register below 0xFFFFFF00: a long is an address error
    OnChipSci,       // the serial interface, which Mednafen does not decode: no width rule is claimed
    ScuPort,         // the SCU's DSP ports (PPAF, PPD, PDA, PDD): a byte or word write is not masked, it writes the whole register
    CdWindow,        // the CD block: one register every 4 bytes, both halves of a long reach it; its FIFO pops per 16-bit access
    Strobe,          // MINIT and SINIT: only a 16-bit write signals; a long signals twice
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
    bool noteWrite = false;          // ...and a write to it says so as a note, unless the value written is 1 (SMPC_SF: any write sets the flag to 1)
};

// Every register, sorted by address, none overlapping.
const HardwareRegister* SaturnHardwareRegisters(size_t& count);

// The canonical address 'address' reaches if it is in a register area: the cached and
// cache-through partitions, bits 28-27 (not decoded), and each chip's own mirror (SMPC every 128
// bytes, SCSP every 4 KiB, VDP2 every 512 bytes, SCU every 256 bytes, the CD block's 64-byte
// window, the CPU-to-CPU signals across their 8 MiB); on-chip, the whole of 0xE0000000-0xFFFFFFFF
// decoded by its low 9 bits, the divider's second copy at 0xFFFFFF20 and the watchdog's at
// 0xFFFFFE88. Mednafen, the emulator SE runs, decodes the partition that way (sh7095.inc MemRead and
// MemWrite case 7 pass the address to OnChipReg*, which look at bit 8 and the low byte only); the
// hardware manual (Docs/Saturn/SaturnMemory.txt) has the registers where bit 14 is set and reads
// garbage elsewhere, which a program meant for the console never relies on (A5 round 4: the
// emulator is the reference). False outside the register areas. The 0xA partition is not one:
// Mednafen sends it to the associative purge (see HardwareRanges).
bool HardwareAlias(uint32_t address, uint32_t& canonical);

// The register at 'address' through any alias, the address anywhere inside its width; or null.
const HardwareRegister* FindHardwareRegister(uint32_t address);

// The registers an access of 'size' bytes at 'address' touches, through any alias, in order.
std::vector<const HardwareRegister*> HardwareRegistersCovering(uint32_t address, uint32_t size);

// The SMPC's registers as a 16-bit access sees them: each byte register sits on the low lane of
// its bus word, so a word at the even address beside it reaches it (ss.c decodes (A & 0x7F) >> 1
// and drops the high lane). One entry per register, at the even address, 2 bytes wide, with the
// register's name: the decompiler symbols them too, so a word access prints as the register.
// Sorted by address; they overlap the table above and are not part of it. The word's other byte,
// the high lane, is no register (a byte there does nothing and reads 0xFF): the width table lists
// it as SMPC_UNUSED_<offset> but it is not a symbol -- the engine takes the smallest symbol at an
// address for an access of any width, so one beside the word view would print every word write as
// the unused lane. A byte at the even address therefore prints as the word view's high byte
// (SMPC_COMREG._0_1_), and the caution on the line says it is the unused lane (A5-17).
const HardwareRegister* SaturnHardwareWordViews(size_t& count);

// ---- The width table --------------------------------------------------------------------------
//
// For every register, each of the three access widths, both directions, and every aligned place
// the access can fall on it, what the hardware does -- taken from Mednafen's handlers for the
// register's area, one rule per family (NarrowAccess). The registers' own kind (write-only,
// read-only) is the access-kind table's, not repeated here. A cell is one of
//   (a) accepted as the code wrote it,
//   (b) the register is written or read whole, wider than asked, the other bytes from the bus,
//   (c) only one lane, one half or one register of the access reaches the register (and which
//       bits of the value),
//   (d) nothing happens,
//   (e) the CPU takes an address error,
//   (f) a side effect that differs from the plain access (a signal sent twice, a FIFO popped
//       twice, a command not started), and
//   (v) it depends on the value (the keyed writes); KeyedWrite says it.
// plus flags for the register's own read or write effect (a latch, a division, a flag cleared)
// and for the 32-bit bus reading the register 2 bytes on as well. The cautions are generated from
// the cells in one place (NarrowAccessEffects); WidthTable() lists every cell so a test can print
// the lot (FrontEnd/tests/HardwareWidthTableTests.cpp, golden file HardwareWidthTable.golden.txt).
enum class WidthRule : uint8_t
{
    // (a)
    Plain,            // the bytes it carries are read or written
    HalfOfLong,       // a long on a 16-bit register of a 16-bit bus: two accesses, the register takes one half, the other half is another register
    AccessKind,       // -: the register is not accessible this way at all (write-only read, read-only write); its kind says it
    // (b)
    ByteWritesWord,   // a byte write writes the 16-bit register whole (VDP1, VDP2)
    PortWritesWhole,  // a byte or word write to an SCU DSP port writes the 32-bit register whole
    NarrowReadWhole,  // a narrow read of an SCU DSP port is the whole 32-bit read, its effect in full
    WordWritesLong,   // a word write at +0 of a 32-bit on-chip register writes it whole, the upper half 0
    FifoPopsTwice,    // a narrow read of the CD FIFO in its 32-bit window pops two 16-bit words
    FifoByteWrite,    // a byte write to the CD FIFO pushes a whole 16-bit word into the transfer
    // (c)
    HalfBesideGap,    // a long on a 16-bit register whose other half is no register in the map
    SmpcLaneWrite,    // a word or long reaches an SMPC register by lane: its bits
    SmpcLaneRead,
    EvenByteWrite,    // a word write at an even on-chip byte register reaches that one only, with the low byte
    EvenByteRead,     // ...a read returns it in bits 7-0 and does not read the odd one
    LowHalfRead,      // a word read of a 32-bit on-chip register returns its low half at +0 and +2
    CdLongWrite,      // a long to a CD register is two 16-bit writes to it, the low half kept
    CdLongRead,       // a long read of a CD register reads it once and returns it in both halves
    // (d)
    SmpcPadWrite,     // a byte at the even SMPC address: no register, nothing happens
    SmpcPadRead,      // ...a read returns 0xFF
    OddByteWrite,     // a word at the even address beside an odd on-chip byte register does not reach it
    OddByteRead,
    LongHalfWrite,    // a word write at +2 of a 32-bit on-chip register does nothing
    LongHalfRead,     // a word read at +2 of a DMA controller register returns 0 and reads nothing (no case there)
    KeyedWordWrite,   // a word write to a keyed (bus state controller) register does nothing
    KeyedByteWrite,   // a byte write to a watchdog register does nothing, except that it cancels a pending RSTCSR WOVF clear
    StrobeByte,       // a byte write to MINIT/SINIT sends no signal
    MirrorWrite,      // the CCR's read mirrors take no write
    ReservedByteWrite,// a byte write to the low byte of IPRB or VCRD: the handler masks the store with 0xFF00 / 0x7F00, so nothing is stored (A5-30)
    // (e)
    AddressError,     // the SH-2 takes an address error
    // (f)
    StrobeLong,       // a long write to MINIT/SINIT signals twice
    CdByteWrite,      // a byte write to CD_CR4 does not start the command
    // (v)
    KeyedWrite,       // the value's key picks what happens: the watchdog's word writes, the bus state controller's long writes
    // not claimed
    Undecoded,        // Mednafen has no handler (the serial interface)
};

enum WidthFlag : uint8_t
{
    kWidthEffect = 1,       // the register's own onRead / onWrite effect happens with this access
    kWidthReadsNext = 2,    // the 32-bit bus also reads the register 2 bytes on (the B bus: SH0 at A, SH1 at A|2)
    kWidthReadsTwice = 4,   // ...or reads this one twice (A has bit 1 set, so A|2 is A)
};

struct WidthCell
{
    WidthRule rule = WidthRule::Plain;
    uint8_t flags = 0;
    uint8_t hi = 0, lo = 0;   // the bits of the value that reach the register, when the rule says (hi 0, lo 0: not applicable)
};

// The cell for an access of 'width' bytes (1, 2 or 4) that starts 'off' bytes from the register
// (negative: below it) at the bus address 'access' (any alias; the CD block's FIFO window and the
// CCR's mirrors are told apart by it).
WidthCell WidthCellFor(const HardwareRegister& reg, uint32_t width, bool write, int32_t off, uint32_t access);
char WidthCategory(WidthRule rule);            // 'a' to 'f', 'v', '-' (AccessKind), '?' (Undecoded)
const char* WidthRuleName(WidthRule rule);
const char* WidthRuleSource(WidthRule rule);   // the Mednafen handler it is read from
const char* WidthFamilyName(NarrowAccess family);

// Every cell, in address order: each register of the table (and each SMPC unused lane), each width
// and direction, each place an aligned access of that width overlaps it. The CD block's FIFO is
// listed in both of its windows.
struct WidthProbe
{
    const HardwareRegister* reg;
    uint32_t width;
    bool write;
    int32_t off;
    uint32_t access;   // the bus address the cell was asked for (cached partition)
    WidthCell cell;
};
std::vector<WidthProbe> WidthTable();

// The registers an access of 'size' bytes at 'address' touches, in order, each with its cell (the
// byte at an even SMPC address reaches the unused lane, which is listed). Empty where no register is.
struct CoveredRegister
{
    const HardwareRegister* reg = nullptr;
    WidthCell cell;
};
std::vector<CoveredRegister> CoveredRegisters(uint32_t address, uint32_t size, bool write);

// What an access of 'size' bytes at 'address' (a bus address, any alias) does that its width does
// not say, generated from the width table's cells: a byte write to a 16-bit VDP register writes
// the whole register, a word write to a 32-bit on-chip register takes it whole (or does nothing), a
// byte or long access to the wrong on-chip registers is an address error, a word at an SMPC register
// is a byte write to it, a long over two registers reaches each with its half. One entry per
// register and cell, in order; empty when the access is the plain access the code wrote. 'text'
// names the register and the width; 'caution' is false for what is only said (a long that the
// manuals document, a register the bus reads as well).
struct NarrowEffect
{
    const HardwareRegister* reg = nullptr;
    std::string text;
    bool caution = true;
};
std::vector<NarrowEffect> NarrowAccessEffects(uint32_t address, uint32_t size, bool write);

// The address an access of 'size' bytes at 'address' really reaches: a misaligned access is an
// address error and is made at the address rounded down to its width (sh7095.inc MemRead and
// MemWrite: A &= ~(sizeof(T) - 1)). The on-chip registers get the unaligned address (case 7 passes
// unmasked_A) and the two halves of the area treat it differently (A5-29): from 0xFFFFFF00 up the
// handlers align it themselves (A &= 0xFE, A &= 0xFC), the same address; below it they switch on
// A & 0xFF as given, so a word at 0xFFFFFE11 is the register at 0xFFFFFE11 (FTCSR).
uint32_t AccessAddress(uint32_t address, uint32_t size);

// The registers an access of 'size' bytes at 'address' really reaches (HardwareRegistersCovering of
// AccessAddress, except below 0xFFFFFF00 on-chip, where a word at an odd address and a long write reach
// the register at the access's own address only, and a long read reaches none: it returns 0). A5-29, A5-31.
std::vector<const HardwareRegister*> HardwareRegistersReached(uint32_t address, uint32_t size, bool write);

// What a write of this value does that the line does not say, for the registers whose effect
// depends on it (or on nothing but the write): a write to SH2_CCR with CP (bit 4) set purges the whole
// cache, a write to VDP1_PTMR with bit 0 set starts drawing, any write to VDP1_ENDR ends it, SCU_DnEN
// with bits 8 and 0 set starts that level's DMA (start factor 7), SCU_AIACK bit 0 acknowledges the A-bus
// interrupt, SCU_DSTP bit 0 stops every level, SCU_T1MD with bit 0 clear resets timer 0 (A5-32).
// 'valueKnown' false when the value is not a constant.
// One text per effect, each a note. Empty when the write has none.
std::vector<std::string> WriteEffects(uint32_t address, uint32_t size, bool valueKnown, uint32_t value);

// The caution for a write of 'size' bytes to a watchdog register (a keyed register: the byte, long
// and odd-address writes it does not take). Every write in 0xFFFFFE80-0xFFFFFE8F, the ignored ones
// too, cancels a pending SH2_RSTCSR WOVF clear (sh7095.inc: z->WDT.RSTCSRM = 0 in each case).
std::string KeyedPortCaution(const HardwareRegister& r, uint32_t size);

// The documented name of a 4-byte access that spans a register pair the manuals allow to be
// accessed whole (VDP2 scroll and table addresses, the UBC's halves); null otherwise.
const char* DocumentedWideAccess(uint32_t address, uint32_t size);

// A word write to the watchdog's write ports (FE80, FE82), which select their register by the
// value's upper byte, or a 4-byte write to the bus state controller (BCR1 to RTCOR), which takes
// effect only with 0xA55A in its upper half: true, with what it does in 'text', and 'ignored' when
// the key is wrong. 'valueKnown' false when the value is not a constant.
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

// ---- Ordinary memory that more than one address names ----------------------------------------------
//
// The engine's data flow keys memory by address, so a store through one name of a word is forwarded past a store
// through another name of the same word (A5-52: `xRam26001000 = 5; xRam06001000 = 7; return 5;`). The bus decodes
// bits 31-29 as the partition (0 cached and 1 cache-through reach memory alike) and ignores bits 28-27 (ss.c, and
// sh7095.inc MemRead / MemWrite case 0 and 1 pass A & 0x07FFFFFF), and each memory repeats inside its window:
// work RAM every 1 MiB (high 0x06000000-0x07FFFFFF; low 0x00200000-0x003FFFFF, which Mednafen answers in its lower
// MiB only and the hardware of some revisions mirrors, taken as repeating: the safe side), VDP2 VRAM every 512 KiB
// (vdp2.c RW_*: A & 0x7FFFF), the VDP1 frame buffer every 256 KiB (vdp1.c: FBA & 0x3FFFF). VDP2 colour RAM repeats
// every 4 KiB (cri = (A & 0xFFF) >> 1), and in colour RAM mode 0 an entry is also written 0x800 bytes on (the entry
// 0x400 up): the mode is a register the decompiler does not follow, so the bytes are taken modulo 0x800 (A5-53).
//
// The extended RAM cartridge (cart/extram.c, A5-65): 0x02400000-0x027FFFFF, `ExtRAM[A & ExtRAM_Mask]` with the mask 0x27FFFE for the 1 MiB
// cartridge and 0x3FFFFE for the 4 MiB one. The 1 MiB one decodes bit 21 (the bank) and bits 18-1, so each 2 MiB half of the window is a 512 KiB
// bank repeated four times (0x02480000 is 0x02400000, 0x02600000 is the other bank); the 4 MiB one decodes bits 21-1 and repeats nothing. The
// decompiler is not told which is plugged in: the 1 MiB repeat is taken, the safe side (volatile for nothing on a 4 MiB cartridge, never wrong).
enum class MemoryKind : uint8_t { WorkRamHigh, WorkRamLow, Vdp1Vram, Vdp1FrameBuffer, Vdp2Vram, Vdp2ColorRam, ExtRam };
struct MemoryBytes
{
    MemoryKind kind = MemoryKind::WorkRamHigh;
    uint32_t first = 0, last = 0;   // the bytes an access covers, as offsets into that memory after its repeats
};
// True when [address, address + size) is in one of these memories, through any partition and any repeat; 'out' is
// the bytes it covers. Two accesses that cover a byte of one memory in common are the same bytes whatever address
// each used.
bool MemoryBytesOf(uint32_t address, uint32_t size, MemoryBytes& out);
const char* MemoryKindName(MemoryKind kind);
// VDP2 colour RAM, through any partition (0x05F00000-0x05F7FFFF and its aliases): a byte write there stores the whole
// 16-bit entry, the byte not written being the last value on the data bus (vdp2.c RW_W8: `CRAM[..] = *DB`, no mask,
// where VRAM's is `(VRAM[vri] & ~mask) | (*DB & mask)`) (A5-50).
bool InColorRam(uint32_t address);

}  // namespace decomp
}  // namespace sfe
