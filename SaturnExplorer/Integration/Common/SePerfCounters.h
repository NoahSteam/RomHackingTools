/* Saturn Explorer -- profiler counter indices, shared by the emulator patch (the code apply.py
 * injects into Mednafen's ss core), the glue, the export server, the live driver and the front
 * end, so the wire order of the perf block (SeLiveProtocol.h, v26) cannot drift between them.
 *
 * Deliberately free of #includes and of anything but an enum and two macros: it is included at
 * the very top of Mednafen translation units, ahead of Mednafen's own types.h, which must come
 * before any standard integer header.
 *
 * Every counter is an unsigned 64-bit total for ONE emulated frame (one call of the emulator's
 * frame function). "Cycles" are SH-2 clock cycles -- the unit of Mednafen's sscpu_timestamp_t --
 * except SE_PERF_M68K_CYCLES, which is in 68000 clock cycles.
 *
 * New counters are appended; an index is never reused or reordered. A reader takes
 * min(count on the wire, SE_PERF_COUNT) and treats the rest as zero / unknown. */
#ifndef SATURNEXPLORER_SE_PERF_COUNTERS_H
#define SATURNEXPLORER_SE_PERF_COUNTERS_H

/* Bus devices an SH-2 external-bus access is attributed to. Grouped by the Saturn's three buses:
 * the CPU bus (what the SH-2s reach directly), the A-bus (cartridge + CD block, through the SCU)
 * and the B-bus (VDP1, VDP2, SCSP, through the SCU). */
enum
{
    SE_PERF_DEV_BIOS = 0,     /* CPU bus: 0x00000000 boot ROM                         */
    SE_PERF_DEV_SMPC,         /* CPU bus: 0x00100000 SMPC                             */
    SE_PERF_DEV_BACKUP,       /* CPU bus: 0x00180000 backup RAM                       */
    SE_PERF_DEV_WRAM_L,       /* CPU bus: 0x00200000 low work RAM                     */
    SE_PERF_DEV_CS0_OTHER,    /* CPU bus: the rest of CS0 (MINIT/SINIT, unmapped)     */
    SE_PERF_DEV_ABUS_CS0,     /* A-bus:   0x02000000 cartridge                        */
    SE_PERF_DEV_ABUS_CS1,     /* A-bus:   0x04000000 cartridge (ID / second select)   */
    SE_PERF_DEV_ABUS_CD,      /* A-bus:   0x05800000 CD block (CS2), and the dummy area below it */
    SE_PERF_DEV_SCSP,         /* B-bus:   0x05A00000 sound RAM + SCSP registers       */
    SE_PERF_DEV_VDP1,         /* B-bus:   0x05C00000 VDP1 VRAM, frame buffer, registers */
    SE_PERF_DEV_VDP2,         /* B-bus:   0x05E00000 VDP2 VRAM, CRAM, registers       */
    SE_PERF_DEV_SCU,          /* CPU bus: 0x05FE0000 SCU registers                    */
    SE_PERF_DEV_WRAM_H,       /* CPU bus: 0x06000000 high work RAM (SDRAM)            */
    SE_PERF_DEV_COUNT
};

/* The three buses an SCU DMA runs between (Mednafen's AddressToBus order). */
enum { SE_PERF_BUS_A = 0, SE_PERF_BUS_B = 1, SE_PERF_BUS_CPU = 2, SE_PERF_BUS_COUNT = 3 };

enum
{
    SE_PERF_FRAME_CYCLES = 0,       /* length of the frame                                       */
    /* Per SH-2, master then slave (index + cpu). Busy time is what is left of the frame:
     * frame - sleep - stall for the master; on-cycles - sleep - stall for the slave. */
    SE_PERF_SH2_SLEEP = 1,          /* [2] cycles in SLEEP (waiting for an interrupt) or standby  */
    SE_PERF_SH2_STALL = 3,          /* [2] cycles held off the bus (DMA burst, external halt)     */
    SE_PERF_SSH2_ON_CYCLES = 5,     /* cycles of the frame the slave SH-2 was switched on        */
    SE_PERF_VDP1_DRAW_CYCLES = 6,   /* cycles VDP1 spent drawing its command table               */
    SE_PERF_VDP1_COMMANDS = 7,      /* command-table entries VDP1 fetched                        */
    SE_PERF_VDP1_DRAW_STARTS = 8,   /* times drawing was started                                 */
    SE_PERF_VDP1_OVERRUNS = 9,      /* draws cut short: a frame-buffer swap or a new start arrived
                                     * before the command table was finished                     */
    SE_PERF_DSP_CYCLES = 10,        /* cycles the SCU DSP was executing                          */
    SE_PERF_M68K_CYCLES = 11,       /* 68000 clock cycles the sound CPU executed (not stopped)   */
    SE_PERF_M68K_TOTAL_CYCLES = 12, /* 68000 clock cycles in the frame                           */
    SE_PERF_DMA_TRANSFERS = 13,     /* [3] SCU DMA transfers started, per level 0..2             */
    SE_PERF_DMA_LEVEL_BYTES = 16,   /* [3] SCU DMA bytes requested, per level 0..2               */
    SE_PERF_DMA_ROUTE_BYTES = 19,   /* [9] SCU DMA bytes by route: source bus * 3 + destination  */
    SE_PERF_BUS_ACCESS = 28,        /* [SE_PERF_DEV_COUNT * 2] SH-2 external-bus accesses:
                                     * device * 2 + (1 for a write). A cache hit is not a bus
                                     * access, and neither is an instruction fetch the emulator
                                     * serves from its fast map.                                  */
    SE_PERF_COUNT = SE_PERF_BUS_ACCESS + SE_PERF_DEV_COUNT * 2      /* 54 */
};

/* Function attribution (the opt-in half; see SE_LIVE_PROF_FUNCTIONS). One entry is the SH-2
 * cycles one CPU spent, this frame, inside one function -- self time, callees excluded. The key
 * packs what the entry is about into 32 bits:
 *   bits 0..26  the address, masked to the 27 bus bits (the SH-2's cache-region bits are dropped,
 *               so a function reached through 0x06... and 0x26... is one entry)
 *   bit 30      set = slave SH-2
 *   bit 31      set = the address is a 16-byte bucket of raw PCs, used while the emulator has no
 *               recorded call frame to name the function (code that was already running when
 *               profiling was switched on, such as the main loop); clear = a function entry the
 *               shadow call stack recorded from a real call. */
#define SE_PERF_FN_ADDR_MASK 0x07FFFFFFu
#define SE_PERF_FN_SLAVE     0x40000000u
#define SE_PERF_FN_RAW_PC    0x80000000u
#define SE_PERF_FN_PC_BUCKET 16u

#endif /* SATURNEXPLORER_SE_PERF_COUNTERS_H */
