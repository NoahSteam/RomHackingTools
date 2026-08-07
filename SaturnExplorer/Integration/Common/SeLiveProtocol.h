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
#define SE_LIVE_VERSION      16u   /* +v16 savestate rewind: per-frame savestate delta stream
                                    * (trailing section) + LST load-state verb */
/* Command verbs are exactly 4 bytes; a request is a verb + 4-byte LE argument. */
#define SE_LIVE_REQUEST      "GET\n"   /* back-compat alias for the snapshot verb */
/* Snapshot request. arg = the client's last-seen frame number: the server keeps an N-deep
 * ring of completed frames and replies with the OLDEST frame newer than 'arg' (gap-free, so
 * a client that keeps up never skips a frame), or the latest when arg is 0 or the client has
 * caught up. The reply's control block reports the frame number actually served. arg 0 (an
 * older client that doesn't track frames) always gets the latest — the original behavior. */
#define SE_LIVE_VERB_GET     "GET\n"
#define SE_LIVE_VERB_PAUSE   "PAU\n"
#define SE_LIVE_VERB_RESUME  "RUN\n"
#define SE_LIVE_VERB_STEP    "STP\n"
#define SE_LIVE_VERB_ISTEP   "IST\n"   /* single-step SH-2 instructions (v12+): arg = count
                                        * (>=1). Runs the halted CPU that many instructions
                                        * then halts with SE_LIVE_STOP_STEP. Step-over/out
                                        * are built client-side from this + a temp bp. */
#define SE_LIVE_VERB_BKPTS   "BKP\n"   /* sync breakpoint set: arg = descriptor count */
#define SE_LIVE_VERB_WRITE   "WRM\n"   /* poke work RAM: arg = byte count N, payload
                                        * = address(u32 LE) + N big-endian bytes */
#define SE_LIVE_VERB_WRITESND "WRS\n"  /* poke sound RAM (v13+): arg = byte count N,
                                        * payload = offset(u32 LE, 0-based in the 512K
                                        * SCSP RAM) + N raw bytes */
#define SE_LIVE_VERB_INPUT   "INP\n"   /* inject controller state (v7+): arg = port(high
                                        * 16 bits) | button bitmask(low 16, SE_PAD_*). The
                                        * emulator glue drives the pad directly, bypassing
                                        * its own host-input mapping. No payload. */
#define SE_LIVE_VERB_TRACE   "TRC\n"   /* install tracepoints (v8+): arg = descriptor
                                        * count N, payload = N SE_LIVE_TRACE_DESC_LEN
                                        * descriptors {id,cpu,address,flags} (all u32 LE).
                                        * The emulator traps those PCs and, on a hit,
                                        * appends an event to the reply's events block. */
