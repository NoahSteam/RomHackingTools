# 09 — Memory Debugger

## MEM-01 — Medium — watch-expression arithmetic silently wraps

Integer parsing and `base +/- offset` arithmetic use unchecked `uint32_t` operations. Mistyped values can wrap into another apparently valid Saturn region.

**Fix:** checked parse/add/subtract with explicit overflow/underflow errors.

## MEM-02 — Low — unreadable regions preserve stale RAM-search candidates

A failed region read during a subsequent scan keeps previous hits unchanged.

**Fix:** mark those hits stale/unverified or expose that the scan was partial.

## MEM-03 — Low — plausible-address UI does not mean readable snapshot address

The plausibility helper covers broader regions than the current backend can read.

**Fix:** distinguish "valid Saturn address" from "available in current source."
