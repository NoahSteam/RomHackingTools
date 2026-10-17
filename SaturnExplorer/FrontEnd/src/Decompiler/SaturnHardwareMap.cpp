#include "Decompiler/SaturnHardwareMap.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <iterator>

namespace sfe
{
namespace decomp
{

namespace
{
// printf into a std::string of the size the text needs. The cautions are formatted with this and never into a fixed buffer: two of them were
// cut off in the middle of a word (A5-59) by buffers a few characters short, which no oracle sees (they match prefixes).
#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
std::string Fmt(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    va_list again;
    va_copy(again, args);
    const int n = std::vsnprintf(nullptr, 0, format, args);
    va_end(args);
    std::string out;
    if (n > 0)
    {
        out.resize(static_cast<size_t>(n) + 1);
        std::vsnprintf(&out[0], out.size(), format, again);
        out.resize(static_cast<size_t>(n));
    }
    va_end(again);
    return out;
}

const RegisterAccess RW = RegisterAccess::ReadWrite;
const RegisterAccess RO = RegisterAccess::ReadOnly;
const RegisterAccess WO = RegisterAccess::WriteOnly;
const RegisterAccess KW = RegisterAccess::KeyedWrite;

struct Reg
{
    uint16_t offset;
    const char* name;
    RegisterAccess access;
    uint8_t size;         // 0: the block's
    const char* split;    // reads and writes reach different fields: what each does
    const char* onRead;   // what a read does besides returning a value
    const char* about;    // said of every access: a register only the emulator has
    const char* onWrite;  // a write does more than store a value
    bool noteWrite = false;   // ...and every write to it says so
};

struct Block
{
    uint32_t base;
    uint8_t size;
    const Reg* regs;
    size_t count;
    NarrowAccess narrow;   // what a narrower access does to the block's registers (see Build for the on-chip split)
};

// SMPC: byte registers at odd addresses (Sega "SMPC User's Manual"; Mednafen smpc.cpp: IREGs,
// COMREG, DDRs, IOSEL and EXLE are only written, OREGs and SR only read, SF and PDRs both).
const Reg kSmpc[] = {
    { 0x001, "SMPC_IREG0", WO }, { 0x003, "SMPC_IREG1", WO }, { 0x005, "SMPC_IREG2", WO },
    { 0x007, "SMPC_IREG3", WO }, { 0x009, "SMPC_IREG4", WO }, { 0x00B, "SMPC_IREG5", WO },
    { 0x00D, "SMPC_IREG6", WO }, { 0x01F, "SMPC_COMREG", WO }, { 0x021, "SMPC_OREG0", RO },
    { 0x023, "SMPC_OREG1", RO }, { 0x025, "SMPC_OREG2", RO }, { 0x027, "SMPC_OREG3", RO },
    { 0x029, "SMPC_OREG4", RO }, { 0x02B, "SMPC_OREG5", RO }, { 0x02D, "SMPC_OREG6", RO },
    { 0x02F, "SMPC_OREG7", RO }, { 0x031, "SMPC_OREG8", RO }, { 0x033, "SMPC_OREG9", RO },
    { 0x035, "SMPC_OREG10", RO }, { 0x037, "SMPC_OREG11", RO }, { 0x039, "SMPC_OREG12", RO },
    { 0x03B, "SMPC_OREG13", RO }, { 0x03D, "SMPC_OREG14", RO }, { 0x03F, "SMPC_OREG15", RO },
    { 0x041, "SMPC_OREG16", RO }, { 0x043, "SMPC_OREG17", RO }, { 0x045, "SMPC_OREG18", RO },
    { 0x047, "SMPC_OREG19", RO }, { 0x049, "SMPC_OREG20", RO }, { 0x04B, "SMPC_OREG21", RO },
    { 0x04D, "SMPC_OREG22", RO }, { 0x04F, "SMPC_OREG23", RO }, { 0x051, "SMPC_OREG24", RO },
    { 0x053, "SMPC_OREG25", RO }, { 0x055, "SMPC_OREG26", RO }, { 0x057, "SMPC_OREG27", RO },
    { 0x059, "SMPC_OREG28", RO }, { 0x05B, "SMPC_OREG29", RO }, { 0x05D, "SMPC_OREG30", RO },
    { 0x05F, "SMPC_OREG31", RO }, { 0x061, "SMPC_SR", RO }, { 0x063, "SMPC_SF", RW, 0, nullptr, nullptr, nullptr,
      "a write sets SF to 1 whatever is written (the SMPC clears it when its command ends)", true },
    { 0x075, "SMPC_PDR1", RW, 0, "a write sets the port's output bits; a read returns its pin levels" },
    { 0x077, "SMPC_PDR2", RW, 0, "a write sets the port's output bits; a read returns its pin levels" },
    { 0x079, "SMPC_DDR1", WO },
    { 0x07B, "SMPC_DDR2", WO }, { 0x07D, "SMPC_IOSEL", WO }, { 0x07F, "SMPC_EXLE", WO },
};

// SCSP common control registers, named after the fields they hold ("SCSP User's Manual"). The
// per-slot block below 0x400 is left unnamed: 32 copies of one layout read better as offsets.
// Access per Mednafen scsp.inc, which reads 0 from every write-only field. Where the English
// manual's labels disagree with its own descriptions, the emulator decides; MCIEB is write-only
// there although the manual pairs it with the readable SCIEB.
const Reg kScsp[] = {
    { 0x400, "SCSP_MVOL", WO }, { 0x402, "SCSP_RBL_RBP", WO },
    { 0x404, "SCSP_MIBUF", RO, 0, nullptr, "a read takes the next MIDI input byte off the FIFO" },
    { 0x406, "SCSP_MOBUF", WO },
    { 0x408, "SCSP_MSLC_CA", RW, 0, "a write selects the monitored slot (MSLC); a read returns that slot's CA, SGC and EG" },
    { 0x412, "SCSP_DMEAL", WO }, { 0x414, "SCSP_DMEAH_DRGA", WO },
    { 0x416, "SCSP_DTLG", RW, 0, "a write sets DTLG and EX, DI, GA; a read returns only EX, DI and GA" },
    { 0x418, "SCSP_TIMA", WO }, { 0x41A, "SCSP_TIMB", WO }, { 0x41C, "SCSP_TIMC", WO },
    { 0x41E, "SCSP_SCIEB", RW },
    { 0x420, "SCSP_SCIPD", RW, 0, "a read returns the pending interrupts; a write of bit 5 only requests one" },
    { 0x422, "SCSP_SCIRE", WO }, { 0x424, "SCSP_SCILV0", WO }, { 0x426, "SCSP_SCILV1", WO },
    { 0x428, "SCSP_SCILV2", WO }, { 0x42A, "SCSP_MCIEB", WO },
    { 0x42C, "SCSP_MCIPD", RW, 0, "a read returns the pending interrupts; a write of bit 5 only requests one" },
    { 0x42E, "SCSP_MCIRE", WO },
};

// VDP1 ("VDP1 User's Manual" ch. 4: the control registers are write-only, the status ones
// read-only).
const Reg kVdp1[] = {
    { 0x000, "VDP1_TVMR", WO }, { 0x002, "VDP1_FBCR", WO }, { 0x004, "VDP1_PTMR", WO },
    { 0x006, "VDP1_EWDR", WO }, { 0x008, "VDP1_EWLR", WO }, { 0x00A, "VDP1_EWRR", WO },
    { 0x00C, "VDP1_ENDR", WO }, { 0x010, "VDP1_EDSR", RO }, { 0x012, "VDP1_LOPR", RO },
    { 0x014, "VDP1_COPR", RO }, { 0x016, "VDP1_MODR", RO },
};

// VDP2 ("VDP2 User's Manual"; 0x00C and 0x0FE are reserved). Only the first eight words read
// back (TVSTAT, HCNT and VCNT are status); everything from CYCA0L up is write-only (the manual,
// register by register; Mednafen vdp2.cpp RegsRead).
const Reg kVdp2[] = {
    { 0x000, "VDP2_TVMD", RW }, { 0x002, "VDP2_EXTEN", RW, 0, nullptr, "a read latches HCNT and VCNT while EXLTEN is 0" },
    { 0x004, "VDP2_TVSTAT", RO, 0, nullptr, "a read clears EXLTFG" },
    { 0x006, "VDP2_VRSIZE", RW }, { 0x008, "VDP2_HCNT", RO }, { 0x00A, "VDP2_VCNT", RO },
    { 0x00E, "VDP2_RAMCTL", RW }, { 0x010, "VDP2_CYCA0L", WO }, { 0x012, "VDP2_CYCA0U", WO },
    { 0x014, "VDP2_CYCA1L", WO }, { 0x016, "VDP2_CYCA1U", WO }, { 0x018, "VDP2_CYCB0L", WO },
    { 0x01A, "VDP2_CYCB0U", WO }, { 0x01C, "VDP2_CYCB1L", WO }, { 0x01E, "VDP2_CYCB1U", WO },
    { 0x020, "VDP2_BGON", WO }, { 0x022, "VDP2_MZCTL", WO }, { 0x024, "VDP2_SFSEL", WO },
    { 0x026, "VDP2_SFCODE", WO }, { 0x028, "VDP2_CHCTLA", WO }, { 0x02A, "VDP2_CHCTLB", WO },
    { 0x02C, "VDP2_BMPNA", WO }, { 0x02E, "VDP2_BMPNB", WO }, { 0x030, "VDP2_PNCN0", WO },
    { 0x032, "VDP2_PNCN1", WO }, { 0x034, "VDP2_PNCN2", WO }, { 0x036, "VDP2_PNCN3", WO },
    { 0x038, "VDP2_PNCR", WO }, { 0x03A, "VDP2_PLSZ", WO }, { 0x03C, "VDP2_MPOFN", WO },
    { 0x03E, "VDP2_MPOFR", WO }, { 0x040, "VDP2_MPABN0", WO }, { 0x042, "VDP2_MPCDN0", WO },
    { 0x044, "VDP2_MPABN1", WO }, { 0x046, "VDP2_MPCDN1", WO }, { 0x048, "VDP2_MPABN2", WO },
    { 0x04A, "VDP2_MPCDN2", WO }, { 0x04C, "VDP2_MPABN3", WO }, { 0x04E, "VDP2_MPCDN3", WO },
    { 0x050, "VDP2_MPABRA", WO }, { 0x052, "VDP2_MPCDRA", WO }, { 0x054, "VDP2_MPEFRA", WO },
    { 0x056, "VDP2_MPGHRA", WO }, { 0x058, "VDP2_MPIJRA", WO }, { 0x05A, "VDP2_MPKLRA", WO },
    { 0x05C, "VDP2_MPMNRA", WO }, { 0x05E, "VDP2_MPOPRA", WO }, { 0x060, "VDP2_MPABRB", WO },
    { 0x062, "VDP2_MPCDRB", WO }, { 0x064, "VDP2_MPEFRB", WO }, { 0x066, "VDP2_MPGHRB", WO },
    { 0x068, "VDP2_MPIJRB", WO }, { 0x06A, "VDP2_MPKLRB", WO }, { 0x06C, "VDP2_MPMNRB", WO },
    { 0x06E, "VDP2_MPOPRB", WO }, { 0x070, "VDP2_SCXIN0", WO }, { 0x072, "VDP2_SCXDN0", WO },
    { 0x074, "VDP2_SCYIN0", WO }, { 0x076, "VDP2_SCYDN0", WO }, { 0x078, "VDP2_ZMXIN0", WO },
    { 0x07A, "VDP2_ZMXDN0", WO }, { 0x07C, "VDP2_ZMYIN0", WO }, { 0x07E, "VDP2_ZMYDN0", WO },
    { 0x080, "VDP2_SCXIN1", WO }, { 0x082, "VDP2_SCXDN1", WO }, { 0x084, "VDP2_SCYIN1", WO },
    { 0x086, "VDP2_SCYDN1", WO }, { 0x088, "VDP2_ZMXIN1", WO }, { 0x08A, "VDP2_ZMXDN1", WO },
    { 0x08C, "VDP2_ZMYIN1", WO }, { 0x08E, "VDP2_ZMYDN1", WO }, { 0x090, "VDP2_SCXN2", WO },
    { 0x092, "VDP2_SCYN2", WO }, { 0x094, "VDP2_SCXN3", WO }, { 0x096, "VDP2_SCYN3", WO },
    { 0x098, "VDP2_ZMCTL", WO }, { 0x09A, "VDP2_SCRCTL", WO }, { 0x09C, "VDP2_VCSTAU", WO },
    { 0x09E, "VDP2_VCSTAL", WO }, { 0x0A0, "VDP2_LSTA0U", WO }, { 0x0A2, "VDP2_LSTA0L", WO },
    { 0x0A4, "VDP2_LSTA1U", WO }, { 0x0A6, "VDP2_LSTA1L", WO }, { 0x0A8, "VDP2_LCTAU", WO },
    { 0x0AA, "VDP2_LCTAL", WO }, { 0x0AC, "VDP2_BKTAU", WO }, { 0x0AE, "VDP2_BKTAL", WO },
    { 0x0B0, "VDP2_RPMD", WO }, { 0x0B2, "VDP2_RPRCTL", WO }, { 0x0B4, "VDP2_KTCTL", WO },
    { 0x0B6, "VDP2_KTAOF", WO }, { 0x0B8, "VDP2_OVPNRA", WO }, { 0x0BA, "VDP2_OVPNRB", WO },
    { 0x0BC, "VDP2_RPTAU", WO }, { 0x0BE, "VDP2_RPTAL", WO }, { 0x0C0, "VDP2_WPSX0", WO },
    { 0x0C2, "VDP2_WPSY0", WO }, { 0x0C4, "VDP2_WPEX0", WO }, { 0x0C6, "VDP2_WPEY0", WO },
    { 0x0C8, "VDP2_WPSX1", WO }, { 0x0CA, "VDP2_WPSY1", WO }, { 0x0CC, "VDP2_WPEX1", WO },
    { 0x0CE, "VDP2_WPEY1", WO }, { 0x0D0, "VDP2_WCTLA", WO }, { 0x0D2, "VDP2_WCTLB", WO },
    { 0x0D4, "VDP2_WCTLC", WO }, { 0x0D6, "VDP2_WCTLD", WO }, { 0x0D8, "VDP2_LWTA0U", WO },
    { 0x0DA, "VDP2_LWTA0L", WO }, { 0x0DC, "VDP2_LWTA1U", WO }, { 0x0DE, "VDP2_LWTA1L", WO },
    { 0x0E0, "VDP2_SPCTL", WO }, { 0x0E2, "VDP2_SDCTL", WO }, { 0x0E4, "VDP2_CRAOFA", WO },
    { 0x0E6, "VDP2_CRAOFB", WO }, { 0x0E8, "VDP2_LNCLEN", WO }, { 0x0EA, "VDP2_SFPRMD", WO },
    { 0x0EC, "VDP2_CCCTL", WO }, { 0x0EE, "VDP2_SFCCMD", WO }, { 0x0F0, "VDP2_PRISA", WO },
    { 0x0F2, "VDP2_PRISB", WO }, { 0x0F4, "VDP2_PRISC", WO }, { 0x0F6, "VDP2_PRISD", WO },
    { 0x0F8, "VDP2_PRINA", WO }, { 0x0FA, "VDP2_PRINB", WO }, { 0x0FC, "VDP2_PRIR", WO },
    { 0x100, "VDP2_CCRSA", WO }, { 0x102, "VDP2_CCRSB", WO }, { 0x104, "VDP2_CCRSC", WO },
    { 0x106, "VDP2_CCRSD", WO }, { 0x108, "VDP2_CCRNA", WO }, { 0x10A, "VDP2_CCRNB", WO },
    { 0x10C, "VDP2_CCRR", WO }, { 0x10E, "VDP2_CCRLB", WO }, { 0x110, "VDP2_CLOFEN", WO },
    { 0x112, "VDP2_CLOFSL", WO }, { 0x114, "VDP2_COAR", WO }, { 0x116, "VDP2_COAG", WO },
    { 0x118, "VDP2_COAB", WO }, { 0x11A, "VDP2_COBR", WO }, { 0x11C, "VDP2_COBG", WO },
    { 0x11E, "VDP2_COBB", WO },
};

// SCU: DMA levels 0-2, DSP, timers, interrupts, A-bus (Sega "SCU User's Manual"). Read back, per
// Mednafen scu.inc: the DMA addresses, DSTA, PPAF, PDD, IST, RSEL and VER; every other register
// reads 0, which is why SBL keeps its own copy of IMS. AIACK is among them (SCU_RegRW_DB_u*_W0
// case 0xA8: `*DB = 0; //ABusIAck`): the manual lists it read/write, the emulator SE runs does not
// read it back, so it is write-only here (A5-16).
const Reg kScu[] = {
    { 0x000, "SCU_D0R", RW }, { 0x004, "SCU_D0W", RW }, { 0x008, "SCU_D0C", WO }, { 0x00C, "SCU_D0AD", WO },
    { 0x010, "SCU_D0EN", WO }, { 0x014, "SCU_D0MD", WO }, { 0x020, "SCU_D1R", RW }, { 0x024, "SCU_D1W", RW },
    { 0x028, "SCU_D1C", WO }, { 0x02C, "SCU_D1AD", WO }, { 0x030, "SCU_D1EN", WO }, { 0x034, "SCU_D1MD", WO },
    { 0x040, "SCU_D2R", RW }, { 0x044, "SCU_D2W", RW }, { 0x048, "SCU_D2C", WO }, { 0x04C, "SCU_D2AD", WO },
    { 0x050, "SCU_D2EN", WO }, { 0x054, "SCU_D2MD", WO }, { 0x060, "SCU_DSTP", WO }, { 0x07C, "SCU_DSTA", RO },
    { 0x080, "SCU_PPAF", RW, 0, "a write controls the DSP program; a read returns its status",
      "a read clears the DSP's V and E flags and its end interrupt", nullptr,
      "controls the DSP from the whole 32-bit value: execute, pause, step, load the program counter" },
    { 0x084, "SCU_PPD", WO, 0, nullptr, nullptr, nullptr,
      "loads one DSP program word at the program counter and advances it (only while the DSP is stopped)" },
    { 0x088, "SCU_PDA", WO, 0, nullptr, nullptr, nullptr, "sets the DSP data RAM address" },
    { 0x08C, "SCU_PDD", RW, 0, nullptr,
      "while the DSP is stopped, a read returns data and advances the address (PDA); while it runs, "
      "it returns 0xFFFFFFFF and advances nothing", nullptr,
      "stores one word at the DSP data RAM address and advances it (only while the DSP is stopped)" },
    { 0x090, "SCU_T0C", WO }, { 0x094, "SCU_T1S", WO }, { 0x098, "SCU_T1MD", WO }, { 0x0A0, "SCU_IMS", WO },
    { 0x0A4, "SCU_IST", RW, 0, "a write of 0 clears that interrupt; a read returns the pending ones" },
    { 0x0A8, "SCU_AIACK", WO }, { 0x0B0, "SCU_ASR0", WO }, { 0x0B4, "SCU_ASR1", WO },
    { 0x0B8, "SCU_AREF", WO }, { 0x0C4, "SCU_RSEL", RW }, { 0x0C8, "SCU_VER", RO },
};

// The CD block's host interface in A-bus CS2: 16-bit registers every 4 bytes, the data FIFO
// first (Mednafen cdb.cpp CDB_Read/CDB_Write_DBM, scu.inc). The FIFO is read 32 bits at a time
// at 0x25818000 by the BIOS and SBL, so it is named there.
const Reg kCdBlock[] = {
    { 0x0008, "CD_HIRQ", RW, 0, "a write of 0 clears that flag; a read returns the flags" },
    { 0x000C, "CD_HIRQMSK", RW },
    { 0x0018, "CD_CR1", RW, 0, "a write is the command; a read is the response" },
    { 0x001C, "CD_CR2", RW, 0, "a write is the command; a read is the response" },
    { 0x0020, "CD_CR3", RW, 0, "a write is the command; a read is the response" },
    { 0x0024, "CD_CR4", RW, 0, "a write is the command; a read is the response",
      "a read marks the response as read", nullptr, "starts the command" },
};
const Reg kCdFifo[] = { { 0x0000, "CD_DATATRNS", RW, 4, nullptr, "a read takes the next data off the FIFO" } };

// The CPU-to-CPU signals: a 16-bit write anywhere in the 8 MiB sets the other SH-2's FRT input
// capture flag (SCU manual MINIT/SINIT; Docs/Saturn/SaturnMemory.txt note 4). Nothing reads back.
const Reg kMinit[] = { { 0x0000, "MINIT", WO } };   // the master signals the slave
const Reg kSinit[] = { { 0x0000, "SINIT", WO } };   // the slave signals the master

const char* const kShadow = "Mednafen's divider shadow register, not one in the SH7604's register table";

// The SH-2's own peripherals (Hitachi SH7604 hardware manual; Mednafen sh7095.inc). The FRT
// counter is two bytes read high first: the high read latches the low byte. The watchdog's
// registers are named where they are read, a byte each at FE80/FE81/FE83 (and again at
// FE88/FE89/FE8B); they take no byte write, only keyed words at FE80 and FE82 (KeyedWrite).
// Flags cleared by "writing 0 after reading 1" (FTCSR, WTCSR, RSTCSR, CHCR, DMAOR, RTCSR) are armed by the read.
// DVDNTH_SHADOW and DVDNTL_SHADOW at FF18/FF1C are Mednafen's, not in the SH7604's register table.
const Reg kOnChip[] = {
    { 0x000, "SH2_SMR", RW, 1 }, { 0x001, "SH2_BRR", RW, 1 }, { 0x002, "SH2_SCR", RW, 1 },
    { 0x003, "SH2_TDR", RW, 1 }, { 0x004, "SH2_SSR", RW, 1 }, { 0x005, "SH2_RDR", RO, 1 },
    { 0x010, "SH2_TIER", RW, 1 }, { 0x011, "SH2_FTCSR", RW, 1, nullptr, "a read arms clearing its flags: write 0 to a flag read as 1" },
    { 0x012, "SH2_FRCH", RW, 1, nullptr, "a read latches SH2_FRCL for the next low-byte read" },
    { 0x013, "SH2_FRCL", RW, 1 }, { 0x014, "SH2_OCRH", RW, 1 }, { 0x015, "SH2_OCRL", RW, 1 },
    { 0x016, "SH2_TCR", RW, 1 }, { 0x017, "SH2_TOCR", RW, 1 }, { 0x018, "SH2_FICRH", RO, 1, nullptr, "a read latches SH2_FICRL for the next low-byte read" },
    { 0x019, "SH2_FICRL", RO, 1 }, { 0x060, "SH2_IPRB", RW, 2 }, { 0x062, "SH2_VCRA", RW, 2 },
    { 0x064, "SH2_VCRB", RW, 2 }, { 0x066, "SH2_VCRC", RW, 2 }, { 0x068, "SH2_VCRD", RW, 2 },
    { 0x071, "SH2_DRCR0", RW, 1 }, { 0x072, "SH2_DRCR1", RW, 1 }, { 0x080, "SH2_WTCSR", KW, 1, nullptr, "a read arms clearing OVF: write 0 to it after reading 1" },
    { 0x081, "SH2_WTCNT", KW, 1 }, { 0x083, "SH2_RSTCSR", KW, 1, nullptr, "a read arms clearing WOVF: write 0xA500 to it after reading 1" }, { 0x091, "SH2_SBYCR", RW, 1 },
    { 0x092, "SH2_CCR", RW, 1 }, { 0x0E0, "SH2_ICR", RW, 2 }, { 0x0E2, "SH2_IPRA", RW, 2 },
    { 0x0E4, "SH2_VCRWDT", RW, 2 }, { 0x100, "SH2_DVSR", RW, 4 },
    { 0x104, "SH2_DVDNT", RW, 4, nullptr, nullptr, nullptr, "starts a 32-by-32 division" },
    { 0x108, "SH2_DVCR", RW, 4 }, { 0x10C, "SH2_VCRDIV", RW, 4 }, { 0x110, "SH2_DVDNTH", RW, 4 },
    { 0x114, "SH2_DVDNTL", RW, 4, nullptr, nullptr, nullptr, "starts a 64-by-32 division" },
    { 0x118, "SH2_DVDNTH_SHADOW", RW, 4, nullptr, nullptr, kShadow },
    { 0x11C, "SH2_DVDNTL_SHADOW", RW, 4, nullptr, nullptr, kShadow },
    { 0x140, "SH2_BARAH", RW, 2 }, { 0x142, "SH2_BARAL", RW, 2 },
    { 0x144, "SH2_BAMRAH", RW, 2 }, { 0x146, "SH2_BAMRAL", RW, 2 }, { 0x148, "SH2_BBRA", RW, 2 },
    { 0x160, "SH2_BARBH", RW, 2 }, { 0x162, "SH2_BARBL", RW, 2 }, { 0x164, "SH2_BAMRBH", RW, 2 },
    { 0x166, "SH2_BAMRBL", RW, 2 }, { 0x168, "SH2_BBRB", RW, 2 }, { 0x170, "SH2_BDRBH", RW, 2 },
    { 0x172, "SH2_BDRBL", RW, 2 }, { 0x174, "SH2_BDMRBH", RW, 2 }, { 0x176, "SH2_BDMRBL", RW, 2 },
    { 0x178, "SH2_BRCR", RW, 2 }, { 0x180, "SH2_SAR0", RW, 4 }, { 0x184, "SH2_DAR0", RW, 4 },
    { 0x188, "SH2_TCR0", RW, 4 }, { 0x18C, "SH2_CHCR0", RW, 4, nullptr, "a read arms clearing TE: write 0 to it after reading 1" }, { 0x190, "SH2_SAR1", RW, 4 },
    { 0x194, "SH2_DAR1", RW, 4 }, { 0x198, "SH2_TCR1", RW, 4 }, { 0x19C, "SH2_CHCR1", RW, 4, nullptr, "a read arms clearing TE: write 0 to it after reading 1" },
    { 0x1A0, "SH2_VCRDMA0", RW, 4 }, { 0x1A8, "SH2_VCRDMA1", RW, 4 }, { 0x1B0, "SH2_DMAOR", RW, 4, nullptr, "a read arms clearing AE and NMIF: write 0 after reading 1" },
    { 0x1E0, "SH2_BCR1", RW, 4 }, { 0x1E4, "SH2_BCR2", RW, 4 }, { 0x1E8, "SH2_WCR", RW, 4 },
    { 0x1EC, "SH2_MCR", RW, 4 }, { 0x1F0, "SH2_RTCSR", RW, 4, nullptr, "a read arms clearing CMF: write 0 after reading 1" }, { 0x1F4, "SH2_RTCNT", RW, 4 },
    { 0x1F8, "SH2_RTCOR", RW, 4 },
};

const uint32_t kSmpcBase = 0x00100000u, kScspBase = 0x05B00000u, kVdp1Base = 0x05D00000u;
const uint32_t kVdp2Base = 0x05F80000u, kScuBase = 0x05FE0000u, kCdBase = 0x05890000u;
const uint32_t kCdFifoBase = 0x05818000u, kMinitBase = 0x01000000u, kSinitBase = 0x01800000u;
const uint32_t kOnChipBase = 0xFFFFFE00u;

// The width family of each block (NarrowAccess). VDP1 and VDP2 write a register whole from a byte
// write (vdp1.c VDP1_Write8_DB -> WriteReg, vdp2.c RW_W8 -> RegsWrite); the SMPC is a byte device
// on the low lane (ss.c); the SCU masks a byte write (SCU_RegRW_DB_u8_W1) except in its DSP ports,
// and the SCSP (SCSP_NE16_RW) writes the bytes it is given; the CD block answers every 4 bytes
// (scu.inc ABusRW_DB_*, cdb.c CDB_Write_DBM); MINIT and SINIT are the CS0 "FRT trigger region"
// (ss.c). The on-chip block is split in Build: from 0xFFFFFF00 up, Mednafen's handlers (the
// A & 0x100 half of sh7095.inc's OnChipReg*) take a register as a whole; below it they take it by
// lane, and a longword is an address error.
#define SE_BLOCK(base, size, regs, narrow) { base, size, regs, sizeof(regs) / sizeof(regs[0]), narrow }
const Block kBlocks[] = {
    SE_BLOCK(kSmpcBase, 1, kSmpc, NarrowAccess::ByteLane),
    SE_BLOCK(kScspBase, 2, kScsp, NarrowAccess::Part),
    SE_BLOCK(kVdp1Base, 2, kVdp1, NarrowAccess::WholeOnByte),
    SE_BLOCK(kVdp2Base, 2, kVdp2, NarrowAccess::WholeOnByte),
    SE_BLOCK(kScuBase, 4, kScu, NarrowAccess::Part),
    SE_BLOCK(kCdBase, 2, kCdBlock, NarrowAccess::CdWindow),
    SE_BLOCK(kCdFifoBase, 4, kCdFifo, NarrowAccess::CdWindow),
    SE_BLOCK(kMinitBase, 2, kMinit, NarrowAccess::Strobe),
    SE_BLOCK(kSinitBase, 2, kSinit, NarrowAccess::Strobe),
    SE_BLOCK(kOnChipBase, 0, kOnChip, NarrowAccess::Part),
};
#undef SE_BLOCK

std::vector<HardwareRegister> Build()
{
    std::vector<HardwareRegister> all;
    for (const Block& b : kBlocks)
        for (size_t i = 0; i < b.count; ++i)
        {
            const Reg& r = b.regs[i];
            const uint8_t size = r.size ? r.size : b.size;
            NarrowAccess narrow = b.narrow;
            if (b.base == kScuBase && r.offset >= 0x80 && r.offset <= 0x8C)
                narrow = NarrowAccess::ScuPort;   // PPAF, PPD, PDA, PDD: scu.inc takes *DB whole for these
            if (b.base == kOnChipBase)
            {
                if (r.offset >= 0x100)
                    narrow = size == 4 ? (r.offset >= 0x1E0 ? NarrowAccess::OnChipKeyedLong
                                                            : r.offset >= 0x180 ? NarrowAccess::OnChipDmac : NarrowAccess::OnChipLong)
                                       : NarrowAccess::OnChipWord;
                else if (r.offset < 0x10)
                    narrow = NarrowAccess::OnChipSci;   // SMR..RDR: sh7095.inc has no case for them
                else
                    narrow = size == 2 ? NarrowAccess::OnChipHalf : NarrowAccess::OnChipByte;
            }
            HardwareRegister reg{ b.base + r.offset, size, r.name, r.access, r.split, r.onRead, r.about };
            reg.narrow = narrow;
            reg.onWrite = r.onWrite;
            reg.noteWrite = r.noteWrite;
            all.push_back(reg);
        }
    std::sort(all.begin(), all.end(),
              [](const HardwareRegister& x, const HardwareRegister& y) { return x.address < y.address; });
    return all;
}

const std::vector<HardwareRegister>& Table()
{
    static const std::vector<HardwareRegister> table = Build();
    return table;
}

// Each SMPC register as the 16-bit bus word it sits in: the word at the even address.
std::vector<HardwareRegister> BuildWordViews()
{
    std::vector<HardwareRegister> out;
    for (const HardwareRegister& r : Table())
        if (r.narrow == NarrowAccess::ByteLane)
        {
            HardwareRegister v = r;
            v.address = r.address - 1;
            v.size = 2;
            out.push_back(v);
        }
    return out;
}

const std::vector<HardwareRegister>& WordViews()
{
    static const std::vector<HardwareRegister> views = BuildWordViews();
    return views;
}

// ...and the word's other byte, the high lane, which no register uses: a row of the width table
// (a byte there does nothing, reads 0xFF) but not a symbol -- the engine takes the smallest symbol
// at an address for an access of any width, so a 1-byte symbol beside the 2-byte one would print
// every word write as the unused lane (tried, A5-17).
std::vector<HardwareRegister> BuildUnusedLanes()
{
    // The names must outlive the table and not move: sized once, filled in place.
    static std::vector<std::string> names;
    names.clear();
    names.reserve(WordViews().size());
    std::vector<HardwareRegister> out;
    for (const HardwareRegister& v : WordViews())
    {
        char name[32];
        std::snprintf(name, sizeof name, "SMPC_UNUSED_%02X", unsigned(v.address & 0x7Fu));
        names.emplace_back(name);
        HardwareRegister pad{ v.address, 1, names.back().c_str(), RegisterAccess::ReadWrite, nullptr, nullptr, nullptr };
        pad.narrow = NarrowAccess::ByteLane;
        out.push_back(pad);
    }
    return out;
}

const std::vector<HardwareRegister>& UnusedLanes()
{
    static const std::vector<HardwareRegister> lanes = BuildUnusedLanes();
    return lanes;
}

// The bus areas that hold registers rather than memory, as 27-bit bus addresses
// (Docs/Saturn/MemoryLayout.txt). Whole areas, mirrors and unnamed registers included. Sound RAM
// is memory, but the 68000 runs from it and the two CPUs talk through it, so a poll of a
// mailbox there must not fold either. Work RAM shared with the slave SH-2 is the user's to mark
// (DecompileRequest::volatileMemory): most of it is ordinary memory.
struct Area
{
    uint32_t first, last;
};
const Area kBusAreas[] = {
    { 0x00100000u, 0x0017FFFFu },   // SMPC
    { 0x00180000u, 0x001FFFFFu },   // backup RAM: a byte device on the low lane of the 16-bit bus, like the SMPC (A5-26)
    { 0x01000000u, 0x01FFFFFFu },   // MINIT / SINIT
    { 0x05800000u, 0x058FFFFFu },   // A-bus CS2: the CD block
    { 0x05A00000u, 0x05AFFFFFu },   // sound RAM, shared with the 68000
    { 0x05B00000u, 0x05BFFFFFu },   // SCSP registers
    { 0x05D00000u, 0x05D7FFFFu },   // VDP1 registers
    { 0x05F80000u, 0x05FBFFFFu },   // VDP2 registers
    { 0x05FE0000u, 0x05FFFFFFu },   // SCU registers, and the unknown ones above them
};

std::vector<HardwareRange> BuildRanges()
{
    std::vector<HardwareRange> out;
    // Bits 28-27 are not decoded, so each area repeats every 128 MiB.
    for (uint32_t partition : { 0x00000000u, 0x20000000u })
        for (uint32_t repeat = 0; repeat < 4; ++repeat)
            for (const Area& a : kBusAreas)
            {
                const uint32_t base = partition | (repeat << 27);
                out.push_back(HardwareRange{ base | a.first, base | a.last });
            }
    // Not memory at all: associative purge and the cache address array (writes there change the
    // cache), the 0xA partition, and the on-chip partition. SaturnMemory.txt calls 0xA the
    // cache-through partition again; Mednafen, which SE runs, sends it to the associative purge
    // with 0x4 (sh7095.inc). Either way an access there has an effect, so all of it is volatile;
    // its registers are not named, since the backend does not reach them there.
    out.push_back(HardwareRange{ 0x40000000u, 0x7FFFFFFFu });
    out.push_back(HardwareRange{ 0xA0000000u, 0xBFFFFFFFu });
    out.push_back(HardwareRange{ 0xE0000000u, 0xFFFFFFFFu });
    std::sort(out.begin(), out.end(), [](const HardwareRange& x, const HardwareRange& y) { return x.first < y.first; });
    return out;
}
}  // namespace

const HardwareRegister* SaturnHardwareRegisters(size_t& count)
{
    count = Table().size();
    return Table().data();
}

bool HardwareAlias(uint32_t address, uint32_t& canonical)
{
    const uint32_t partition = address >> 29;
    if (partition == 7)
    {
        // Mednafen decodes the whole of 0xE0000000-0xFFFFFFFF by A & 0x1FF: MemRead and MemWrite case 7 pass the
        // address to OnChipReg*, which look at bit 8 (the 0xFFFFFF00 half) and the low byte (sh7095.inc). The
        // manual's map (SaturnMemory.txt) has bit 14 set, and bits 28 and 12 for the low 256 offsets, and reads
        // garbage elsewhere; the emulator is the reference SE runs, and the decompiler describes what it does.
        uint32_t offset = address & 0x1FFu;
        // The divider answers at 0x120-0x13F too, the same registers (sh7095.inc decodes
        // 0x00/0x20 alike, a write there divides as well); the watchdog at 0x88-0x8B, its read
        // bytes and its write ports alike.
        if (offset >= 0x120u && offset < 0x140u) offset -= 0x20u;
        if (offset >= 0x088u && offset < 0x08Cu) offset -= 0x08u;
        // CCR reads back at 0xFFFFFE92-0xFFFFFE9F (sh7095.inc OnChipRegRead_INLINE_u8, case 0x92 ... 0x9F);
        // a write there does nothing ("No 0x93...0x9F mirrors for writes"; the width table says so).
        if (offset > 0x092u && offset <= 0x09Fu) offset = 0x092u;
        canonical = kOnChipBase + offset;
        return true;
    }
    if (partition != 0 && partition != 1) return false;
    const uint32_t a = address & 0x07FFFFFFu;
    if (a >= 0x00100000u && a <= 0x0017FFFFu) canonical = kSmpcBase | (a & 0x7Fu);
    else if (a >= 0x01000000u && a <= 0x017FFFFFu) canonical = kMinitBase | (a & 1u);
    else if (a >= 0x01800000u && a <= 0x01FFFFFFu) canonical = kSinitBase | (a & 1u);
    else if (a >= 0x05800000u && a <= 0x058FFFFFu && (a & 0x7FFFu) < 0x1000u)
    {
        // The CD block answers wherever the low 15 bits are below 0x1000, one register per 4
        // bytes of a 64-byte window (scu.inc).
        const uint32_t reg = a & 0x3Cu;
        canonical = reg == 0 ? kCdFifoBase | (a & 3u) : kCdBase | reg | (a & 1u);
    }
    else if (a >= 0x05B00000u && a <= 0x05BFFFFFu) canonical = kScspBase | (a & 0xFFFu);
    else if (a >= 0x05D00000u && a <= 0x05D7FFFFu) canonical = a;   // VDP1: no repeat
    else if (a >= 0x05F80000u && a <= 0x05FBFFFFu) canonical = kVdp2Base | (a & 0x1FFu);
    else if (a >= 0x05FE0000u && a <= 0x05FEFFFFu) canonical = kScuBase | (a & 0xFFu);
    else return false;
    return true;
}

const HardwareRegister* FindHardwareRegister(uint32_t address)
{
    uint32_t a = 0;
    if (!HardwareAlias(address, a)) return nullptr;
    const std::vector<HardwareRegister>& t = Table();
    auto it = std::upper_bound(t.begin(), t.end(), a,
                               [](uint32_t v, const HardwareRegister& r) { return v < r.address; });
    if (it == t.begin()) return nullptr;
    --it;
    return a < it->address + it->size ? &*it : nullptr;
}

std::vector<const HardwareRegister*> HardwareRegistersCovering(uint32_t address, uint32_t size)
{
    std::vector<const HardwareRegister*> out;
    for (uint32_t i = 0; i < size; ++i)
    {
        const HardwareRegister* r = FindHardwareRegister(address + i);
        if (r && (out.empty() || out.back() != r)) out.push_back(r);
    }
    return out;
}

const HardwareRegister* SaturnHardwareWordViews(size_t& count)
{
    count = WordViews().size();
    return WordViews().data();
}

// ---- The width table ---------------------------------------------------------------------------

namespace
{
struct RuleInfo
{
    char category;
    const char* name;
    const char* source;
};

// In WidthRule's order. 'source' is where in Mednafen (the reference SE runs) the rule is read from.
const RuleInfo kRuleInfo[] = {
    { 'a', "Plain", "the register's own handler: scu.inc SCU_RegRW_DB_u*, SS_SCSP_RW_u*, vdp1.c ReadReg/WriteReg, vdp2.c RegsRead/RegsWrite, sh7095.inc OnChipReg*, cdb.c CDB_Read/CDB_Write_DBM" },
    { 'a', "HalfOfLong", "scu.inc SCU_FromSH2_BusRW_DB_u32_W*: the B bus is BBusRW_DB_u16_*_SH0 at A then _SH1 at A|2, high half first" },
    { '-', "AccessKind", "(none: the register's kind -- write-only, read-only -- says it)" },
    { 'b', "ByteWritesWord", "scu.inc BBusRW_DB_u8_W1_SH0 -> vdp1.c VDP1_Write8_DB / vdp2.c VDP2_Write8_DB: WriteReg / RegsWrite(A, *DB), the 16-bit lane of the data bus (RW_W8's byte mask is VRAM's only)" },
    { 'b', "PortWritesWhole", "scu.inc SCU_RegRW_DB_u8_W1 / _u16_W1 cases 0x80-0x8C: *DB is used whole, the mask every other case applies is not" },
    { 'b', "NarrowReadWhole", "scu.inc SCU_RegRW_DB_u*_W0 cases 0x80, 0x8C: the whole 32-bit read happens, the CPU takes its part" },
    { 'b', "WordWritesLong", "sh7095.inc SH7095_OnChipRegWrite_u16, A & 0x100 set: the register takes V (DVDNT, DVDNTL also divide)" },
    { 'b', "FifoPopsTwice", "scu.inc ABus_Read(A & ~3): ABusRW_DB_u16_W0_SH0 and _SH1 both reach CDB_Read(0) when bit 19 of A is clear" },
    { 'b', "FifoByteWrite", "scu.inc ABusRW_DB_u8_W1_SH0 (CS2, (A & 0x7FFF) < 0x1000): CDB_Write_DBM(0, *DB, 0xFF << ...); cdb.c case 0x0 stores (FIFO[WP] &~ mask) | (DB & mask) and advances WP, InBufOffs and TotalCounter by a word" },
    { 'c', "HalfBesideGap", "as HalfOfLong; the other half is a word nothing in the map names" },
    { 'c', "SmpcLaneWrite", "ss.c BusRW_DB_CS0_u16_W1: SMPC_Write((A & 0x7F) >> 1, low byte of the half); a long is two of them" },
    { 'c', "SmpcLaneRead", "ss.c BusRW_DB_CS0_u16_W0: 0xFF00 | SMPC_Read((A & 0x7F) >> 1); a long is two of them" },
    { 'c', "EvenByteWrite", "sh7095.inc SH7095_OnChipRegWrite_u16, A & 0x100 clear: switch (A & 0xFF) has the even address only; TIER = V (uint8_t)" },
    { 'c', "EvenByteRead", "sh7095.inc SH7095_OnChipRegRead_INLINE_u16, A & 0x100 clear: ret is the register at the word's own address" },
    { 'c', "LowHalfRead", "sh7095.inc SH7095_OnChipRegRead_INLINE_u16, A & 0x100 set: A &= 0xFE, ret = the 32-bit register truncated" },
    { 'c', "CdLongWrite", "scu.inc ABus_Write_DB32_u32: ABusRW_DB_u16_W1_SH0 at A, _SH1 at A|2; both decode (A & 0x3F) >> 2 to one CDB_Write_DBM" },
    { 'c', "CdLongRead", "scu.inc ABus_Read: ABusRW_DB_u16_W0_SH1 reads only if !(A & 0x80000), so the register window keeps the first value in both halves" },
    { 'd', "SmpcPadWrite", "ss.c BusRW_DB_CS0_u8_W1: SMPC_Write only if (A & 1)" },
    { 'd', "SmpcPadRead", "ss.c BusRW_DB_CS0_u8_W0: the high lane of the 16-bit word reads 0xFF" },
    { 'd', "OddByteWrite", "sh7095.inc SH7095_OnChipRegWrite_u16, A & 0x100 clear: no case at the odd address of a word (A is even)" },
    { 'd', "OddByteRead", "sh7095.inc SH7095_OnChipRegRead_INLINE_u16, A & 0x100 clear: no case at the odd address of a word" },
    { 'd', "LongHalfWrite", "sh7095.inc SH7095_OnChipRegWrite_u16, A & 0x100 set: A &= 0xFE, and the cases are the registers' own addresses" },
    { 'd', "LongHalfRead", "sh7095.inc SH7095_OnChipRegRead_INLINE_u16, A & 0x100 set: A &= 0xFE; the DMA cases are 0x80 .. 0xB0 only, so +2 falls to default (ret = 0, no CHCRM / DMAORM clear)" },
    { 'd', "KeyedWordWrite", "sh7095.inc SH7095_OnChipRegWrite_u16 cases 0xE0-0xF8: break (only the _u32 write checks 0xA55A)" },
    { 'd', "KeyedByteWrite", "sh7095.inc SH7095_OnChipRegWrite_u8 cases 0x80-0x8F: z->WDT.RSTCSRM = 0 and nothing else (only the _u16 write takes a key)" },
    { 'd', "StrobeByte", "ss.c BusRW_DB_CS0_u8_W1, \"FRT trigger region\": the block is empty (only _u16_W1 calls SH7095_SetFTI)" },
    { 'd', "MirrorWrite", "sh7095.inc OnChipRegWrite_u*: case 0x92 only (\"No 0x93...0x9F mirrors for writes\")" },
    { 'd', "ReservedByteWrite", "sh7095.inc SH7095_OnChipRegWrite_u8 cases 0x60/0x61 (IPRB) and 0x68/0x69 (VCRD): z->IPRB = (z->IPRB &~ mask) | ((V << shift) & mask & 0xFF00), VCRD & 0x7F00, with mask 0xFF at the odd address: nothing is stored" },
    { 'e', "AddressError", "sh7095.inc SH7095_SetPEX(z, SH7095_PEX_CPUADDR): OnChipRegWrite_u8 / RegRead_u8 with A & 0x100 set, OnChipRegWrite_u32 / RegRead_u32 with it clear" },
    { 'f', "StrobeLong", "sh7095.inc SH7095_BSC_BusWrite_u32 (CS0): two BusRW_DB_CS0_u16_W1, each SH7095_SetFTI" },
    { 'f', "CdByteWrite", "cdb.c CDB_Write_DBM case 0x9: CommandPending only if mask == 0xFFFF" },
    { 'v', "KeyedWrite", "sh7095.inc OnChipRegWrite_u16 cases 0x80, 0x82 (the key in the upper byte); _u32 cases 0xE0-0xF8 ((V & 0xFFFF0000) == 0xA55A0000)" },
    { '?', "Undecoded", "sh7095.inc has no case for the serial interface: no rule is claimed" },
};
static_assert(sizeof(kRuleInfo) / sizeof(kRuleInfo[0]) == size_t(WidthRule::Undecoded) + 1, "one RuleInfo per WidthRule");

WidthCell MakeCell(WidthRule rule, uint8_t flags, unsigned hi = 0, unsigned lo = 0)
{
    WidthCell c;
    c.rule = rule;
    c.flags = flags;
    c.hi = static_cast<uint8_t>(hi);
    c.lo = static_cast<uint8_t>(lo);
    return c;
}

// A long on a 16-bit register of the 16-bit buses: two 16-bit accesses, high half first; the
// register takes one half, and the other is the register beside it or nothing the map names.
WidthCell HalfCell(const HardwareRegister& r, uint8_t flags)
{
    const uint32_t other = (r.address & ~3u) | ((r.address & 2u) ^ 2u);
    const bool high = !(r.address & 2u);
    return MakeCell(FindHardwareRegister(other) ? WidthRule::HalfOfLong : WidthRule::HalfBesideGap, flags,
                    high ? 31 : 15, high ? 16 : 0);
}

// The B bus's 16-bit registers (SCSP, VDP1, VDP2). A write is one 16-bit access (a long: two); a
// read is a 32-bit bus read, SH0 at A and SH1 at A|2, so the register 2 bytes on is read as well
// when A has bit 1 clear, and this one twice when it is set (scu.inc BBusRW_DB_u16_W0_SH0, _SH1).
WidthCell BBus16(const HardwareRegister& r, uint32_t w, bool write, int32_t off, bool whole)
{
    if (w == 4) return HalfCell(r, kWidthEffect);
    if (write) return MakeCell(w == 1 && whole ? WidthRule::ByteWritesWord : WidthRule::Plain, kWidthEffect);
    const uint32_t a = r.address + off;
    return MakeCell(WidthRule::Plain, kWidthEffect | ((a & 2u) ? kWidthReadsTwice : kWidthReadsNext));
}

std::string NameAt(uint32_t canonical)
{
    const HardwareRegister* r = FindHardwareRegister(canonical);
    return r ? r->name : std::string();
}

// The register at 'canonical' when it is another one than 'self' (the CCR reads back at the next address too).
std::string NameBeside(const HardwareRegister& self, uint32_t canonical)
{
    const HardwareRegister* r = FindHardwareRegister(canonical);
    return r && r != &self ? r->name : std::string();
}

std::string Hex8s(uint32_t a)
{
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", a);
    return b;
}

std::string Bits(unsigned hi, unsigned lo)
{
    return "bits " + std::to_string(hi) + "-" + std::to_string(lo);
}

// What Mednafen does after it raises the address error of a byte access at 0xFFFFFF00 and up (A5-31, sh7095.inc
// OnChipRegWrite_u8 / RegRead_u8, A & 0x100 set): SetPEX, then the access is made on the register at A & 0xFC. A write
// stores `V |= (uint8_t)V << 8` (both bytes of the low half, the upper 16 bits 0) and does what the register does on a
// write (a division); a read returns bits 15-8 of the register whatever byte was asked for (`ret >>= ((A & 1) ^ 1) << 3`
// runs after A has been aligned, so it always shifts by 8). The bus state controller takes a 4-byte keyed write only, and
// the user break controller has no handler: neither stores anything, and the latter reads 0.
std::string AfterByteError(const HardwareRegister& r, bool write)
{
    if (r.narrow == NarrowAccess::OnChipWord)
        return write ? "; Mednafen decodes no register there, so the write does nothing" : "; Mednafen decodes no register there, so the read returns 0";
    if (write)
    {
        if (r.narrow == NarrowAccess::OnChipKeyedLong)
            return "; Mednafen then ignores the write: the bus state controller takes a 4-byte write with 0xA55A in its upper half only";
        return std::string("; Mednafen then makes the access: the register takes the byte twice (V | V << 8, bits 15-0; the upper 16 bits are 0)") +
               (r.onWrite ? std::string(" and ") + r.onWrite : std::string());
    }
    return "; Mednafen then returns bits 15-8 of the register, whichever byte was asked for";
}

// The caution a cell gives, or "" when it has none. The wording of the round 2 fixtures is kept.
std::string WidthText(const HardwareRegister& r, const WidthCell& c, uint32_t w, bool write, uint32_t access)
{
    const std::string name = r.name;
    const std::string width = std::to_string(w) + "-byte";
    const std::string dir = write ? "write to " : "read of ";
    const std::string bitsOf = std::to_string(unsigned(r.size) * 8) + "-bit";
    const std::string article = r.size == 1 ? "an " : "a ";
    switch (c.rule)
    {
    case WidthRule::Plain:
    case WidthRule::HalfOfLong:
    case WidthRule::AccessKind:
    case WidthRule::KeyedWrite:
    case WidthRule::Undecoded:
        return std::string();
    case WidthRule::ByteWritesWord:
        return "a 1-byte write to " + name + " writes the whole 16-bit register: its other byte is whatever was last on the data bus";
    case WidthRule::PortWritesWhole:
        return "a " + width + " write to " + name + " writes the whole 32-bit register: its other bytes are whatever was last on "
               "the data bus (the SCU masks the byte and word writes of every other register, not the DSP ports)" +
               (r.onWrite ? std::string(" and ") + r.onWrite : std::string());
    case WidthRule::NarrowReadWhole:
        return "a " + width + " read of " + name + " is a read of the whole 32-bit register: what a read does to it happens in "
               "full, and only the bytes asked for are returned";
    case WidthRule::WordWritesLong:
        return "a 2-byte write to " + name + ", a 32-bit register, writes the whole register with the 16-bit value (upper half 0)" +
               (r.onWrite ? std::string(" and ") + r.onWrite : std::string());
    case WidthRule::FifoByteWrite:
        return "a 1-byte write to " + name + " pushes a whole 16-bit word into the transfer (the CD block takes the byte under a "
               "byte mask and leaves the other byte of the FIFO slot as it was): the sector gets one word per write, half of it stale";
    case WidthRule::FifoPopsTwice:
        return "a " + width + " read of " + name + " takes two 16-bit words off the FIFO: the 32-bit bus reads it twice and the "
               "access returns only part of them (the register window at 0x25898000 reads it once)";
    case WidthRule::HalfBesideGap:
    {
        const unsigned oh = c.hi == 31 ? 15 : 31, ol = c.hi == 31 ? 0 : 16;
        const uint32_t other = (access & ~3u) | ((r.address & 2u) ^ 2u);
        return write ? "a 4-byte write over " + name + " is two 16-bit bus writes, high half first: " + name + " takes " +
                           Bits(c.hi, c.lo) + " of the value, and " + Bits(oh, ol) + " go to the word at " + Hex8s(other) +
                           ", which is no register in the map"
                     : "a 4-byte read over " + name + " is two 16-bit bus reads: " + name + " is " + Bits(c.hi, c.lo) +
                           " of the result, and " + Bits(oh, ol) + " are the word at " + Hex8s(other) +
                           ", which is no register in the map";
    }
    case WidthRule::SmpcLaneWrite:
        if (w == 2)
            return "a 2-byte write to " + name + " is a byte write: the register takes the low byte of the value and the high byte "
                   "goes nowhere (the SMPC is a byte device on the low lane of the 16-bit bus)";
        return "a 4-byte write reaching " + name + " is two 16-bit bus writes: the register takes " + Bits(c.hi, c.lo) +
               " of the value, and the other byte of each half goes nowhere (the SMPC is a byte device on the low lane)";
    case WidthRule::SmpcLaneRead:
        if (w == 2)
            return "a 2-byte read of " + name + " returns 0xFF00 or the register: the SMPC is a byte device on the low lane of "
                   "the 16-bit bus, and the high byte reads as 0xFF";
        return "a 4-byte read of " + name + " is two 16-bit bus reads: the register is " + Bits(c.hi, c.lo) +
               " of the result, and the other byte of each half reads as 0xFF (the SMPC is a byte device on the low lane)";
    case WidthRule::SmpcPadWrite:
    case WidthRule::SmpcPadRead:
    {
        const std::string beside = NameAt(r.address | 1u);
        return c.rule == WidthRule::SmpcPadWrite
                   ? "a 1-byte write at the even address beside " + beside + " does nothing: the SMPC is a byte device on the low "
                     "lane of the 16-bit bus, and its registers are at the odd addresses"
                   : "a 1-byte read at the even address beside " + beside + " returns 0xFF, not the register: the SMPC is a byte "
                     "device on the low lane of the 16-bit bus, and its registers are at the odd addresses";
    }
    case WidthRule::EvenByteWrite:
    {
        const std::string next = NameBeside(r, r.address + 1);
        return "a 2-byte write to " + name + ", an 8-bit register, reaches only it, with the low byte of the value (bits 7-0, the "
               "byte a big-endian word has at the odd address)" +
               (next.empty() ? std::string() : "; " + next + ", the register beside it, is not written") +
               (r.onWrite ? std::string(" and ") + r.onWrite : std::string());
    }
    case WidthRule::EvenByteRead:
    {
        const std::string next = NameBeside(r, r.address + 1);
        if (r.address == 0xFFFFFE92u)   // sh7095.inc: case 0x92 ... 0x9F: ret = CCR | (CCR << 8)
            return "a 2-byte read at " + name + ", an 8-bit register, returns it in both bytes (bits 15-8 and 7-0)";
        return "a 2-byte read at " + name + ", an 8-bit register, returns only it, in bits 7-0" +
               (next.empty() ? std::string() : "; " + next + ", the register beside it, is not read");
    }
    case WidthRule::OddByteWrite:
    case WidthRule::OddByteRead:
    {
        const std::string prev = NameAt(r.address - 1);
        const bool wr = c.rule == WidthRule::OddByteWrite;
        // sh7095.inc OnChipRegRead_INLINE_u16: the watchdog's unused even addresses (0x82, 0x86, 0x8A, 0x8E) return 0xFF, the rest 0.
        const char* none = r.address - 1 == 0xFFFFFE82u ? ", and there is none (it reads 0x00FF)" : ", and there is none (it reads 0)";
        return "a 2-byte " + std::string(wr ? "write" : "read") + " at the even address beside " + name + " does not " +
               (wr ? "write" : "read") + " it: a word reaches only the register at its own address" +
               (prev.empty() ? std::string(wr ? ", and there is none" : none)
                             : " (" + prev + (wr ? ")" : ", in bits 7-0)"));
    }
    case WidthRule::LowHalfRead:
        return "a 2-byte read at " + name + ", a 32-bit register, returns its low 16 bits, not the upper half this address "
               "suggests" + (r.narrow == NarrowAccess::OnChipDmac ? " (a read at +2 returns 0 and reads nothing)"
                                                                  : " (a read at +2 returns them too)");
    case WidthRule::LongHalfRead:
        return "a 2-byte read at +2 of " + name + ", a 32-bit register, returns 0 and reads nothing: the DMA controller answers a "
               "word read at the register's own address only (what a read of it arms does not happen)";
    case WidthRule::LongHalfWrite:
        return "a 2-byte write to the low half of " + name + ", a 32-bit register, does nothing: a word takes effect only at "
               "the register's own address";
    case WidthRule::KeyedByteWrite:
        return KeyedPortCaution(r, w);
    case WidthRule::ReservedByteWrite:
        return "a 1-byte write to the low byte of " + name + " does nothing: that byte is reserved, and the handler masks the store with " +
               (r.address == 0xFFFFFE60u ? "0xFF00" : "0x7F00") + " (a read there returns 0)";
    case WidthRule::KeyedWordWrite:
        return "a 2-byte write to " + name + ", a 32-bit register, does nothing: it is set only by a 4-byte write with 0xA55A in "
               "the upper half";
    case WidthRule::AddressError:
        // (a long below 0xFFFFFF00 is one caution for the whole access, with what Mednafen does after it: OnChipLongError)
        if (w == 4) return std::string();
        return "a " + width + " " + dir + name + ", " + article + bitsOf + " register, is an address error on the SH-2: it is "
               "accessed whole" + AfterByteError(r, write);
    case WidthRule::MirrorWrite:
        return "a " + width + " write at " + Hex8s(access) + " does nothing: " + name + " reads back at 0xFFFFFE93-0xFFFFFE9F, but "
               "takes writes at 0xFFFFFE92 only";
    case WidthRule::StrobeByte:
        return "a 1-byte write to " + name + " does nothing: the signal to the other CPU takes a 16-bit write (the CS0 byte "
               "path ignores the FRT trigger region)";
    case WidthRule::StrobeLong:
        return "a 4-byte write to " + name + " is two 16-bit bus writes, so it signals the other CPU twice (its input capture "
               "takes the second)";
    case WidthRule::CdLongWrite:
        return "a 4-byte write to " + name + " is two 16-bit writes to the same register (the CD block answers every 4 bytes): it "
               "is written with bits 31-16 and then with bits 15-0, which it keeps, and a write that has an effect happens twice";
    case WidthRule::CdLongRead:
        return "a 4-byte read of " + name + " reads it once and returns it in both halves (the CD block ignores the second "
               "16-bit access in its register window)";
    case WidthRule::CdByteWrite:
        return "a 1-byte write to " + name + " stores that byte only: it does not start the command, which takes a 16-bit write";
    }
    return std::string();
}

bool IsPad(const HardwareRegister& r) { return r.narrow == NarrowAccess::ByteLane && r.size == 1 && !(r.address & 1u); }
}  // namespace

WidthCell WidthCellFor(const HardwareRegister& r, uint32_t w, bool write, int32_t off, uint32_t access)
{
    // The register's kind answers the direction it does not have.
    if (!IsPad(r) && ((write && r.access == RegisterAccess::ReadOnly) || (!write && r.access == RegisterAccess::WriteOnly)))
        return MakeCell(WidthRule::AccessKind, 0);
    const uint8_t eff = kWidthEffect;
    switch (r.narrow)
    {
    case NarrowAccess::Part:
        if (r.size == 4) return MakeCell(WidthRule::Plain, eff);   // the SCU: one access of any width, a write masked to its bytes
        return BBus16(r, w, write, off, false);                    // the SCSP
    case NarrowAccess::WholeOnByte:
        return BBus16(r, w, write, off, true);
    case NarrowAccess::ByteLane:
        if (IsPad(r)) return MakeCell(write ? WidthRule::SmpcPadWrite : WidthRule::SmpcPadRead, 0);
        if (w == 1) return MakeCell(WidthRule::Plain, eff);
        if (w == 2) return MakeCell(write ? WidthRule::SmpcLaneWrite : WidthRule::SmpcLaneRead, eff, 7, 0);
        return off == -1 ? MakeCell(write ? WidthRule::SmpcLaneWrite : WidthRule::SmpcLaneRead, eff, 23, 16)
                         : MakeCell(write ? WidthRule::SmpcLaneWrite : WidthRule::SmpcLaneRead, eff, 7, 0);
    case NarrowAccess::ScuPort:
        if (write) return MakeCell(w == 4 ? WidthRule::Plain : WidthRule::PortWritesWhole, eff);
        return MakeCell(w < 4 && (r.onRead || r.about) ? WidthRule::NarrowReadWhole : WidthRule::Plain, eff);
    case NarrowAccess::CdWindow:
    {
        const bool second = !(access & 0x80000u);   // the FIFO window: the CD block does not ignore the second access
        if (r.size == 4)   // the FIFO: two words in its own window, one in the register window
        {
            if (write && w == 1) return MakeCell(WidthRule::FifoByteWrite, eff);
            if (!write && w < 4 && second) return MakeCell(WidthRule::FifoPopsTwice, eff);
            if (!write && w == 4 && !second) return MakeCell(WidthRule::CdLongRead, eff, 31, 0);   // one word, in both halves
            return MakeCell(WidthRule::Plain, eff);
        }
        if (write)
        {
            if (w == 4) return MakeCell(WidthRule::CdLongWrite, eff, 31, 0);
            if (w == 1 && r.onWrite) return MakeCell(WidthRule::CdByteWrite, 0);
            return MakeCell(WidthRule::Plain, eff);
        }
        if (w == 4 && !second) return MakeCell(WidthRule::CdLongRead, eff, 31, 0);
        return MakeCell(WidthRule::Plain, eff | (second ? kWidthReadsTwice : 0));
    }
    case NarrowAccess::Strobe:
        if (!write) return MakeCell(WidthRule::Plain, 0);
        if (w == 1) return MakeCell(WidthRule::StrobeByte, 0);
        return MakeCell(w == 4 ? WidthRule::StrobeLong : WidthRule::Plain, eff);
    case NarrowAccess::OnChipLong:
    case NarrowAccess::OnChipDmac:
    case NarrowAccess::OnChipKeyedLong:
    {
        const bool keyed = r.narrow == NarrowAccess::OnChipKeyedLong;
        // The SH-2 takes an address error on a byte access to these, and Mednafen then makes the access: the
        // register's own effect (the divider starts, a flag is armed) happens (sh7095.inc, A5-24).
        if (w == 1) return MakeCell(WidthRule::AddressError, eff);
        if (write)
        {
            if (w == 4) return MakeCell(keyed ? WidthRule::KeyedWrite : WidthRule::Plain, keyed ? 0 : eff);
            if (keyed) return MakeCell(WidthRule::KeyedWordWrite, 0);
            return off == 0 ? MakeCell(WidthRule::WordWritesLong, eff, 15, 0) : MakeCell(WidthRule::LongHalfWrite, 0);
        }
        if (w == 2 && off == 0) return MakeCell(WidthRule::LowHalfRead, eff, 15, 0);
        // The DMA controller has no word case at +2 (the divider and the bus state controller have): it reads 0.
        if (w == 2 && r.narrow == NarrowAccess::OnChipDmac) return MakeCell(WidthRule::LongHalfRead, 0);
        return MakeCell(WidthRule::Plain, eff);
    }
    case NarrowAccess::OnChipWord:   // the UBC, which Mednafen does not decode: the SH7604 manual's byte rule
        if (w == 1) return MakeCell(WidthRule::AddressError, 0);
        if (w == 4) return HalfCell(r, eff);
        return MakeCell(WidthRule::Plain, eff);
    case NarrowAccess::OnChipHalf:
        // IPRB and VCRD keep only their high byte (`& mask & 0xFF00`, `& 0x7F00`): a byte write at +1 stores nothing (A5-30).
        if (write && w == 1 && off == 1 && (r.address == 0xFFFFFE60u || r.address == 0xFFFFFE68u))
            return MakeCell(WidthRule::ReservedByteWrite, 0);
        return MakeCell(w == 4 ? WidthRule::AddressError : WidthRule::Plain, w == 4 ? 0 : eff);
    case NarrowAccess::OnChipSci:
        return w == 1 ? MakeCell(WidthRule::Plain, eff) : MakeCell(WidthRule::Undecoded, 0);
    case NarrowAccess::OnChipByte:
    {
        if (w == 4) return MakeCell(WidthRule::AddressError, 0);
        if (write && w <= 2 && r.address == 0xFFFFFE92u && (access & 0x1FFu) != 0x92u)
            return MakeCell(WidthRule::MirrorWrite, 0);
        // (a word at the odd register's own address, which only a misaligned access makes -- the handlers below 0xFFFFFF00
        // switch on the address as given -- is not the key port either: the port is the even address, FE80)
        if (write && r.access == RegisterAccess::KeyedWrite)   // the watchdog
            return MakeCell(w == 1 || (w == 2 && off == 0 && (r.address & 1u)) ? WidthRule::KeyedByteWrite : WidthRule::KeyedWrite, 0);
        if (w == 1) return MakeCell(WidthRule::Plain, eff);
        const bool even = !(r.address & 1u) || off == 0;   // (off 0 at an odd register: a word at its own address, A5-29)
        if (write) return even ? MakeCell(WidthRule::EvenByteWrite, eff, 7, 0) : MakeCell(WidthRule::OddByteWrite, 0);
        return even ? MakeCell(WidthRule::EvenByteRead, eff, 7, 0) : MakeCell(WidthRule::OddByteRead, 0);
    }
    }
    return MakeCell(WidthRule::Plain, eff);
}

char WidthCategory(WidthRule rule) { return kRuleInfo[size_t(rule)].category; }
const char* WidthRuleName(WidthRule rule) { return kRuleInfo[size_t(rule)].name; }
const char* WidthRuleSource(WidthRule rule) { return kRuleInfo[size_t(rule)].source; }

const char* WidthFamilyName(NarrowAccess family)
{
    switch (family)
    {
    case NarrowAccess::Part: return "Part";
    case NarrowAccess::WholeOnByte: return "WholeOnByte";
    case NarrowAccess::ByteLane: return "ByteLane";
    case NarrowAccess::OnChipLong: return "OnChipLong";
    case NarrowAccess::OnChipKeyedLong: return "OnChipKeyedLong";
    case NarrowAccess::OnChipDmac: return "OnChipDmac";
    case NarrowAccess::OnChipWord: return "OnChipWord";
    case NarrowAccess::OnChipByte: return "OnChipByte";
    case NarrowAccess::OnChipHalf: return "OnChipHalf";
    case NarrowAccess::OnChipSci: return "OnChipSci";
    case NarrowAccess::ScuPort: return "ScuPort";
    case NarrowAccess::CdWindow: return "CdWindow";
    case NarrowAccess::Strobe: return "Strobe";
    }
    return "?";
}

std::vector<WidthProbe> WidthTable()
{
    std::vector<const HardwareRegister*> rows;
    for (const HardwareRegister& r : Table()) rows.push_back(&r);
    for (const HardwareRegister& v : UnusedLanes())
        if (v.size == 1) rows.push_back(&v);   // the SMPC's unused lanes
    std::stable_sort(rows.begin(), rows.end(),
                     [](const HardwareRegister* x, const HardwareRegister* y) { return x->address < y->address; });
    std::vector<WidthProbe> out;
    for (const HardwareRegister* r : rows)
    {
        // The CD FIFO answers in two windows (bit 19 of the address); the CCR's write mirrors are
        // other addresses of the same register.
        std::vector<uint32_t> bases = { r->address };
        if (r->narrow == NarrowAccess::CdWindow && r->size == 4) bases.push_back(r->address + 0x80000u);
        for (uint32_t base : bases)
            for (uint32_t w : { 1u, 2u, 4u })
                for (int write = 1; write >= 0; --write)
                {
                    if (w != 1 && IsPad(*r)) continue;   // a wider access reaches the register beside it, which says so
                    const uint64_t start = r->address & ~(w - 1);
                    for (uint64_t a = start; a < uint64_t(r->address) + r->size; a += w)   // 64 bits: RTCOR ends at 2^32
                    {
                        const int32_t off = static_cast<int32_t>(static_cast<uint32_t>(a) - r->address);
                        const uint32_t access = base + static_cast<uint32_t>(off);
                        out.push_back(WidthProbe{ r, w, write != 0, off, access, WidthCellFor(*r, w, write != 0, off, access) });
                    }
                }
        if (r->address == 0xFFFFFE92u)
            for (uint32_t w : { 1u, 2u })
            {
                const uint32_t access = 0xFFFFFE93u + (w - 1);   // the first mirror at the width's alignment: 0xFE93, 0xFE94
                out.push_back(WidthProbe{ r, w, true, 0, access, WidthCellFor(*r, w, true, 0, access) });
            }
    }
    return out;
}

uint32_t AccessAddress(uint32_t address, uint32_t size)
{
    if (size < 2 || !(address & (size - 1))) return address;
    // On-chip, MemRead / MemWrite case 7 hand the handlers the unaligned address. From 0xFFFFFF00 up they align it themselves
    // (`A &= 0xFE` for a word, `A &= 0xFC` for a long: the address the bus would have made); below it they switch on A & 0xFF as
    // given, so a word at 0xFFFFFE11 is the register at 0xFFFFFE11 (A5-29).
    if ((address >> 29) == 7 && !(address & 0x100u)) return address;
    return address & ~(size - 1);
}

namespace
{
// An access below 0xFFFFFF00 on-chip that reaches the register at its own address only: a word at an odd address, and a long
// (a read returns 0 and reads nothing; a write is made on the register at A & 0xFF as given). 'canonical' is the canonical address.
bool OwnAddressOnly(uint32_t address, uint32_t canonical, uint32_t size)
{
    return (address >> 29) == 7 && canonical < 0xFFFFFF00u && ((size == 2 && (address & 1u)) || size == 4);
}
}  // namespace

std::vector<const HardwareRegister*> HardwareRegistersReached(uint32_t address, uint32_t size, bool write)
{
    std::vector<const HardwareRegister*> out;
    address = AccessAddress(address, size);
    uint32_t canonical = 0;
    if (size != 0 && HardwareAlias(address, canonical) && OwnAddressOnly(address, canonical, size))
    {
        if (size == 4 && !write) return out;   // sh7095.inc OnChipRegRead_INLINE_u32, A & 0x100 clear: SetPEX and `return ret` with ret = 0
        const HardwareRegister* r = FindHardwareRegister(canonical);
        if (r) out.push_back(r);
        return out;
    }
    return HardwareRegistersCovering(address, size);
}

std::vector<CoveredRegister> CoveredRegisters(uint32_t address, uint32_t size, bool write)
{
    std::vector<CoveredRegister> out;
    uint32_t first = 0;
    address = AccessAddress(address, size);
    if (size == 0 || !HardwareAlias(address, first)) return out;
    // A byte at an even SMPC address reaches the bus word's unused lane.
    if (size == 1 && !(first & 1u))
        for (const HardwareRegister& v : UnusedLanes())
            if (v.size == 1 && v.address == first)
            {
                out.push_back(CoveredRegister{ &v, WidthCellFor(v, 1, write, 0, address) });
                break;
            }
    for (const HardwareRegister* reg : HardwareRegistersReached(address, size, write))
    {
        const int32_t off = static_cast<int32_t>(first) - static_cast<int32_t>(reg->address);
        out.push_back(CoveredRegister{ reg, WidthCellFor(*reg, size, write, off, address) });
    }
    return out;
}

namespace
{
// Backup RAM, 0x00180000-0x001FFFFF through either partition: a byte device on the low lane of the 16-bit bus, like the
// SMPC (ss.c BusRW_DB_CS0_*: the store is `if(A & 1)` for a byte and the low byte for a word; a read is
// `0xFF00 | BackupRAM[(A >> 1) & 0x7FFF]`).
bool InBackupRam(uint32_t address)
{
    if ((address >> 29) > 1) return false;
    const uint32_t a = address & 0x07FFFFFFu;
    return a >= 0x00180000u && a <= 0x001FFFFFu;
}

std::string BackupRamEffect(uint32_t address, uint32_t size, bool write)
{
    const std::string kind = " (backup RAM is a byte device on the low lane of the 16-bit bus: its bytes are at the odd addresses)";
    if (size == 1)
    {
        if (address & 1u) return std::string();
        return write ? "a 1-byte write to backup RAM at an even address does nothing" + kind
                     : "a 1-byte read of backup RAM at an even address returns 0xFF, not a byte of it" + kind;
    }
    if (size == 2)
        return write ? "a 2-byte write to backup RAM stores the low byte of the value only; the high byte is lost" + kind
                     : "a 2-byte read of backup RAM returns 0xFF00 or the byte, in the low byte" + kind;
    return write ? "a 4-byte write to backup RAM is two 16-bit writes, high half first, each storing its low byte only (bits 23-16 then 7-0)" + kind
                 : "a 4-byte read of backup RAM is two 16-bit reads: each half is 0xFF00 or a byte, so only bits 23-16 and 7-0 carry data" + kind;
}
}  // namespace

std::vector<NarrowEffect> NarrowAccessEffects(uint32_t address, uint32_t size, bool write)
{
    std::vector<NarrowEffect> out;
    const std::vector<CoveredRegister> covered = CoveredRegisters(address, size, write);
    const uint32_t reach = AccessAddress(address, size);
    const uint32_t partition = address >> 29;
    const std::string width = std::to_string(size) + "-byte";
    auto say = [&](const HardwareRegister* reg, const std::string& text, bool caution) {
        out.push_back(NarrowEffect{ reg, text, caution });
    };
    const HardwareRegister* first = covered.empty() ? nullptr : covered[0].reg;

    // A misaligned access is an address error and is made at the aligned address (A5-25): the register it reaches
    // is that one's, and the rest of this function describes that access. On-chip, the handlers at 0xFFFFFF00 and up
    // align it themselves (the same address); below it they decode it as given (A5-29).
    uint32_t onchipCanon = 0;
    const bool onChip = partition == 7 && HardwareAlias(reach, onchipCanon);
    const bool lowHalf = onChip && onchipCanon < 0xFFFFFF00u;
    if (size > 1 && (address & (size - 1)))
    {
        std::string text = "a misaligned " + width + (write ? " write" : " read") + " at " + Hex8s(address) +
                           " is an address error on the SH-2 (an access must be aligned to its width)";
        const std::string name = NameAt(reach).empty() ? std::string() : " (" + NameAt(reach) + ")";
        if (lowHalf)
            text += "; the on-chip handlers below 0xFFFFFF00 decode the address as given, so the access is made at " + Hex8s(reach) +
                    (name.empty() ? std::string(" (no register is named there)") : name);
        else
        {
            uint32_t canon = 0;
            text += "; the access is made at " + Hex8s(reach) + (HardwareAlias(reach, canon) ? name : std::string());
        }
        say(first, text, true);
    }

    // A long over registers that each take their half: the manuals document some of them as one
    // register (the scroll values, the UBC's addresses), the rest are a pair accessed as one.
    size_t halves = 0;
    for (const CoveredRegister& c : covered) halves += c.cell.rule == WidthRule::HalfOfLong;
    if (covered.size() > 1 && halves == covered.size())
    {
        std::string names;
        for (size_t i = 0; i < covered.size(); ++i)
            names += (i == 0 ? "" : i + 1 == covered.size() ? " and " : ", ") + std::string(covered[i].reg->name);
        const char* whole = DocumentedWideAccess(reach, size);
        if (whole)
            say(covered[0].reg, std::string(whole) + ": one " + width + (write ? " write" : " read") + " of " + names +
                                    ", as documented (shown under the first)", false);
        else
            say(covered[0].reg, "one " + width + (write ? " write" : " read") + " covers " + names + " (shown under the first)", true);
    }

    for (const CoveredRegister& c : covered)
    {
        const std::string text = WidthText(*c.reg, c.cell, size, write, reach);
        if (!text.empty()) say(c.reg, text, true);
        // The B bus reads 32 bits: the register 2 bytes on is read as well, effect and all.
        if ((c.cell.flags & kWidthReadsNext) && size < 4)
        {
            const HardwareRegister* next = FindHardwareRegister(reach | 2u);
            if (next && next != c.reg && next->onRead && next->access != RegisterAccess::WriteOnly)
                say(next, "a " + width + " read at " + std::string(c.reg->name) + " also reads " + next->name +
                              " (the bus reads 32 bits): " + next->onRead, false);
        }
    }

    // A long access to an on-chip address below 0xFFFFFF00 is an address error whether or not a register is there (sh7095.inc:
    // OnChipRegWrite_u32 / RegRead_u32 raise it in the half below). Mednafen then makes the access, once, on the register at the
    // long's own address: a read returns 0 and reads nothing, a write reaches that register alone (`switch(A & 0xFF)`), so this
    // is one caution for the access and not one for each register under its four bytes (A5-31).
    if (lowHalf && size == 4)
    {
        // (the bytes below the split only: a long at 0xFFFFFEFE has two bytes above it, in the other half, and DVSR's handler is not
        // the one that runs, A5-41)
        const std::vector<const HardwareRegister*> under = HardwareRegistersCovering(reach, std::min<uint32_t>(4u, 0x100u - (reach & 0xFFu)));
        const HardwareRegister* own = write ? FindHardwareRegister(reach) : nullptr;
        const HardwareRegister* head = own ? own : under.empty() ? nullptr : under[0];
        std::vector<const HardwareRegister*> others;
        for (const HardwareRegister* u : under)
            if (u != own) others.push_back(u);
        auto names = [](const std::vector<const HardwareRegister*>& v) {
            std::string t;
            for (size_t i = 0; i < v.size(); ++i) t += (i == 0 ? "" : i + 1 == v.size() ? " and " : ", ") + std::string(v[i]->name);
            return t;
        };
        std::string text;
        if (head)
            text = "a 4-byte " + std::string(write ? "write to " : "read of ") + head->name + ", " + (head->size == 1 ? "an " : "a ") +
                   std::to_string(unsigned(head->size) * 8) + "-bit register, is an address error on the SH-2: the registers at "
                   "0xFFFFFE00-0xFFFFFEFF take byte and word accesses only";
        else
            text = "a 4-byte " + std::string(write ? "write" : "read") + " at " + Hex8s(address) + " is an address error on the SH-2: no register is "
                   "named there, and the on-chip registers at 0xFFFFFE00-0xFFFFFEFF take byte and word accesses only";
        if (!write)
            text += "; Mednafen then returns 0 and reads " + (under.empty() ? std::string("nothing") : "none of " + names(under));
        else if (!own)
            text += "; Mednafen then writes nothing" + (others.empty() ? std::string() : ": " + names(others) + (others.size() == 1 ? " is" : " are") + " not written");
        else
        {
            text += "; Mednafen then writes only " + std::string(own->name) + ", the register at " + Hex8s(reach) + ", ";
            if (own->narrow == NarrowAccess::OnChipSci) text += "which it does not decode: nothing is stored";
            else if (own->access == RegisterAccess::KeyedWrite)
                text += "which a 4-byte write does not write (the watchdog takes only a keyed word); the write cancels a pending SH2_RSTCSR WOVF clear";
            else if (own->size == 2 && (reach & 1u)) text += "which a 4-byte write at its low byte does not reach: nothing is stored";
            else if (own->size == 2) text += "with bits 15-8 of the value in its high byte (the low byte is left as it was)";
            else text += "with bits 7-0 of the value";
            if (!others.empty()) text += "; " + names(others) + (others.size() == 1 ? " is" : " are") + " not written";
        }
        say(head, text, true);
    }
    // A byte access at or above 0xFFFFFF00 is one whether or not a register is there (OnChipRegWrite_u8 / RegRead_u8, A & 0x100
    // set); the registers say so themselves, this is for the addresses nothing is named at (A5-27).
    if (onChip && !lowHalf && size == 1 && covered.empty())
        say(first, "a 1-byte " + std::string(write ? "write" : "read") + " at " + Hex8s(address) + " is an address error on the SH-2: no register is named there, and the on-chip "
                   "registers at 0xFFFFFF00-0xFFFFFFFF take word and longword accesses only", true);

    // Backup RAM, which the map does not name.
    if (InBackupRam(reach))
    {
        const std::string text = BackupRamEffect(reach, size, write);
        if (!text.empty()) say(first, text, true);
    }

    // An access through 0x4xxxxxxx or 0xAxxxxxxx is not a memory access: Mednafen runs it as an associative purge of the cache line
    // for the address (sh7095.inc MemRead / MemWrite cases 2 and 5; a read returns all ones). The slave idle loop in the games does it.
    if (partition == 2 || partition == 5)
    {
        const std::string b = Fmt("an access at %s is an associative purge, not a memory access: it invalidates the cache line for 0x%08X%s",
                      Hex8s(address).c_str(), reach & 0x1FFFFFF0u, write ? "" : " (a read returns all ones)");
        say(nullptr, b, false);
    }

    // An access through 0x6xxxxxxx is the cache's address array, not memory (sh7095.inc MemRead / MemWrite case 3, the volatile range
    // already keeps it as written): entry (A >> 4) & 0x3F of the way SH2_CCR selects (bits 7-6). A write sets its tag from the address
    // (valid is bit 2 inverted) and its LRU bits from bits 9-4 of the value; a read returns tag, valid and LRU (A5-34). The 0x8xxxxxxx and
    // 0xCxxxxxxx data array is RAM that a cache fill overwrites with the cache on: ordinary memory to the decompiler, as the games that
    // use the cache as RAM want it.
    if (partition == 3)
    {
        const std::string b = Fmt(
                      "an access at %s is the cache address array, not a memory access: entry %u of the way SH2_CCR selects; a write sets its tag "
                      "(from the address, valid is bit 2 inverted) and its LRU bits (bits 9-4 of the value), a read returns tag, valid and LRU",
                      Hex8s(address).c_str(), (reach >> 4) & 0x3Fu);
        say(nullptr, b, false);
    }

    // A read through the cached partition (0x0xxxxxxx) of a register area is a cache read when the cache is on (CCR.CE, which
    // the games set): a hit returns the cached bytes without a bus access, so a poll of a register never sees it change, and
    // a miss reads the whole 16-byte line, every register in it with its effect (sh7095.inc MemRead case 0 and the line fill
    // of four ExtBusRead). A write is written through (MemWrite case 0 falls into case 1). A5-19.
    uint32_t canon = 0;
    if (!write && partition == 0 && (HardwareAlias(reach, canon) || InBackupRam(reach)) && canon < 0xFFFFFE00u)
    {
        const std::string b = Fmt(
                      "a read of %s through the cached partition is a cache read when the cache is on (SH2_CCR.CE): a hit returns the "
                      "cached bytes without reading the hardware, so a poll of it may never see it change, and a miss reads the whole "
                      "16-byte line 0x%08X-0x%08X, every register in it with its read effect; read it through 0x2xxxxxxx to bypass the cache",
                      first ? first->name : "hardware", reach & ~0xFu, reach | 0xFu);
        say(first, b, true);
    }
    return out;
}

const std::vector<HardwareRange>& HardwareRanges()
{
    static const std::vector<HardwareRange> ranges = BuildRanges();
    return ranges;
}

const char* DocumentedWideAccess(uint32_t address, uint32_t size)
{
    uint32_t a = 0;
    if (size != 4 || (address & 3) || !HardwareAlias(address, a)) return nullptr;
    // VDP2 ("VDP2 User's Manual"): the scroll values are 16- or 32-bit registers; the zoom
    // increments and the vertical-cell, line scroll, line colour and back screen table addresses
    // 32-bit ones.
    struct Wide { uint32_t address; const char* name; };
    static const Wide kWide[] = {
        { 0x05F80070u, "VDP2_SCXN0" }, { 0x05F80074u, "VDP2_SCYN0" }, { 0x05F80078u, "VDP2_ZMXN0" },
        { 0x05F8007Cu, "VDP2_ZMYN0" }, { 0x05F80080u, "VDP2_SCXN1" }, { 0x05F80084u, "VDP2_SCYN1" },
        { 0x05F80088u, "VDP2_ZMXN1" }, { 0x05F8008Cu, "VDP2_ZMYN1" }, { 0x05F80090u, "VDP2_SCXN2:SCYN2" },
        { 0x05F80094u, "VDP2_SCXN3:SCYN3" }, { 0x05F8009Cu, "VDP2_VCSTA" }, { 0x05F800A0u, "VDP2_LSTA0" },
        { 0x05F800A4u, "VDP2_LSTA1" },
        { 0x05F800A8u, "VDP2_LCTA" }, { 0x05F800ACu, "VDP2_BKTA" },
        // The UBC's address, mask and data registers are word halves of one longword (SH7604).
        { 0xFFFFFF40u, "SH2_BARA" }, { 0xFFFFFF44u, "SH2_BAMRA" }, { 0xFFFFFF60u, "SH2_BARB" },
        { 0xFFFFFF64u, "SH2_BAMRB" }, { 0xFFFFFF70u, "SH2_BDRB" }, { 0xFFFFFF74u, "SH2_BDMRB" },
    };
    for (const Wide& w : kWide)
        if (w.address == a) return w.name;
    return nullptr;
}

std::string KeyedPortCaution(const HardwareRegister& r, uint32_t size)
{
    return std::string(r.name) + " takes no " + std::to_string(size) + "-byte write: it is written by a keyed word at " +
           (std::string(r.name) == "SH2_RSTCSR" ? "0xFFFFFE82" : "0xFFFFFE80") +
           "; this write does not write it, and it cancels a pending SH2_RSTCSR WOVF clear (a read arms it)";
}

bool KeyedWrite(uint32_t address, uint32_t size, bool valueKnown, uint32_t value, std::string& text, bool& ignored)
{
    uint32_t a = 0;
    ignored = false;
    // The bus state controller (BCR1 to RTCOR): a 4-byte write takes effect only with 0xA55A in its
    // upper half (sh7095.inc SH7095_OnChipRegWrite_u32, `(V & 0xFFFF0000) == 0xA55A0000`); the
    // word and byte writes never do (the width table's KeyedWordWrite and AddressError).
    if (size == 4 && HardwareAlias(address, a) && a >= 0xFFFFFFE0u && a <= 0xFFFFFFF8u && !(a & 3u))
    {
        const HardwareRegister* reg = FindHardwareRegister(a);
        const std::string name = reg ? reg->name : "the bus state controller";
        if (!valueKnown)
            text = name + " takes a 4-byte write only with 0xA55A in its upper half; any other value does nothing";
        else if ((value & 0xFFFF0000u) == 0xA55A0000u)
        {
            text = name + Fmt(" keyed write: the low half 0x%04X is taken (0xA55A in the upper half is the key)", value & 0xFFFFu);
        }
        else
        {
            text = "write to " + name + Fmt(" without 0xA55A in the upper half (it is 0x%04X): the write does nothing", value >> 16);
            ignored = true;
        }
        return true;
    }
    if (size != 2 || !HardwareAlias(address, a) || (a != 0xFFFFFE80u && a != 0xFFFFFE82u)) return false;
    // SH7604 12.2.4: WTCNT and WTCSR are written by a word at FE80 whose upper byte is the key
    // (0x5A for WTCNT, 0xA5 for WTCSR); RSTCSR by a word at FE82, 0xA500 clearing WOVF and 0x5A
    // with data setting RSTE and RSTS. Any other word is ignored. Each takes effect only on
    // conditions the write alone does not show: a flag is cleared only if a read saw it set, and
    // Mednafen loads WTCNT only while WTCSR's TME is set (sh7095.inc). WTCSR keeps bits 6-5 and 2-0
    // of the data (V & 0x67; it reads back with bits 4-3 set); with TME (bit 5) set the write clears
    // SBYCR's SBY, with TME clear it clears OVF whatever a read saw and resets WTCNT (A5-21). Every
    // write in the area, an ignored word too, cancels a pending RSTCSR WOVF clear (RSTCSRM = 0).
    const uint32_t key = (value >> 8) & 0xFFu, data = value & 0xFFu;
    if (a == 0xFFFFFE80u)
    {
        if (!valueKnown)
            text = "keyed watchdog write: SH2_WTCNT when the upper byte is 0x5A (taken while SH2_WTCSR's TME is "
                   "set), SH2_WTCSR when it is 0xA5 (with TME clear, OVF is cleared and SH2_WTCNT reset; with TME set, "
                   "OVF is cleared only if a read saw it set and SBYCR's SBY is cleared)";
        else if (key == 0x5Au)
        {
            text = Fmt("keyed watchdog write: requests SH2_WTCNT = 0x%02X (taken while SH2_WTCSR's TME is set)", data);
        }
        else if (key == 0xA5u)
        {
            if (data & 0x20u)
                text = Fmt("keyed watchdog write: SH2_WTCSR = 0x%02X (TME is set: OVF is cleared only if a read saw it set, "
                                           "and SBYCR's SBY bit is cleared)", data & 0x67u);
            else
                text = Fmt("keyed watchdog write: SH2_WTCSR = 0x%02X (TME is clear: OVF is cleared whatever a read saw, "
                                           "and SH2_WTCNT is reset to 0)", data & 0x67u);
        }
        else
        {
            text = Fmt("watchdog write with key 0x%02X, which neither SH2_WTCNT (0x5A) nor SH2_WTCSR (0xA5) takes: ignored "
                                       "(it still cancels a pending SH2_RSTCSR WOVF clear)", key);
            ignored = true;
        }
        return true;
    }
    if (!valueKnown)
        text = "keyed watchdog write to SH2_RSTCSR: 0xA500 clears WOVF if a read saw it set, 0x5A with data sets "
               "RSTE and RSTS";
    else if (value == 0xA500u) text = "keyed watchdog write: clears SH2_RSTCSR's WOVF, if a read saw it set";
    else if (key == 0x5Au) text = Fmt("keyed watchdog write: SH2_RSTCSR RSTE/RSTS = 0x%02X", data);
    else
    {
        text = Fmt("SH2_RSTCSR write 0x%04X is neither 0xA500 nor 0x5A with data: ignored "
                                   "(it still cancels a pending WOVF clear)", value & 0xFFFFu);
        ignored = true;
    }
    return true;
}

namespace
{

// The 16-bit register at 'reg' (even) as a write of 'size' bytes at 'address' reaches it: whether it does, which of its bits the write
// carries and what they are. A byte at the register's address carries bits 15-8, at +1 bits 7-0; a word at the register all of them;
// a long at the register carries its upper half, one 2 below it its lower (the 16-bit bus takes a long as two words).
struct WordLane
{
    bool reached = false;
    uint16_t mask = 0;
    uint16_t value = 0;
};

WordLane LaneOf(uint32_t reg, uint32_t address, uint32_t size, uint32_t value)
{
    WordLane l;
    if (size == 1 && address == reg) l = WordLane{ true, 0xFF00u, static_cast<uint16_t>((value & 0xFFu) << 8) };
    else if (size == 1 && address == reg + 1) l = WordLane{ true, 0x00FFu, static_cast<uint16_t>(value & 0xFFu) };
    else if (size == 2 && address == reg) l = WordLane{ true, 0xFFFFu, static_cast<uint16_t>(value & 0xFFFFu) };
    else if (size == 4 && address == reg) l = WordLane{ true, 0xFFFFu, static_cast<uint16_t>(value >> 16) };
    else if (size == 4 && address + 2 == reg) l = WordLane{ true, 0xFFFFu, static_cast<uint16_t>(value & 0xFFFFu) };
    return l;
}

const char* SmpcCommandName(uint32_t command)
{
    switch (command)
    {
    case 0x00: return "MSHON";
    case 0x02: return "SSHON";
    case 0x03: return "SSHOFF";
    case 0x06: return "SNDON";
    case 0x07: return "SNDOFF";
    case 0x08: return "CDON";
    case 0x09: return "CDOFF";
    case 0x0D: return "SYSRES";
    case 0x0E: return "CKCHG352";
    case 0x0F: return "CKCHG320";
    case 0x10: return "INTBACK";
    case 0x16: return "SETTIME";
    case 0x17: return "SETSMEM";
    case 0x18: return "NMIREQ";
    case 0x19: return "RESENAB";
    case 0x1A: return "RESDISA";
    default: return nullptr;
    }
}

}  // namespace

std::vector<std::string> WriteEffects(uint32_t address, uint32_t size, bool valueKnown, uint32_t value)
{
    std::vector<std::string> out;
    uint32_t a = 0;
    address = AccessAddress(address, size);
    if (!HardwareAlias(address, a)) return out;
    // SH2_CCR (sh7095.inc SH7095_SetCCR): CP, bit 4, purges the whole cache; a write elsewhere in the register's read mirrors does nothing.
    // A long write below 0xFFFFFF00 is made on the register at its own address (an address error first), so it purges as well.
    if (a == 0xFFFFFE92u && (address & 0x1FFu) == 0x92u && size <= 4)
    {
        if (!valueKnown)
            out.push_back("SH2_CCR: a write with CP (bit 4) set purges the whole cache");
        else if (value & 0x10u)
            out.push_back("SH2_CCR: CP (bit 4) is set, so this write purges the whole cache");
        return out;
    }
    // VDP1_PTMR (vdp1.c WriteReg case 2): bit 0 set starts drawing the command table. The register is 16 bits at 0x05D00004:
    // a word write or the long's high half carries it whole; a byte write carries bit 0 at the odd address only.
    if (a >= 0x05D00004u && a < 0x05D00006u && size <= 4)
    {
        const bool whole = size == 2 || size == 4 || (size == 1 && (address & 1u));
        const uint32_t bit0 = size == 4 ? (value >> 16) & 1u : value & 1u;
        if (!valueKnown || !whole)
            out.push_back("VDP1_PTMR: a write with bit 0 set starts drawing the command table");
        else if (bit0)
            out.push_back("VDP1_PTMR: bit 0 is set, so this write starts drawing the command table");
    }
    // VDP1_ENDR (vdp1.c WriteReg case 0x6): `if(DrawingActive) { DrawingActive = false; ... }` whatever is written, at any width
    // that reaches the register (a long at 0x05D0000C writes it with its high half).
    for (const HardwareRegister* reg : HardwareRegistersCovering(a, size))
        if (reg->address == 0x05D0000Cu)
            out.push_back("VDP1_ENDR: any write ends the drawing in progress, whatever is written");
    // CD_CR4 (cdb.c CDB_Write_DBM case 0x9): `if(mask == 0xFFFF) { CommandPending = true; ...}`: a 16-bit write issues the command whatever
    // it holds (the CS2 long write is two of them); a byte write does not (the caution of the width table says so) (A5-40).
    if (size >= 2)
        for (const HardwareRegister* reg : HardwareRegistersCovering(a, size))
            if (reg->address == 0x05890024u)
                out.push_back("CD_CR4: a 16-bit write issues the command in CD_CR1 to CD_CR4, whatever is written");
    // The SCU (scu.inc SCU_RegRW_DB_u8/u16/u32_W1, `switch(A & 0xFC)`): a write takes the bits of its lane and acts on the whole
    // register. The lane of a byte at +3 is bits 7-0, of a word at +2 bits 15-0, of a long all 32; the others carry no bit 0 or 8.
    if (a >= 0x05FE0000u && a <= 0x05FEFFFFu)
    {
        const uint32_t reg = a & 0xFCu, off = a & 3u;
        const bool lane0 = (size == 4 && off == 0) || (size == 2 && off == 2) || (size == 1 && off == 3);   // carries bit 0
        const bool lane8 = (size == 4 && off == 0) || (size == 2 && off == 2) || (size == 1 && off == 2);   // carries bit 8
        const bool bit0 = valueKnown && (value & 1u);
        const bool bit8 = valueKnown && (value & 0x100u);
        const char* const kLevels[] = { "0", "1", "2" };
        if ((reg == 0x10 || reg == 0x30 || reg == 0x50) && lane0)
        {
            // DnEN: `if((tmp & 0x1) && d->Enable && d->SF == 0x7)` starts the transfer; Enable is bit 8 of the value written, or what it
            // was before when the lane is a byte at +3.
            const std::string level = kLevels[(reg >> 5) & 3];
            const std::string name = "SCU_D" + level + "EN";
            const std::string start = "starts level-" + level + " DMA when its start factor (SCU_D" + level + "MD bits 2-0) is 7 (start by this register)";
            if (lane8)
            {
                if (!valueKnown) out.push_back(name + ": a write with bits 8 and 0 set " + start);
                else if (bit0 && bit8) out.push_back(name + ": bits 8 and 0 are set, so this write " + start);
            }
            else if (!valueKnown) out.push_back(name + ": a write with bit 0 set " + start + ", when bit 8 is already set");
            else if (bit0) out.push_back(name + ": bit 0 is set, so this write " + start + ", when bit 8 is already set");
        }
        // PPAF (case 0x80): every write acts on the DSP program, from the value: bit 25 pauses, else bit 26 unpauses, else bit 16 runs it
        // and its absence stops a running program, bit 17 steps it; bit 15 loads the program counter from the value (A5-40).
        if (reg == 0x80)
            out.push_back("SCU_PPAF: a write acts on the DSP program whatever is written: bit 25 pauses it, else bit 26 unpauses it, "
                          "else bit 16 set runs it and clear stops it if it runs, bit 17 steps it; bit 15 loads its program counter from the value");
        // AIACK: `if(*DB & mask & 0x0001) { ABusIProhibit = 0; ABusIRQCheck(); }`.
        if (reg == 0xA8 && lane0 && (!valueKnown || bit0))
            out.push_back(std::string("SCU_AIACK: ") + (valueKnown ? "bit 0 is set, so this write" : "a write with bit 0 set") +
                          " acknowledges the A-bus interrupt (the SCU may raise it again)");
        // DSTP: `if(*DB & mask & 0x1) { for each level: Active = false; GoGoGadget = false; }`.
        if (reg == 0x60 && lane0 && (!valueKnown || bit0))
            out.push_back(std::string("SCU_DSTP: ") + (valueKnown ? "bit 0 is set, so this write" : "a write with bit 0 set") +
                          " stops DMA on every level");
        // T1MD: `Timer_Enable = tmp & 1; if(!Timer_Enable) Timer0_Counter = 0;` (the old enable bit when the lane has no bit 0).
        if (reg == 0x98 && lane0 && (!valueKnown || !bit0))
            out.push_back(std::string("SCU_T1MD: ") + (valueKnown ? "bit 0 (the timer enable) is clear, so this write" : "a write with bit 0 clear") +
                          " also resets timer 0's counter");
    }
    // Writes whose action is chosen by the value written (A5-55): a flag or a command taken from the value, not a store the line shows.
    // The SCSP's common and slot registers (scsp.inc SS_SCSP_RW_u16_W1): a write takes the bits of its lane, the others keep their state.
    if (a >= 0x05B00000u && a <= 0x05B00FFFu && size <= 4)
    {
        const uint32_t off = a & 0xFFFu;
        // DTLG (case 0x0B): EX, bit 12, is latched into DMA_Execute and SS_SCSP_RunDMA runs: it starts the SCSP DMA.
        const WordLane dtlg = LaneOf(0x05B00416u, a, size, value);
        if (dtlg.reached && (dtlg.mask & 0x1000u) && (!valueKnown || (dtlg.value & 0x1000u)))
            out.push_back(std::string("SCSP_DTLG: ") + (valueKnown ? "EX (bit 12) is set, so this write" : "a write with EX (bit 12) set") +
                          " starts the SCSP DMA set up in SCSP_DMEAL, SCSP_DMEAH_DRGA and this register (DTLG is the length)");
        // A slot's word 0 (case 0x00): KYONEX, bit 12, is latched into KeyExecute and not stored; every slot is then keyed on or off by its KYONB.
        if (off < 0x400u && ((off >> 1) & 0xFu) == 0)
        {
            const WordLane kyonex = LaneOf(0x05B00000u | (off & ~0x1Fu), a, size, value);
            if (kyonex.reached && (kyonex.mask & 0x1000u) && (!valueKnown || (kyonex.value & 0x1000u)))
            {
                out.push_back(Fmt("SCSP slot %u word 0: ", (off >> 5) & 0x1Fu) + (valueKnown ? "KYONEX (bit 12) is set, so this write" : "a write with KYONEX (bit 12) set") +
                              " keys every slot on or off by its own KYONB (bit 11); KYONEX is not stored");
            }
        }
        // SCIRE (case 0x11) and MCIRE (case 0x17): the written bits clear those pending interrupts (SCIPD, MCIPD &= ~value).
        const struct { uint32_t reg; const char* name; const char* whose; const char* pending; } kClears[] = {
            { 0x05B00422u, "SCSP_SCIRE", "sound-CPU", "SCSP_SCIPD" }, { 0x05B0042Eu, "SCSP_MCIRE", "main-CPU", "SCSP_MCIPD" } };
        for (const auto& c : kClears)
        {
            const WordLane l = LaneOf(c.reg, a, size, value);
            if (!l.reached) continue;
            if (valueKnown && (l.value & l.mask) == 0) continue;   // clears nothing
            if (valueKnown)
            {
                out.push_back(Fmt("%s: clears the pending %s interrupts (%s) whose bits are set in the value written (0x%X here)",
                                  c.name, c.whose, c.pending, static_cast<unsigned>(l.value & l.mask)));
            }
            else
                out.push_back(std::string(c.name) + ": clears the pending " + c.whose + " interrupts (" + c.pending +
                              ") whose bits are set in the value written");
        }
    }
    // SMPC_COMREG (smpc.c SMPC_Write case 0x0F, PendingCommand = V): the byte written is the command the SMPC then executes. A word or a long
    // reaches the byte by its lane, and it is always the low byte of the value.
    for (const HardwareRegister* reg : HardwareRegistersCovering(a, size))
        if (reg->address == 0x0010001Fu)
        {
            if (!valueKnown)
                out.push_back("SMPC_COMREG: the byte written is the SMPC command it executes (INTBACK is 0x10, RESENAB 0x19, SYSRES 0x0D, "
                              "SNDON 0x06, SNDOFF 0x07, ...)");
            else
            {
                const uint32_t cmd = value & 0xFFu;
                const char* name = SmpcCommandName(cmd);
                out.push_back(name ? Fmt("SMPC_COMREG: issues SMPC command 0x%02X (%s)", cmd, name)
                                   : Fmt("SMPC_COMREG: issues SMPC command 0x%02X (a value the SMPC does not know)", cmd));
            }
        }
    // VDP1_FBCR (vdp1.c WriteReg case 0x1, FBManualPending |= value & 0x2): bit 1 (FCM) requests a manual frame-buffer change, which the end of
    // the frame carries out (the buffers swap if bit 0, FCT, is set, else only the erase starts). The register is written whole, the byte not
    // written coming from the data bus: a byte at the even address leaves bit 1 unknown.
    if (a >= 0x05D00000u && a < 0x05D00004u && size <= 4)
    {
        const WordLane l = LaneOf(0x05D00002u, a, size, value);
        const bool carries = size == 2 || (size == 4 && a == 0x05D00000u) || (size == 1 && (a & 1u));
        const bool known = l.reached && carries && valueKnown;
        if (l.reached && (!known || (l.value & 2u)))
            out.push_back(std::string("VDP1_FBCR: ") + (known ? "bit 1 (FCM) is set, so this write" : "a write with bit 1 (FCM) set") +
                          " requests a manual frame-buffer change, carried out at the end of the frame: the buffers swap if bit 0 (FCT) is then set, "
                          "else only the erase starts");
    }
    return out;
}

std::vector<HardwareRange> VolatileAliases(uint32_t first, uint32_t last)
{
    // Outward to whole longwords: the engine asks whether an access is volatile at its first byte,
    // so a word or longword that only ends inside the range would otherwise not be. SH-2 accesses
    // are aligned, so every access touching the range then starts in it -- at the cost of the
    // bytes before and after it in the same longword.
    if (last < first) return std::vector<HardwareRange>();   // inverted: no range (checked before widening)
    first &= ~3u;
    last |= 3u;
    const uint64_t lo = first, hi = last;
    const uint64_t busEnd = 0x40000000ull;   // partitions 000 and 001
    std::vector<HardwareRange> out;
    // Whatever lies outside the bus partitions is taken as given.
    if (hi >= busEnd) out.push_back(HardwareRange{ static_cast<uint32_t>(std::max<uint64_t>(lo, busEnd)), last });
    if (lo >= busEnd) return out;

    // The bus part, as physical (27-bit) intervals: bits 31-27 do not select memory on the bus,
    // so an interval spanning a whole 128 MiB period covers all of it, and one crossing a period
    // boundary wraps.
    const uint64_t period = 0x08000000ull;
    const uint64_t a = lo, b = std::min<uint64_t>(hi, busEnd - 1);
    std::vector<std::pair<uint32_t, uint32_t>> phys;
    if (b - a + 1 >= period)
        phys.emplace_back(0u, 0x07FFFFFFu);
    else
    {
        const uint32_t pa = static_cast<uint32_t>(a % period), pb = static_cast<uint32_t>(b % period);
        if (pa <= pb) phys.emplace_back(pa, pb);
        else
        {
            phys.emplace_back(pa, 0x07FFFFFFu);
            phys.emplace_back(0u, pb);
        }
    }
    // Work RAM repeats every 1 MiB: high work RAM across 0x06000000-0x07FFFFFF, and low work RAM
    // (1 MiB, Mednafen's "Low RAM") across 0x00200000-0x003FFFFF (review of 0e3cd12). Each
    // physical interval's part in a repeating area reaches the same bytes through every repeat.
    const uint32_t mib = 0x00100000u;
    struct Repeat { uint32_t first, last; } const repeats[] = { { 0x00200000u, 0x003FFFFFu }, { 0x06000000u, 0x07FFFFFFu } };
    std::vector<std::pair<uint32_t, uint32_t>> pieces;
    for (const auto& p : phys)
    {
        uint64_t next = p.first;   // the start of the part of p not yet placed
        for (const Repeat& rep : repeats)
        {
            if (next > p.second) break;
            if (next < rep.first)   // the part below this repeating area is taken as it is
            {
                pieces.emplace_back(static_cast<uint32_t>(next), std::min(p.second, rep.first - 1));
                next = rep.first;
            }
            if (next > p.second || next > rep.last) continue;
            const uint32_t s = static_cast<uint32_t>(next), e = std::min(p.second, rep.last);
            if (e - s + 1 >= mib) pieces.emplace_back(rep.first, rep.last);
            else
            {
                const uint32_t sa = s & (mib - 1), ea = e & (mib - 1);
                for (uint32_t r = rep.first; r < rep.last; r += mib)
                {
                    if (sa <= ea) pieces.emplace_back(r + sa, r + ea);
                    else
                    {
                        pieces.emplace_back(r + sa, r + mib - 1);
                        pieces.emplace_back(r, r + ea);
                    }
                }
            }
            next = static_cast<uint64_t>(rep.last) + 1;
        }
        if (next <= p.second) pieces.emplace_back(static_cast<uint32_t>(next), p.second);
    }
    // ...through both partitions and every 128 MiB period.
    for (uint32_t partition : { 0x00000000u, 0x20000000u })
        for (uint32_t repeat = 0; repeat < 4; ++repeat)
            for (const auto& p : pieces)
                out.push_back(HardwareRange{ partition | (repeat << 27) | p.first, partition | (repeat << 27) | p.second });
    return out;
}

bool InHardwareRange(uint32_t address)
{
    const std::vector<HardwareRange>& r = HardwareRanges();
    auto it = std::upper_bound(r.begin(), r.end(), address,
                               [](uint32_t v, const HardwareRange& x) { return v < x.first; });
    return it != r.begin() && address <= std::prev(it)->last;
}

bool MemoryBytesOf(uint32_t address, uint32_t size, MemoryBytes& out)
{
    if ((address >> 29) > 1 || size == 0) return false;   // the partitions that reach memory: cached and cache-through
    const uint32_t a = address & 0x07FFFFFFu;
    struct Area { uint32_t first, last, mask; MemoryKind kind; };
    static const Area kAreas[] = {
        { 0x00200000u, 0x003FFFFFu, 0x000FFFFFu, MemoryKind::WorkRamLow },
        { 0x02400000u, 0x027FFFFFu, 0x0027FFFFu, MemoryKind::ExtRam },   // (bit 21 selects the bank; bits 20 and 19 are not decoded: A5-65)
        { 0x05C00000u, 0x05C7FFFFu, 0x0007FFFFu, MemoryKind::Vdp1Vram },
        { 0x05C80000u, 0x05CFFFFFu, 0x0003FFFFu, MemoryKind::Vdp1FrameBuffer },
        { 0x05E00000u, 0x05EFFFFFu, 0x0007FFFFu, MemoryKind::Vdp2Vram },
        { 0x05F00000u, 0x05F7FFFFu, 0x000007FFu, MemoryKind::Vdp2ColorRam },
        { 0x06000000u, 0x07FFFFFFu, 0x000FFFFFu, MemoryKind::WorkRamHigh },
    };
    for (const Area& area : kAreas)
    {
        if (a < area.first || a > area.last) continue;
        const uint32_t off = a & area.mask;
        out.kind = area.kind;
        out.first = off;
        out.last = std::min<uint32_t>(off + size - 1, area.mask);   // (an access past the repeat is an address error anyway)
        return true;
    }
    return false;
}

const char* MemoryKindName(MemoryKind kind)
{
    switch (kind)
    {
    case MemoryKind::WorkRamHigh: return "high work RAM";
    case MemoryKind::WorkRamLow: return "low work RAM";
    case MemoryKind::Vdp1Vram: return "VDP1 VRAM";
    case MemoryKind::Vdp1FrameBuffer: return "the VDP1 frame buffer";
    case MemoryKind::Vdp2Vram: return "VDP2 VRAM";
    case MemoryKind::Vdp2ColorRam: return "VDP2 colour RAM";
    case MemoryKind::ExtRam: return "the extended RAM cartridge";
    }
    return "memory";
}

bool InColorRam(uint32_t address)
{
    MemoryBytes m;
    return MemoryBytesOf(address, 1, m) && m.kind == MemoryKind::Vdp2ColorRam;
}

}  // namespace decomp
}  // namespace sfe
