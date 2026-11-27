/* Saturn Explorer -- hardware event record, shared by the emulator glue, the export server, the
 * live driver and the front end so the 32-byte wire record cannot drift between them. Design:
 * Docs/DebuggerFeaturesPlan.md (D3).
 *
 * Pure C (C89 declarations, usable from C++14) and free of #includes: like SePerfCounters.h it
 * is included near the top of emulator translation units, ahead of the emulator's own types.h.
 * The wire limits (record/header length, per-reply cap) live in SeLiveProtocol.h with the other
 * section limits (SE_LIVE_PERF_*); this file holds only what the emulator side needs.
 *
 * New kinds are appended; a value is never reused. A reader renders an unknown kind generically
 * so an older client keeps working against a newer emulator.
 */
#ifndef SE_HW_EVENTS_H
#define SE_HW_EVENTS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Enable bits: the arg of the HWE verb, and (as a bit index 0..7) SeHwEvent.source. */
#define SE_HWEV_EN_CPU_EXC   0x01u  /* SH-2 interrupts and exceptions taken */
#define SE_HWEV_EN_SCU_INT   0x02u  /* SCU interrupt delivered, mask writes */
#define SE_HWEV_EN_SCU_DMA   0x04u  /* SCU DMA start, end, illegal */
#define SE_HWEV_EN_SH2_DMAC  0x08u  /* on-chip DMAC start, end */
#define SE_HWEV_EN_VDP1      0x10u  /* draw start/end, frame-buffer swap/erase */
#define SE_HWEV_EN_VDP2      0x20u  /* VBlank IN/OUT, display on/off */
#define SE_HWEV_EN_CD        0x40u  /* CD block command and response */
#define SE_HWEV_EN_SMPC      0x80u  /* SMPC command */

/* Kinds. Every record has cpu (0 master, 1 slave, SE_HWEV_CPU_NONE) and pc (valid with
 * SE_HWEV_F_HAS_PC). Blank cells are 0. Meaning of arg[0..3]:
 *
 *   kind                 cpu / pc          arg0              arg1            arg2          arg3
 *   1  SH2_EXCEPTION     cpu, saved PC     vector            SE_HWEV_EXC_*   level (INT)   handler PC
 *   2  SCU_INT_ACCEPT    none              source bit 0..31  level           vector        IPending after
 *   3  SCU_INT_MASK      none              new IMask         old IMask
 *   4  SCU_DMA_START     none              level | indirect<<8 | startFactor<<16 | readBus<<24 | writeBus<<28
 *                                                            src             dst           bytes
 *   5  SCU_DMA_END       none              level
 *   6  SCU_DMA_ILLEGAL   none              level | side<<8   src             dst
 *                                          (side 0: the SOURCE was refused, 1: the DESTINATION; arg1 and arg2
 *                                          are both always the transfer's addresses)
 *   7  DMAC_START        cpu, PC approx    ch | CHCR<<8      SAR             DAR           TCR
 *   8  DMAC_END          cpu               ch
 *   9  CD_COMMAND       none              CR1 (cmd = hi)    CR2             CR3           CR4
 *   10 CD_RESPONSE       none              Results[0] (status = hi byte), Results[1], Results[2], Results[3]
 *   11 SMPC_COMMAND      none              command           IREG0..3        IREG4..6
 *   12 VDP1_DRAW_START   none              draw FB           1 if the previous draw was interrupted
 *   13 VDP1_DRAW_END     none              last cmd addr     reason (0 END bit, 1 aborted by swap)
 *   14 VDP1_FB_SWAP      none              displayed FB      FBCR            TVMR
 *   15 VDP1_FB_ERASE     none              FB                EWDR            EWLR          EWRR
 *   16 VDP2_VBLANK       none              1 IN / 0 OUT
 *   17 VDP2_DISPLAY      none              1 on / 0 off      TVMD value
 *   255 SUPPRESSED       cpu whose events  count suppressed  per-frame cap
 *                        were dropped (or none)
 */
