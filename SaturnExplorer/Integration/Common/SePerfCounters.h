/* Saturn Explorer -- profiler counter indices, shared by the emulator patch (the code apply.py
 * injects into Mednafen's ss core), the glue, the export server, the live driver and the front
 * end, so the wire order of the perf block (SeLiveProtocol.h, v27) cannot drift between them.
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
 * min(count on the wire, SE_PERF_COUNT) and treats the rest as zero / unknown.
 *
 * ---- v26 -> v27 mapping (frozen; every later change to the counters appends to the v27 list) ----
 *
 *   index      v26 meaning                      v27 meaning
 *   0          frame cycles                     unchanged
 *   1,2        SH-2 sleep, master/slave         unchanged
 *   3,4        SH-2 stall, master/slave         unchanged
 *   5          slave on-cycles                  SAME INDEX, SAME MEANING. Only its producer is
 *                                               corrected (F2: integrated over real on/off edges
 *                                               instead of all-or-nothing per frame). A v26
 *                                               server's value is the all-or-nothing one.
 *   6          elapsed VDP1 drawing cycles      RETIRED. A v27 producer writes 0 here and a v27
 *                                               reader ignores it. It is never redefined or reused:
 *                                               the new VDP1 figure is index 55. A v26 server's
 *                                               value is still the elapsed figure and is shown as
 *                                               such, under its old label (SE_PERF_V26_VDP1_ELAPSED).
 *   7..13      commands, draw starts, overruns, DSP, 68000 executed, 68000 total, DMA starts
 *                                               unchanged (13 is the base of a [3] array)
 *   16..53     DMA bytes per level, per route, bus accesses per device
 *                                               unchanged; the device list does not grow (see 58)
 *   54         -                                SE_PERF_SSH2_ON_INTEGRATED (F2)
 *   55         -                                SE_PERF_VDP1_WORK_CYCLES (F4)
 *   56         -                                SE_PERF_VDP1_DMA_DISCARDED (F4)
 *   57         -                                SE_PERF_M68K_IDLE_CYCLES (F5)
 *   58..61     -                                SE_PERF_BUS_ACCESS_X [2 * 2] (F8)
 *   count      54                               62
 *
 * Which of the v27-only entries a producer actually fills is stated per record, not guessed from a
 * value: the record's 'valid' word (SE_PERF_VALID_*, below). An entry whose bit is clear is "not
 * available", never a measured zero; a reader that sees a v26 record treats every bit as clear. */
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
    SE_PERF_RETIRED_6 = 6,          /* v27: RETIRED, always 0, never reused. In a v26 record this
                                     * is SE_PERF_V26_VDP1_ELAPSED: cycles VDP1 spent drawing its
                                     * command table, charged whenever a draw was active          */
    SE_PERF_V26_VDP1_ELAPSED = 6,   /* alias of 6, for readers that present a v26 server's value  */
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
    SE_PERF_V26_COUNT = SE_PERF_BUS_ACCESS + SE_PERF_DEV_COUNT * 2, /* 54: what a v26 server sends */

    /* ---- appended in v27 ---- */
    SE_PERF_SSH2_ON_INTEGRATED = 54,  /* F2: slave SH-2 on-cycles integrated over the real on/off
                                       * edges inside the frame. Written with the same value as
                                       * index 5; index 5 stays so a v26 comparison still works.   */
    SE_PERF_VDP1_WORK_CYCLES = 55,    /* F4: VDP1 command-work cycles, counted where the command
                                       * budget is debited (not elapsed time). May exceed the
                                       * frame in instant-draw mode.                               */
    SE_PERF_VDP1_DMA_DISCARDED = 56,  /* F4: positive active-draw budget thrown away when a DMA
                                       * held VDP1. A budget quantity, not elapsed hold time.      */
    SE_PERF_M68K_IDLE_CYCLES = 57,    /* F5: 68000 clock cycles stopped/halted in the frame.
                                       * Executed (index 11) = total (12) - idle (57) once F5 lands */
    SE_PERF_BUS_ACCESS_X = 58,        /* F8: [SE_PERF_XDEV_COUNT * 2] accesses to the buckets that
                                       * do not exist in the device list above, xdev * 2 + (1 write).
                                       * The v26 device list cannot grow without shifting index 28's
                                       * array, so they live here.                                 */
    SE_PERF_COUNT = SE_PERF_BUS_ACCESS_X + 2 * 2                    /* 62 */
};

/* The extra bus buckets of SE_PERF_BUS_ACCESS_X (F8). A v27 producer no longer folds these into
 * SE_PERF_DEV_ABUS_CD or the VDP2 / SCU windows. */
enum { SE_PERF_XDEV_ABUS_DUMMY = 0,   /* 0x05000000.. A-bus dummy space (not the CD block) */
       SE_PERF_XDEV_UNMAPPED = 1,     /* holes above the VDP2 / SCU windows, other unmapped */
       SE_PERF_XDEV_COUNT = 2 };

/* Per-record 'valid' word of a v27 record: which v27-only fields the producer really filled. A
 * clear bit means "not available" (the field is on the wire as zero but is not a measurement).
 * Bits only ever get added. A producer that has not implemented an item sends its bit clear. */
#define SE_PERF_VALID_IDENTITY   0x01u  /* timelineId and frameNo (F3). sampleSeq is always valid  */
#define SE_PERF_VALID_MODE       0x02u  /* 'mode' is the attribution mode latched for the frame (F6);
                                         * clear: it is only what was in force at publication      */
#define SE_PERF_VALID_OTHER      0x04u  /* the per-CPU Other split and satFlags (F7)               */
#define SE_PERF_VALID_CACHE      0x08u  /* cacheMode (F8)                                          */
#define SE_PERF_VALID_SSH2_INT   0x10u  /* counter 54 (F2)                                         */
#define SE_PERF_VALID_VDP1_WORK  0x20u  /* counters 55 and 56 (F4)                                 */
#define SE_PERF_VALID_M68K_IDLE  0x40u  /* counter 57 (F5)                                         */
#define SE_PERF_VALID_BUS_X      0x80u  /* counters 58..61 (F8)                                    */
#define SE_PERF_VALID_ALL        0xFFu

/* cacheMode: the SH-2 instruction-cache emulation the emulator was configured with (F8). Bus
 * request counts depend on it, so it travels with the sample. */
#define SE_PERF_CACHE_UNKNOWN 0u
#define SE_PERF_CACHE_OFF     1u   /* cache not emulated (fast-map fetches, no fills) */
#define SE_PERF_CACHE_ON      2u   /* cache emulated (fills, hits) */

/* satFlags (F7): table limits reached during the frame. The per-CPU cycle totals are in the
 * record's otherNotAdmitted / otherEvicted words. */
#define SE_PERF_SAT_TABLE_FULL 0x1u   /* the per-frame table refused a new key (3072 admitted)       */
#define SE_PERF_SAT_TRUNCATED  0x2u   /* more than 64 keys existed; the lowest were evicted to Other */

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
