# Snapshot / Offline Input Verification

## Verified
- `SaturnExplorerSeStateCodecTests` passed.
- `SaturnExplorerSavestateSlotsTests` passed.
- synthetic Context/recorded-frame paths are exercised by existing tests.

## Partial
Hardware snapshot and state-rebuild machinery receives substantial synthetic coverage.

## Blocked
No real Yabause, Yaba Sanshiro, Kronos, Mednafen, or Beetle Saturn savestate fixtures are checked in. Compatibility with each real format therefore was not claimed as verified.

### Needed
Add representative fixture states and assert known registers, VRAM/CRAM, SH-2 state, reconstructed commands, and rendered output.
