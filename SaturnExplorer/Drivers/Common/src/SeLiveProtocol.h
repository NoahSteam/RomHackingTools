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
 *   server -> client : a 44-byte little-endian header, then the payloads in
 *                      order: VDP1 VRAM, VDP2 VRAM, CRAM, VDP2 struct, VDP1 regs,
 *                      low work RAM, high work RAM, VDP1 frame buffer, control block.
 *
 * Byte conventions match what the core (and the savestate driver) expect:
 *   - VDP1/VDP2 VRAM : Saturn-native big-endian (Yabause stores it that way).
 *   - CRAM           : Yabause host byte order (client normalizes to big-endian).
 *   - VDP2 struct    : the raw 288-byte Yabause `Vdp2` register struct (client
 *                      rebuilds the hardware-offset image via BuildVdp2RegImage).
 *   - VDP1 regs      : a ready hardware-offset, big-endian VDP1 register image
 *                      (0x18 bytes) the server assembles from its Vdp1 struct.
 *   - Work RAM       : raw bytes (1 MiB low @ 0x00200000, 1 MiB high @ 0x06000000).
 *   - Control block  : paused flag (u32 LE, 1 = paused) + frame counter (u64 LE),
 *                      so the client can label the pause button and show the
 *                      current frame without an extra round-trip.
 *
 * Any section length may be 0 (that data unavailable this build/version).
 */
#ifndef SATURNEXPLORER_SE_LIVE_PROTOCOL_H
#define SATURNEXPLORER_SE_LIVE_PROTOCOL_H

#define SE_LIVE_MAGIC0 'S'
#define SE_LIVE_MAGIC1 'E'
#define SE_LIVE_MAGIC2 'X'
#define SE_LIVE_MAGIC3 'P'
#define SE_LIVE_VERSION      4u
/* Command verbs are exactly 4 bytes; a request is a verb + 4-byte LE argument. */
#define SE_LIVE_REQUEST      "GET\n"   /* back-compat alias for the snapshot verb */
#define SE_LIVE_VERB_GET     "GET\n"
#define SE_LIVE_VERB_PAUSE   "PAU\n"
#define SE_LIVE_VERB_RESUME  "RUN\n"
#define SE_LIVE_VERB_STEP    "STP\n"
#define SE_LIVE_VERB_LEN     4
#define SE_LIVE_REQUEST_LEN  8    /* verb(4) + arg(4, little-endian) */
#define SE_LIVE_HEADER_LEN   44   /* magic(4) + version(4) + 9 section lengths(4 each) */

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
#define SE_LIVE_CONTROL_LEN     12u       /* paused(u32 LE) + frame(u64 LE) */

/* Default endpoints. The TCP port is used for the web bridge: the browser build
 * tunnels a normal TCP connect over a WebSocket proxy to this port (the client
 * writes the endpoint as "tcp:host:port"). */
#define SE_LIVE_DEFAULT_SOCK_PATH "/tmp/saturn_explorer.sock"
#define SE_LIVE_DEFAULT_PIPE_NAME "\\\\.\\pipe\\SaturnExplorer"
#define SE_LIVE_DEFAULT_TCP_PORT  6845

#endif /* SATURNEXPLORER_SE_LIVE_PROTOCOL_H */