#define SE_HWEV_K_SH2_EXCEPTION   1u
#define SE_HWEV_K_SCU_INT_ACCEPT  2u
#define SE_HWEV_K_SCU_INT_MASK    3u
#define SE_HWEV_K_SCU_DMA_START   4u
#define SE_HWEV_K_SCU_DMA_END     5u
#define SE_HWEV_K_SCU_DMA_ILLEGAL 6u
#define SE_HWEV_K_DMAC_START      7u
#define SE_HWEV_K_DMAC_END        8u
#define SE_HWEV_K_CD_COMMAND      9u
#define SE_HWEV_K_CD_RESPONSE     10u
#define SE_HWEV_K_SMPC_COMMAND    11u
#define SE_HWEV_K_VDP1_DRAW_START 12u
#define SE_HWEV_K_VDP1_DRAW_END   13u
#define SE_HWEV_K_VDP1_FB_SWAP    14u
#define SE_HWEV_K_VDP1_FB_ERASE   15u
#define SE_HWEV_K_VDP2_VBLANK     16u
#define SE_HWEV_K_VDP2_DISPLAY    17u
#define SE_HWEV_K_SUPPRESSED      255u  /* "... +N more this frame" for one source */

/* SeHwEvent.cpu when the event belongs to no CPU. */
#define SE_HWEV_CPU_NONE 0xFFu

/* SeHwEvent.flags */
#define SE_HWEV_F_HAS_PC    1u  /* 'pc' is valid */
#define SE_HWEV_F_PC_APPROX 2u  /* 'pc' is where the CPU was when the event was noticed, not exact */

/* SH2_EXCEPTION arg1 */
#define SE_HWEV_EXC_INT      0u
#define SE_HWEV_EXC_NMI      1u
#define SE_HWEV_EXC_CPUADDR  2u
#define SE_HWEV_EXC_DMAADDR  3u
#define SE_HWEV_EXC_ILLINSTR 4u
#define SE_HWEV_EXC_ILLSLOT  5u
#define SE_HWEV_EXC_TRAP     6u

/* Emulator-side ring: 8192 records x 32 bytes = 256 KiB, overwrite-oldest. */
#define SE_HWEV_RING 8192u

/* Default per-source, per-CPU, per-frame record cap applied by the glue before the ring (compile-time in v1).
 * Master, slave and "no CPU" each have their own budget (a busy slave must not starve the master). Excess is
 * counted and surfaced as one SE_HWEV_K_SUPPRESSED record per (source, cpu) at frame end; its 'cpu' field says
 * whose events were dropped. */
#define SE_HWEV_FRAME_CAP 64u

/* Wire order, no padding. The wire is little-endian and the exporter serializes field by field,
 * so host padding could not leak onto it; the size check guards the in-memory layout the glue
 * fills and the sizing of the ring. */
typedef struct SeHwEvent
{
    unsigned char kind;     /* SE_HWEV_K_* */
    unsigned char source;   /* bit index (0..7) of the SE_HWEV_EN_* that produced it */
    unsigned char cpu;      /* 0 master, 1 slave, SE_HWEV_CPU_NONE */
    unsigned char flags;    /* SE_HWEV_F_* */
    unsigned int  frame;    /* stamped by the exporter, not the producer */
    unsigned int  ts;       /* master SH-2 clock, frame-relative */
    unsigned int  pc;       /* valid with SE_HWEV_F_HAS_PC */
    unsigned int  arg[4];
} SeHwEvent;

/* Compile-time size check that works in C89 and C++14 (a negative array size is the error). */
typedef char SeHwEvent_size_is_32[(sizeof(SeHwEvent) == 32) ? 1 : -1];

#ifdef __cplusplus
}
#endif

#endif /* SE_HW_EVENTS_H */
