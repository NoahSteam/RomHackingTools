/* Saturn Explorer — live-tap wire protocol, shared by the LiveDriver (client)
 * and the Yabause se_export module (server) so they can't drift.
 *
 * Transport: a local stream socket (Unix domain socket on POSIX, named pipe on
 * Windows). Request/response, one snapshot per request:
 *
 *   client -> server : an 8-byte command frame: a 4-byte verb + a 4-byte
 *                      little-endian argument. Verbs:
 *                        "GET\n" (arg 0)  request a snapshot
 *                        "PAU\n" (arg 0)  pause after the current frame
 *                        "RUN\n" (arg 0)  resume free-running
 *                        "STP\n" (arg n)  run n frames, then pause
 *                      Every verb replies with a full snapshot reflecting the
 *                      new run state, so the client stays in sync with one code
 *                      path.
 *   server -> client : a little-endian header (magic + version + N section
 *                      lengths), then the payloads in order: VDP1 VRAM, VDP2 VRAM,
 *                      CRAM, VDP2 struct, VDP1 regs, low work RAM, high work RAM,
 *                      VDP1 frame buffer, control block, and (v5+) SH-2 state.
 *
 * Byte conventions match what the core (and the savestate driver) expect:
 *   - VDP1/VDP2 VRAM : Saturn-native big-endian (Yabause stores it that way).
 *   - CRAM           : Yabause host byte order (client normalizes to big-endian).
 *   - VDP2 struct    : the raw 288-byte Yabause `Vdp2` register struct (client
 *                      rebuilds the hardware-offset image via BuildVdp2RegImage).
 *   - VDP1 regs      : a ready hardware-offset, big-endian VDP1 register image
 *                      (0x18 bytes) the server assembles from its Vdp1 struct.
 *   - Work RAM       : Yabause host order — 16-bit words in host (little-endian)
 *                      byte order (T2 access); the client byte-swaps each word to
 *                      Saturn-native big-endian, exactly like the savestate path.
 *                      1 MiB low @ 0x00200000, 1 MiB high @ 0x06000000.
 *   - Control block  : paused flag (u32 LE, 1 = paused) + frame counter (u64 LE),
 *                      then (v5+) stop reason (u32 LE, 0 = user/none, 1 = execution
 *                      breakpoint), stop CPU (u32 LE, 0 master / 1 slave), and stop
 *                      PC (u32 LE), so the client can jump to a breakpoint hit
 *                      without an extra round-trip.
 *   - SH-2 state     : (v5+) two Yabause sh2regs_struct blocks, master then slave,
 *                      each 23 host-order (LE) u32 in struct order R[0..15], SR,
 *                      GBR, VBR, MACH, MACL, PR, PC. The client maps these to
 *                      se_sh2_regs for the disassembler / Assembly panel.
 *
 * Breakpoints (v5+): the client syncs its whole execution-breakpoint set whenever
 * it changes, via a "BKP\n" command whose arg is the descriptor count, followed by
 * that many 12-byte descriptors: address(u32 LE) + size(u32 LE) + flags(u32 LE,
 * bits 0-1 kind {0 exec,1 read,2 write,3 rw}, bit 2 cpu {0 master,1 slave}, bit 3
 * enabled). The server replaces its set and installs them in the emulator; a hit
 * pauses the emulator and is reported through the control block's stop fields.
 *
 * Any section length may be 0 (that data unavailable this build/version).
 */
#ifndef SATURNEXPLORER_SE_LIVE_PROTOCOL_H
#define SATURNEXPLORER_SE_LIVE_PROTOCOL_H

#define SE_LIVE_MAGIC0 'S'
#define SE_LIVE_MAGIC1 'E'
#define SE_LIVE_MAGIC2 'X'
#define SE_LIVE_MAGIC3 'P'
#define SE_LIVE_VERSION      6u
/* Command verbs are exactly 4 bytes; a request is a verb + 4-byte LE argument. */
#define SE_LIVE_REQUEST      "GET\n"   /* back-compat alias for the snapshot verb */
#define SE_LIVE_VERB_GET     "GET\n"
#define SE_LIVE_VERB_PAUSE   "PAU\n"
#define SE_LIVE_VERB_RESUME  "RUN\n"
#define SE_LIVE_VERB_STEP    "STP\n"
#define SE_LIVE_VERB_BKPTS   "BKP\n"   /* sync breakpoint set: arg = descriptor count */
#define SE_LIVE_VERB_WRITE   "WRM\n"   /* poke work RAM: arg = byte count N, payload
                                        * = address(u32 LE) + N big-endian bytes */
#define SE_LIVE_VERB_LEN     4
#define SE_LIVE_REQUEST_LEN  8    /* verb(4) + arg(4, little-endian) */
#define SE_LIVE_BKPT_DESC_LEN 12  /* address(4) + size(4) + flags(4), all LE */
#define SE_LIVE_HEADER_LEN   48   /* magic(4) + version(4) + 10 section lengths(4 each) */

/* Breakpoint descriptor flag bits (v5+). */
#define SE_LIVE_BP_KIND_MASK  0x3u   /* 0 exec, 1 read, 2 write, 3 read/write */
#define SE_LIVE_BP_CPU_SLAVE  0x4u   /* set = slave SH-2, clear = master */
#define SE_LIVE_BP_ENABLED    0x8u

/* Control-block stop reasons (v5+). */
#define SE_LIVE_STOP_NONE     0u     /* not stopped, or paused by the user */
#define SE_LIVE_STOP_EXEC_BP  1u     /* halted on an execution breakpoint */

/* Canonical section sizes (bytes). The header still carries the actual lengths,
 * so a client validates rather than assumes; these are the expected values. */
#define SE_LIVE_VDP1_VRAM_LEN   0x80000u
#define SE_LIVE_VDP2_VRAM_LEN   0x80000u
#define SE_LIVE_CRAM_LEN        0x1000u
#define SE_LIVE_VDP2_STRUCT_LEN 288u
#define SE_LIVE_VDP1_REGS_LEN   0x18u
#define SE_LIVE_WRAM_LOW_LEN    0x100000u
#define SE_LIVE_WRAM_HIGH_LEN   0x100000u
#define SE_LIVE_VDP1_FB_LEN     0x40000u   /* VDP1 frame buffer (drawn output) */
#define SE_LIVE_CONTROL_LEN     24u       /* paused(u32) + frame(u64) + stop{reason,cpu,pc}(u32 each) */
#define SE_LIVE_SH2_REGS_LEN    92u        /* one CPU: 23 u32 (R[16],SR,GBR,VBR,MACH,MACL,PR,PC) */
#define SE_LIVE_SH2_LEN         (2u * SE_LIVE_SH2_REGS_LEN)   /* master + slave */

/* Default endpoints. The TCP port is used for the web bridge: the browser build
 * tunnels a normal TCP connect over a WebSocket proxy to this port (the client
 * writes the endpoint as "tcp:host:port"). */
#define SE_LIVE_DEFAULT_SOCK_PATH "/tmp/saturn_explorer.sock"
#define SE_LIVE_DEFAULT_PIPE_NAME "\\\\.\\pipe\\SaturnExplorer"
#define SE_LIVE_DEFAULT_TCP_PORT  6845
/* The browser build has no local socket, so it defaults to this TCP endpoint, which
 * the WebSocket->TCP bridge forwards to the emulator's export port (see the Yabause
 * README "Web (browser) live viewing"). */
#define SE_LIVE_DEFAULT_TCP_ENDPOINT "tcp:127.0.0.1:6845"

#endif /* SATURNEXPLORER_SE_LIVE_PROTOCOL_H */
