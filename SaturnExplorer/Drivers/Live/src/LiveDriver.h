/* Saturn Explorer — Live driver (Seam A). Connects to a running, patched Yabause
 * over a local socket (see SeLiveProtocol.h) and serves its VDP state to the core
 * as an se_data_source, refreshed on a background thread. Native only (threads +
 * sockets) — not part of the WASM/web build.
 */
#ifndef SATURNEXPLORER_LIVE_DRIVER_H
#define SATURNEXPLORER_LIVE_DRIVER_H

#include "saturnexplorer/SeDataSource.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Open a live connection. 'endpoint' is the local socket path (POSIX) or named
 * pipe (Windows); pass NULL for the platform default (SE_LIVE_DEFAULT_*). On
 * success returns SE_OK and fills '*out' (whose 'close' stops the poll thread on
 * se_destroy). Returns SE_ERR_IO if the emulator isn't reachable. */
se_result se_live_open(const char* endpoint, se_data_source* out);

/* The live protocol version last reported by the connected server, or 0 if not
 * connected / not a live source. Compare against SE_LIVE_VERSION to detect a
 * client/emulator version mismatch. 'ds' must be an se_data_source filled by
 * se_live_open. */
uint32_t se_live_server_version(const se_data_source* ds);

/* Push the whole execution/memory breakpoint set to the emulator (v5+). 'descs'
 * points at 'count' 12-byte descriptors (address u32 LE + size u32 LE + flags u32
 * LE; see SeLiveProtocol.h SE_LIVE_BP_*). The poll thread ships them on its next
 * cycle; the server replaces its set and installs them. No-op if 'ds' isn't a live
 * source. Call whenever the local breakpoint set changes. */
void se_live_set_breakpoints(const se_data_source* ds, const uint8_t* descs,
                             uint32_t count);

/* Single-step the halted SH-2 'count' instructions then halt again (v12+). The server
 * steps whichever CPU the last stop latched; on completion the control block reports
 * SE_LIVE_STOP_STEP with the new PC. Used to build Step Into (count = 1). No-op if 'ds'
 * isn't a live source. */
void se_live_step_insn(const se_data_source* ds, uint32_t count);

/* Inject controller state into the running emulator (v7+): 'port' (0 = controller 1)
 * and 'buttons' (an SE_PAD_* bitmask). The poll thread drives the emulated pad
 * directly, bypassing the emulator's host-input mapping. Send it whenever the held
 * buttons change (0 releases all). No-op if 'ds' isn't a live source. */
void se_live_send_input(const se_data_source* ds, uint32_t port, uint32_t buttons);

/* Push the tracepoint set to the emulator (v8+). 'descs' points at 'count' 16-byte
 * descriptors {id,cpu,address,flags} (u32 LE; SE_LIVE_TRACE_DESC_LEN each). The poll
 * thread ships them (TRC) on its next cycle. Call whenever the local set changes.
 * No-op if 'ds' isn't a live source. */
void se_live_set_tracepoints(const se_data_source* ds, const uint8_t* descs, uint32_t count);

/* A fired tracepoint event drained from the server (v8+): the tracepoint id, the CPU
 * (0 master / 1 slave), the frame it fired on, and the captured SH-2 register file
 * (23 u32 in se_sh2_regs order: r[0..15], pc, pr, sr, gbr, vbr, mach, macl). */
typedef struct se_live_event
{
    uint32_t id;
    uint32_t cpu;
    uint32_t frame;
    uint32_t regs[23];
} se_live_event;

/* Drain up to 'max' received tracepoint events into 'out'; returns the number written
 * (0 if none or not a live source). Call each frame to feed the Log. */
uint32_t se_live_poll_events(const se_data_source* ds, se_live_event* out, uint32_t max);

/* One recorded shadow-stack frame from the server (v9+): the calling instruction, the
 * frame's function entry, its return address, R15 at the call, a cycle stamp, and the
 * emulated frame it was recorded on. Every such frame is genuinely observed, so the
 * client presents them as ● Confirmed. */
typedef struct se_live_call_frame
{
    uint32_t call_site;
    uint32_t func;
    uint32_t ret;
    uint32_t sp;
    uint64_t cycle;
    uint32_t frame_no;
} se_live_call_frame;

/* Copy up to 'max' frames of the last-received shadow call stack for 'cpu' (0 master /
 * 1 slave) into 'out', innermost (current) frame first. Returns the number written (0 if
 * none, not a live source, or the server predates v9). Reads the latest snapshot's
 * stack; call after a stop to populate the Call Stack panel. */
uint32_t se_live_poll_callstack(const se_data_source* ds, int cpu,
                                se_live_call_frame* out, uint32_t max);

/* Copy the emulator's live host keyboard bindings for 'port' (0/1) into 'out' (v10+):
 * up to 'max' USB-HID scancodes, one per Saturn pad button in ascending SE_PAD_* order
 * (UP,DOWN,LEFT,RIGHT,A,B,C,X,Y,Z,L,R,START), -1 where no keyboard key is bound. Returns
 * the number written (0 if not a live source or the server predates v10). Lets the
 * controller panel mirror the user's keys with no config-file upload. */
uint32_t se_live_poll_keymap(const se_data_source* ds, uint32_t port,
                             int32_t* out, uint32_t max);

/* Drain diagnostic log lines the emulator sent (v11+) into 'out', a caller-provided
 * buffer of 'maxLines' rows each 'lineLen' bytes (each row NUL-terminated). Returns the
 * number of lines written (0 if not a live source or the server predates v11). Lets the
 * emulator surface diagnostics — e.g. the controller input it receives — in the client's
 * Log window with no separate console. */
uint32_t se_live_poll_log(const se_data_source* ds, char* out, uint32_t lineLen,
                          uint32_t maxLines);

/* Drain the savestate blocks the emulator sent (v16+) — the per-frame delta/keyframe stream
 * that powers rewind "Play from here". 'cb' is called once per block, oldest first, with the
 * block kind (SE_LIVE_STATE_KIND_*), its frame number, the base keyframe frame it deltas
 * against (== frame for a keyframe), and its opaque RLE payload. Returns the number drained
 * (0 if not a live source or the server predates v16). Call each frame to feed the client's
 * FrameRecorder ring. */
typedef void (*se_live_state_block_cb)(void* user, uint8_t kind, uint32_t frame,
                                       uint32_t base, uint32_t full_len,
                                       const uint8_t* payload, uint32_t len);
uint32_t se_live_drain_state_blocks(const se_data_source* ds,
                                    se_live_state_block_cb cb, void* user);

/* Read the last stop event reported by the server's control block (v5+). Fills
 * '*reason' (SE_LIVE_STOP_*), '*cpu' (0 master / 1 slave), and '*pc' when non-NULL.
 * Returns 1 if the emulator is halted on a breakpoint, 0 otherwise (or not live). */
int se_live_get_stop(const se_data_source* ds, uint32_t* reason, uint32_t* cpu,
                     uint32_t* pc);

#ifdef __cplusplus
}
#endif

#endif /* SATURNEXPLORER_LIVE_DRIVER_H */