#define SE_LIVE_VERB_LOADSTATE "LST\n" /* rewind (v16+): atomically restore a full savestate,
                                        * apply memory edits on top, and resume from a past frame.
                                        * arg = payload byte count N; payload =
                                        *   frame_no(u32 LE) + edits_len(u32 LE) +
                                        *   edits[edits_len] + state[N-8-edits_len].
                                        * 'edits' is the SE_LIVE_EDIT_* blob below (may be empty);
                                        * 'state' is the opaque emulator image the client rebuilt
                                        * from its keyframe+delta ring. The emulator restores +
                                        * patches + adopts frame_no + resumes, all on the emulate
                                        * thread at the frame gate (so pokes can't race the load). */
#define SE_LIVE_VERB_LEN     4
#define SE_LIVE_REQUEST_LEN  8    /* verb(4) + arg(4, little-endian) */

/* Tracepoint descriptor (v8+): id(4) + cpu(4) + address(4) + flags(4), all LE.
 * flags bit0 = enabled (matches SE_LIVE_TP_ENABLED). */
#define SE_LIVE_TRACE_DESC_LEN 16
#define SE_LIVE_TP_ENABLED     0x1u

/* Tracepoint events block (v8+). Appended AFTER the 10 snapshot sections + control +
 * SH-2 sections, as a version-gated trailing block so the 48-byte header and the
 * existing sections are unchanged (a v8 client reads the block only when the server
 * reports version >= 8). Layout: u32 eventCount (LE), then eventCount events. Each
 * event is SE_LIVE_EVENT_LEN bytes: id(4) + cpu(4) + frame(4) + 23 captured SH-2
 * registers (u32 LE, in se_sh2_regs order: r[0..15], pc, pr, sr, gbr, vbr, mach, macl).
 * The client formats the message from these captured registers, so no string handling
 * happens emulator-side. */
#define SE_LIVE_EVENT_REGS     23
#define SE_LIVE_EVENT_LEN      (12u + SE_LIVE_EVENT_REGS * 4u)   /* 104 */
#define SE_LIVE_EVENTS_MAX     64u   /* cap per reply; excess dropped with a marker */

/* Call-stack block (v9+). Appended AFTER the v8 events block, version-gated the same
 * way (a client reads it only when the server reports version >= 9). It carries the
 * emulator's recorded per-CPU shadow call stack — every frame here is ● Confirmed
 * (recorded from a real bsr/jsr...rts), unlike the client's heuristic reconstruction.
 * Layout: for CPU 0 (master) then CPU 1 (slave): u32 frameCount (LE, capped at
 * SE_LIVE_CALLSTACK_MAX), then frameCount frames, innermost (current) first. Each frame
 * is SE_LIVE_CALLFRAME_LEN bytes: callSite(4) + functionAddress(4) + returnAddress(4) +
 * stackPointer(4) + cycle(8, LE lo then hi) + frameNumber(4), all LE. */
#define SE_LIVE_CALLFRAME_LEN  28u
#define SE_LIVE_CALLSTACK_MAX  64u

/* Keyboard-map block (v10+). Appended AFTER the v9 call-stack block, version-gated the
 * same way (a client reads it only when the server reports version >= 10). It reports the
 * emulator's OWN live host keyboard bindings so a front end can mirror the user's keys
 * (e.g. WASD) automatically — no config-file export/upload. For port 0 then port 1:
 * SE_LIVE_KEYMAP_BUTTONS int32 (LE) values, one per Saturn pad button in ascending
 * SE_PAD_* order (UP, DOWN, LEFT, RIGHT, A, B, C, X, Y, Z, L, R, START). Each value is the
 * USB-HID keyboard scancode currently bound to that button, or a negative value when the
 * button has no keyboard binding (unbound, or mapped to a joystick/mouse). USB-HID
 * scancodes are exactly what SDL/ImGui use, so the client maps them straight across. */
#define SE_LIVE_KEYMAP_BUTTONS 13
#define SE_LIVE_KEYMAP_PORTS   2
#define SE_LIVE_KEYMAP_LEN     (SE_LIVE_KEYMAP_PORTS * SE_LIVE_KEYMAP_BUTTONS * 4u)

/* Saturn digital-pad buttons (v7+), an emulator-agnostic logical bitmask carried by
 * INP. The per-emulator glue maps these to that emulator's own pad bit order, so the
 * wire stays independent of any one emulator's input convention. Port is packed into
 * the high 16 bits of the INP arg: arg = (port << 16) | (buttons & SE_PAD_ALL). */
#define SE_PAD_UP     0x0001u
#define SE_PAD_DOWN   0x0002u
#define SE_PAD_LEFT   0x0004u
#define SE_PAD_RIGHT  0x0008u
#define SE_PAD_A      0x0010u
#define SE_PAD_B      0x0020u
#define SE_PAD_C      0x0040u
#define SE_PAD_X      0x0080u
#define SE_PAD_Y      0x0100u
#define SE_PAD_Z      0x0200u
#define SE_PAD_L      0x0400u
#define SE_PAD_R      0x0800u
#define SE_PAD_START  0x1000u
#define SE_PAD_ALL    0x1FFFu
#define SE_LIVE_INPUT_PORT(arg)     (((arg) >> 16) & 0xFFFFu)
#define SE_LIVE_INPUT_BUTTONS(arg)  ((arg) & 0xFFFFu)
#define SE_LIVE_BKPT_DESC_LEN 12  /* address(4) + size(4) + flags(4), all LE */
#define SE_LIVE_HEADER_LEN   48   /* magic(4) + version(4) + 10 section lengths(4 each) */

/* Debug log lines (v11+): a trailing block after the keymap. u32 count (LE, capped at
 * SE_LIVE_LOG_MAX), then that many fixed-length NUL-padded text records. The emulator
 * glue appends lines via SeExportLog(); the client drains them into its Log window. Lets
 * the emulator surface diagnostics (e.g. the controller input it actually receives) in
 * Saturn Explorer with no separate console. */
#define SE_LIVE_LOG_MAX       32u   /* max log lines carried per snapshot reply */
#define SE_LIVE_LOG_LINE_LEN  96u   /* bytes per record (NUL-terminated/padded) */

/* Sound-RAM block (v13+): a trailing block after the v11 log block, version-gated the same
 * way (a client reads it only when the server reports version >= 13). Carries the Saturn's
 * 512 KiB SCSP sound RAM — the 68000 sound-driver program + PCM tone bank + sequence data.
 * Layout: u32 length (LE); when length == SE_LIVE_SOUND_RAM_LEN it is followed by that many
 * raw bytes, and length 0 means this build/emulator didn't supply it (no bytes follow).
 * SCSP RAM is byte-addressed, so the bytes need no per-word swap. Kept as a trailing block
 * (not an 11th header section) so the 48-byte header and all existing sections are
 * unchanged. Poke it back with the WRS verb. */
#define SE_LIVE_SOUND_RAM_LEN 0x80000u   /* 512 KiB */

/* SCSP slot block (v14+): a trailing block after the v13 sound-RAM block, version-gated the
 * same way (read only when the server reports version >= 14). Carries the 32 SCSP voices as
 * DECODED state — the emulator glue fills each record from its own decoded slot struct,
 * because the "currently sounding" fields (eg_phase/eg_level/cur_addr) are not in the raw
 * SCSP registers. Layout: u32 length (LE); when length == SE_LIVE_SCSP_BLOCK_LEN it is
 * followed by SE_LIVE_SCSP_SLOTS records, else length 0 (no records). Each record is
 * SE_LIVE_SCSP_SLOT_LEN bytes, packed little-endian in this fixed order (matching the client
 * parser and the se_scsp_slot ABI struct):
 *   +0  key_on(u8) +1 active(u8) +2 eg_phase(u8) +3 format(u8) +4 loop_mode(u8) +5 octave(s8)
 *   +6  total_level(u8) +7 direct_level(u8) +8 direct_pan(u8) +9 effect_level(u8)
 *   +10 effect_pan(u8) +11 ar(u8) +12 d1r(u8) +13 d2r(u8) +14 rr(u8) +15 dl(u8)
 *   +16 eg_level(u16) +18 freq_num(u16)
 *   +20 start_addr(u32) +24 loop_start(u32) +28 loop_end(u32) +32 cur_addr(u32)  = 36 bytes */
#define SE_LIVE_SCSP_SLOTS     32u
#define SE_LIVE_SCSP_SLOT_LEN  36u
#define SE_LIVE_SCSP_BLOCK_LEN (SE_LIVE_SCSP_SLOTS * SE_LIVE_SCSP_SLOT_LEN)   /* 1152 */

/* CD-status block (v15+): a trailing block after the v14 SCSP slot block, version-gated the
 * same way (read only when the server reports version >= 15). Carries the live CD-block state
 * so the Disc Explorer can show the sector the drive is reading right now. Layout: u32 length
 * (LE); when length == SE_LIVE_CD_BLOCK_LEN it is followed by the fixed record below, else
 * length 0 (this build/emulator has no CD tap). Record, little-endian, matching se_cd_status:
 *   +0 current_fad(u32) +4 play_start_fad(u32) +8 play_end_fad(u32) +12 status(u8) +13 pad(3) */
#define SE_LIVE_CD_BLOCK_LEN   16u

/* Savestate-rewind section (v16+): a trailing section after the v15 CD block, version-gated
 * the same way (read only when the server reports version >= 16). Unlike the fixed blocks, it
 * carries a LAGGING stream of 0..N per-frame savestate blocks (the emulator diffs each frame on
 * a worker thread, so a block usually arrives a few frames behind its snapshot). Section layout:
 *   u32 count; then 'count' blocks, each:
 *     u8  kind            (SE_LIVE_STATE_KIND_*: 0 = delta, 1 = keyframe)
 *     u8  pad[3]
 *     u32 frame_no        the frame this block reconstructs
 *     u32 base_keyframe   frame_no of the keyframe this delta is against (== frame_no if kind==keyframe)
 *     u32 payload_len     bytes of payload that follow
 *     u32 full_len        decoded full-savestate size (so the client can size its buffer)
 *     u8  payload[payload_len]   RLE(full state) for a keyframe; RLE(XOR(full, keyframe_full)) for a delta
 * count 0 = the worker had nothing ready this response, or the feature is off (no save hook).
 * The RLE + XOR codec is opaque byte-crunching (SeStateCodec.h); the client never interprets
 * the savestate contents, only reconstructs the full image to hand back via the LST verb. */
#define SE_LIVE_STATE_HDR_LEN     20u   /* kind(1)+pad(3)+frame(4)+base(4)+payload_len(4)+full_len(4) */
#define SE_LIVE_STATE_KIND_DELTA   0u
#define SE_LIVE_STATE_KIND_KEYFRAME 1u
#define SE_LIVE_STATE_MAX_PER_REPLY 4u  /* cap on blocks drained into one response */
#define SE_LIVE_STATE_MAX_PAYLOAD (64u * 1024u * 1024u) /* sanity bound on one block's payload */

/* Memory-edit blob (v16), carried inside an LST payload and applied by the emulator right
 * after the savestate restore. Layout:
 *   u32 count; then 'count' edits, each:
 *     u8  type            (SE_LIVE_EDIT_*: 0 = work RAM @ Saturn bus address, 1 = sound RAM offset)
 *     u8  pad[3]
 *     u32 addr            work-RAM Saturn bus address, or 0-based sound-RAM offset
 *     u32 len             number of bytes
 *     u8  bytes[len]      the new bytes (work RAM: written big-endian byte-by-byte as usual)
 * These are the edits the user made while scrubbed; applying them post-restore is what makes
 * the game re-simulate from frame N *with the modifications*. */
#define SE_LIVE_EDIT_HDR_LEN   12u   /* type(1)+pad(3)+addr(4)+len(4) per edit */
#define SE_LIVE_EDIT_TYPE_WRAM  0u
#define SE_LIVE_EDIT_TYPE_SOUND 1u

/* Breakpoint descriptor flag bits (v5+). */
#define SE_LIVE_BP_KIND_MASK  0x3u   /* 0 exec, 1 read, 2 write, 3 read/write */
#define SE_LIVE_BP_CPU_SLAVE  0x4u   /* set = slave SH-2, clear = master */
#define SE_LIVE_BP_ENABLED    0x8u

/* Control-block stop reasons (v5+). */
#define SE_LIVE_STOP_NONE     0u     /* not stopped, or paused by the user */
#define SE_LIVE_STOP_EXEC_BP  1u     /* halted on an execution breakpoint */
#define SE_LIVE_STOP_STEP     2u     /* halted after an instruction step (IST) completed */

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
