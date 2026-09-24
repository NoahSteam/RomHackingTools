# Live Transport / Emulator Verification

## Live reconnect — Verified
`SaturnExplorerLiveReconnectTests` passed on Linux and macOS.

## Emulator shadow call stack — Failed under UBSan
Normal tests pass, but UBSan reports:
`se_mednafen_glue.c:444: left shift of negative value`
for backward BSR flow tracking.

## Real live session — Blocked
Not exercised without a patched running emulator:
pause/resume, frame/instruction step, breakpoints, watchpoints, tracepoints, controller injection, live writes, SCSP/CD state, emulator slots, rewind/load-state.

## Cross-version protocol — Partial
Reconnect/framing paths are tested, but old-server compatibility is not fixture-tested.
