# 07 — Emulator Hooks / Export Server

## HOOK-01 — High — Windows shutdown does not truly join blocked threads

The Windows deinit path waits 1000 ms for worker/server threads, closes their handles, then destroys critical sections and shared global state.

A thread blocked in synchronous named-pipe I/O can still be alive after the timeout. Closing a thread handle does not terminate the thread. If it later wakes, it can touch destroyed locks/freed state.

**Fix:** cancellable/overlapped I/O or an explicit stop event; close/cancel active pipe operations; wait for actual thread termination before destroying shared state.

## HOOK-02 — Medium — server command argument allows unbounded local work/allocation

BKP, WRM, WRS, LST, and TRACE use the 32-bit argument for receive loops/allocation with incomplete command-specific caps.

**Fix:** define protocol maxima for every verb and reject/drain safely.

## HOOK-03 — Medium — memory-write channel is privileged

WRM can write arbitrary bus addresses through the emulator cheat/debug path. This is useful but should be documented as a privileged local-control channel, especially for the loopback TCP/web bridge.
