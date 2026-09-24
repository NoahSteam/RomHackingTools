# 05 — Offline Inputs / Savestate Driver

## OFF-01 — Medium — structural YSS recognition can silently accept incompatible forks

The Yabause-family parser intentionally uses structural layout detection instead of versions. That supports multiple emulator families, but a fork that preserves total section size while moving fields can be misdecoded rather than rejected.

**Fix:** strengthen validation and keep real fixtures from every supported emulator family.

## OFF-02 — Low — `ftell`/`long` is not portable for large files

The loader uses C `ftell` into `long`; Windows keeps `long` at 32 bits.

**Fix:** use a 64-bit file API with an explicit maximum accepted savestate size.

## OFF-03 — Low — malformed odd-sized u16 fields are tolerated

Mednafen u16-array normalization processes pairs and leaves an unmatched byte.

**Fix:** reject odd-length u16 fields.
