#include "Decompiler/SaturnHardwareMap.h"

#include <algorithm>
#include <iterator>

namespace sfe
{
namespace decomp
{

namespace
{
const RegisterAccess RW = RegisterAccess::ReadWrite;
const RegisterAccess RO = RegisterAccess::ReadOnly;
const RegisterAccess WO = RegisterAccess::WriteOnly;

struct Reg
{
    uint16_t offset;
    const char* name;
    RegisterAccess access;
    uint8_t size;   // 0: the block's
};

struct Block
{
    uint32_t base;
    uint8_t size;
    const Reg* regs;
    size_t count;
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
    { 0x05F, "SMPC_OREG31", RO }, { 0x061, "SMPC_SR", RO }, { 0x063, "SMPC_SF", RW },
    { 0x075, "SMPC_PDR1", RW }, { 0x077, "SMPC_PDR2", RW }, { 0x079, "SMPC_DDR1", WO },
    { 0x07B, "SMPC_DDR2", WO }, { 0x07D, "SMPC_IOSEL", WO }, { 0x07F, "SMPC_EXLE", WO },
};

// SCSP common control registers, named after the fields they hold ("SCSP User's Manual"). The
// per-slot block below 0x400 is left unnamed: 32 copies of one layout read better as offsets.
// SCIRE and MCIRE reset pending interrupts and read back nothing.
const Reg kScsp[] = {
    { 0x400, "SCSP_MVOL", RW }, { 0x402, "SCSP_RBL_RBP", RW }, { 0x404, "SCSP_MIBUF", RW },
    { 0x406, "SCSP_MOBUF", RW }, { 0x408, "SCSP_MSLC_CA", RW }, { 0x412, "SCSP_DMEAL", RW },
    { 0x414, "SCSP_DMEAH_DRGA", RW }, { 0x416, "SCSP_DTLG", RW }, { 0x418, "SCSP_TIMA", RW },
    { 0x41A, "SCSP_TIMB", RW }, { 0x41C, "SCSP_TIMC", RW }, { 0x41E, "SCSP_SCIEB", RW },
    { 0x420, "SCSP_SCIPD", RW }, { 0x422, "SCSP_SCIRE", WO }, { 0x424, "SCSP_SCILV0", RW },
    { 0x426, "SCSP_SCILV1", RW }, { 0x428, "SCSP_SCILV2", RW }, { 0x42A, "SCSP_MCIEB", RW },
    { 0x42C, "SCSP_MCIPD", RW }, { 0x42E, "SCSP_MCIRE", WO },
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
    { 0x000, "VDP2_TVMD", RW }, { 0x002, "VDP2_EXTEN", RW }, { 0x004, "VDP2_TVSTAT", RO },
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
// Mednafen scu.inc: the DMA addresses, DSTA, PPAF, PDD, IST, AIACK, RSEL and VER; every other
// register reads 0, which is why SBL keeps its own copy of IMS.
const Reg kScu[] = {
    { 0x000, "SCU_D0R", RW }, { 0x004, "SCU_D0W", RW }, { 0x008, "SCU_D0C", WO }, { 0x00C, "SCU_D0AD", WO },
    { 0x010, "SCU_D0EN", WO }, { 0x014, "SCU_D0MD", WO }, { 0x020, "SCU_D1R", RW }, { 0x024, "SCU_D1W", RW },
    { 0x028, "SCU_D1C", WO }, { 0x02C, "SCU_D1AD", WO }, { 0x030, "SCU_D1EN", WO }, { 0x034, "SCU_D1MD", WO },
    { 0x040, "SCU_D2R", RW }, { 0x044, "SCU_D2W", RW }, { 0x048, "SCU_D2C", WO }, { 0x04C, "SCU_D2AD", WO },
    { 0x050, "SCU_D2EN", WO }, { 0x054, "SCU_D2MD", WO }, { 0x060, "SCU_DSTP", WO }, { 0x07C, "SCU_DSTA", RO },
    { 0x080, "SCU_PPAF", RW }, { 0x084, "SCU_PPD", WO }, { 0x088, "SCU_PDA", WO }, { 0x08C, "SCU_PDD", RW },
    { 0x090, "SCU_T0C", WO }, { 0x094, "SCU_T1S", WO }, { 0x098, "SCU_T1MD", WO }, { 0x0A0, "SCU_IMS", WO },
    { 0x0A4, "SCU_IST", RW }, { 0x0A8, "SCU_AIACK", RW }, { 0x0B0, "SCU_ASR0", WO }, { 0x0B4, "SCU_ASR1", WO },
    { 0x0B8, "SCU_AREF", WO }, { 0x0C4, "SCU_RSEL", RW }, { 0x0C8, "SCU_VER", RO },
};

// The CD block's host interface in A-bus CS2: 16-bit registers every 4 bytes, the data FIFO
// first (Mednafen cdb.cpp CDB_Read/CDB_Write_DBM, scu.inc). The FIFO is read 32 bits at a time
// at 0x25818000 by the BIOS and SBL, so it is named there.
const Reg kCdBlock[] = {
    { 0x0008, "CD_HIRQ", RW }, { 0x000C, "CD_HIRQMSK", RW }, { 0x0018, "CD_CR1", RW },
    { 0x001C, "CD_CR2", RW }, { 0x0020, "CD_CR3", RW }, { 0x0024, "CD_CR4", RW },
};
const Reg kCdFifo[] = { { 0x0000, "CD_DATATRNS", RW, 4 } };

// The CPU-to-CPU signals: a 16-bit write anywhere in the 8 MiB sets the other SH-2's FRT input
// capture flag (SCU manual MINIT/SINIT; Docs/Saturn/SaturnMemory.txt note 4). Nothing reads back.
const Reg kMinit[] = { { 0x0000, "MINIT", WO } };   // the master signals the slave
const Reg kSinit[] = { { 0x0000, "SINIT", WO } };   // the slave signals the master

// The SH-2's own peripherals (Hitachi SH7604 hardware manual; Mednafen sh7095.inc). The FRT
// counter is two bytes read high first: the high read latches the low byte. The watchdog is read
// a byte at a time at FE80/FE81/FE83 but written as keyed words at FE80/FE82.
const Reg kOnChip[] = {
    { 0x000, "SH2_SMR", RW, 1 }, { 0x001, "SH2_BRR", RW, 1 }, { 0x002, "SH2_SCR", RW, 1 },
    { 0x003, "SH2_TDR", RW, 1 }, { 0x004, "SH2_SSR", RW, 1 }, { 0x005, "SH2_RDR", RO, 1 },
    { 0x010, "SH2_TIER", RW, 1 }, { 0x011, "SH2_FTCSR", RW, 1 }, { 0x012, "SH2_FRCH", RW, 1 },
    { 0x013, "SH2_FRCL", RW, 1 }, { 0x014, "SH2_OCRH", RW, 1 }, { 0x015, "SH2_OCRL", RW, 1 },
    { 0x016, "SH2_TCR", RW, 1 }, { 0x017, "SH2_TOCR", RW, 1 }, { 0x018, "SH2_FICRH", RO, 1 },
    { 0x019, "SH2_FICRL", RO, 1 }, { 0x060, "SH2_IPRB", RW, 2 }, { 0x062, "SH2_VCRA", RW, 2 },
    { 0x064, "SH2_VCRB", RW, 2 }, { 0x066, "SH2_VCRC", RW, 2 }, { 0x068, "SH2_VCRD", RW, 2 },
    { 0x071, "SH2_DRCR0", RW, 1 }, { 0x072, "SH2_DRCR1", RW, 1 }, { 0x080, "SH2_WTCSR", RW, 1 },
    { 0x081, "SH2_WTCNT", RW, 1 }, { 0x083, "SH2_RSTCSR", RW, 1 }, { 0x091, "SH2_SBYCR", RW, 1 },
    { 0x092, "SH2_CCR", RW, 1 }, { 0x0E0, "SH2_ICR", RW, 2 }, { 0x0E2, "SH2_IPRA", RW, 2 },
    { 0x0E4, "SH2_VCRWDT", RW, 2 }, { 0x100, "SH2_DVSR", RW, 4 }, { 0x104, "SH2_DVDNT", RW, 4 },
    { 0x108, "SH2_DVCR", RW, 4 }, { 0x10C, "SH2_VCRDIV", RW, 4 }, { 0x110, "SH2_DVDNTH", RW, 4 },
    { 0x114, "SH2_DVDNTL", RW, 4 }, { 0x140, "SH2_BARAH", RW, 2 }, { 0x142, "SH2_BARAL", RW, 2 },
    { 0x144, "SH2_BAMRAH", RW, 2 }, { 0x146, "SH2_BAMRAL", RW, 2 }, { 0x148, "SH2_BBRA", RW, 2 },
    { 0x160, "SH2_BARBH", RW, 2 }, { 0x162, "SH2_BARBL", RW, 2 }, { 0x164, "SH2_BAMRBH", RW, 2 },
    { 0x166, "SH2_BAMRBL", RW, 2 }, { 0x168, "SH2_BBRB", RW, 2 }, { 0x170, "SH2_BDRBH", RW, 2 },
    { 0x172, "SH2_BDRBL", RW, 2 }, { 0x174, "SH2_BDMRBH", RW, 2 }, { 0x176, "SH2_BDMRBL", RW, 2 },
    { 0x178, "SH2_BRCR", RW, 2 }, { 0x180, "SH2_SAR0", RW, 4 }, { 0x184, "SH2_DAR0", RW, 4 },
    { 0x188, "SH2_TCR0", RW, 4 }, { 0x18C, "SH2_CHCR0", RW, 4 }, { 0x190, "SH2_SAR1", RW, 4 },
    { 0x194, "SH2_DAR1", RW, 4 }, { 0x198, "SH2_TCR1", RW, 4 }, { 0x19C, "SH2_CHCR1", RW, 4 },
    { 0x1A0, "SH2_VCRDMA0", RW, 4 }, { 0x1A8, "SH2_VCRDMA1", RW, 4 }, { 0x1B0, "SH2_DMAOR", RW, 4 },
    { 0x1E0, "SH2_BCR1", RW, 4 }, { 0x1E4, "SH2_BCR2", RW, 4 }, { 0x1E8, "SH2_WCR", RW, 4 },
    { 0x1EC, "SH2_MCR", RW, 4 }, { 0x1F0, "SH2_RTCSR", RW, 4 }, { 0x1F4, "SH2_RTCNT", RW, 4 },
    { 0x1F8, "SH2_RTCOR", RW, 4 },
};

const uint32_t kSmpcBase = 0x00100000u, kScspBase = 0x05B00000u, kVdp1Base = 0x05D00000u;
const uint32_t kVdp2Base = 0x05F80000u, kScuBase = 0x05FE0000u, kCdBase = 0x05890000u;
const uint32_t kCdFifoBase = 0x05818000u, kMinitBase = 0x01000000u, kSinitBase = 0x01800000u;
const uint32_t kOnChipBase = 0xFFFFFE00u;

#define SE_BLOCK(base, size, regs) { base, size, regs, sizeof(regs) / sizeof(regs[0]) }
const Block kBlocks[] = {
    SE_BLOCK(kSmpcBase, 1, kSmpc),      SE_BLOCK(kScspBase, 2, kScsp),
    SE_BLOCK(kVdp1Base, 2, kVdp1),      SE_BLOCK(kVdp2Base, 2, kVdp2),
    SE_BLOCK(kScuBase, 4, kScu),        SE_BLOCK(kCdBase, 2, kCdBlock),
    SE_BLOCK(kCdFifoBase, 4, kCdFifo),  SE_BLOCK(kMinitBase, 2, kMinit),
    SE_BLOCK(kSinitBase, 2, kSinit),    SE_BLOCK(kOnChipBase, 0, kOnChip),
};
#undef SE_BLOCK

std::vector<HardwareRegister> Build()
{
    std::vector<HardwareRegister> all;
    for (const Block& b : kBlocks)
        for (size_t i = 0; i < b.count; ++i)
        {
            const Reg& r = b.regs[i];
            all.push_back(HardwareRegister{ b.base + r.offset, r.size ? r.size : b.size, r.name, r.access });
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

// The bus areas that hold registers rather than memory, as 27-bit bus addresses
// (Docs/Saturn/MemoryLayout.txt). Whole areas, mirrors and unnamed registers included.
struct Area
{
    uint32_t first, last;
};
const Area kBusAreas[] = {
    { 0x00100000u, 0x0017FFFFu },   // SMPC
    { 0x01000000u, 0x01FFFFFFu },   // MINIT / SINIT
    { 0x05800000u, 0x058FFFFFu },   // A-bus CS2: the CD block
    { 0x05B00000u, 0x05BFFFFFu },   // SCSP registers
    { 0x05D00000u, 0x05D7FFFFu },   // VDP1 registers
    { 0x05F80000u, 0x05FBFFFFu },   // VDP2 registers
    { 0x05FE0000u, 0x05FFFFFFu },   // SCU registers, and the unknown ones above them
};

std::vector<HardwareRange> BuildRanges()
{
    std::vector<HardwareRange> out;
    // Bits 28-27 are not decoded, so each area repeats every 128 MiB; the 0xA partition is the
    // cache-through one again (SaturnMemory.txt).
    for (uint32_t partition : { 0x00000000u, 0x20000000u, 0xA0000000u })
        for (uint32_t repeat = 0; repeat < 4; ++repeat)
            for (const Area& a : kBusAreas)
            {
                const uint32_t base = partition | (repeat << 27);
                out.push_back(HardwareRange{ base | a.first, base | a.last });
            }
    // Not memory at all: associative purge and the cache address array (writes there change the
    // cache), and the on-chip partition.
    out.push_back(HardwareRange{ 0x40000000u, 0x7FFFFFFFu });
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
        // 111x ???? ???? ???? ?1?x ???r rrrr rrrr: bit 14 must be set, and the low 256 offsets
        // are registers only with bits 28 and 12 set too; elsewhere they read garbage.
        if (!(address & 0x4000u)) return false;
        const uint32_t offset = address & 0x1FFu;
        if (offset < 0x100u && !((address & 0x10000000u) && (address & 0x1000u))) return false;
        canonical = kOnChipBase + offset;
        return true;
    }
    if (partition != 0 && partition != 1 && partition != 5) return false;
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

const std::vector<HardwareRange>& HardwareRanges()
{
    static const std::vector<HardwareRange> ranges = BuildRanges();
    return ranges;
}

bool InHardwareRange(uint32_t address)
{
    const std::vector<HardwareRange>& r = HardwareRanges();
    auto it = std::upper_bound(r.begin(), r.end(), address,
                               [](uint32_t v, const HardwareRange& x) { return v < x.first; });
    return it != r.begin() && address <= std::prev(it)->last;
}

}  // namespace decomp
}  // namespace sfe
