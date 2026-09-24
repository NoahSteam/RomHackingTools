# 02 — Snapshot / State Ingestion

## SNAP-01 — Medium — partial hardware regions remain representable as valid state

Snapshot capture accepts short region reads. Several consumers assume hardware-sized/power-of-two buffers. CRAM lookup is a clear example: `index & (words - 1)` behaves like modulo only when the entry count is a power of two.

**Fix:** either require exact hardware-sized regions when a capability is advertised, or make downstream consumers safe for arbitrary partial lengths.

## SNAP-02 — Low — snapshot validity is VDP1-centric

A source with useful VDP2-only data cannot become a generally valid context.

**Fix:** if VDP2-only analysis is desirable, make validity capability-based rather than tied to VDP1 VRAM.

## Positive

The immutable-per-frame capture model, explicit `RebuildDerived`, and derive-serial invalidation model are good architectural choices.
