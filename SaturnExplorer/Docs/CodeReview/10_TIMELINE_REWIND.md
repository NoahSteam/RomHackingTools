# 10 — Timeline / Rewind

## REW-01 — Medium — state attachments bypass eviction

`FrameRecorder::AttachStateBlock` increments frame bytes and `mBytes` but does not call `Evict()`.

Large asynchronous state payloads can push the recorder beyond its byte ceiling until another compressed frame arrives.

**Fix:** immediately rerun dependency-aware eviction after attachment.

## REW-02 — Medium — corrupt compressed frame becomes zeroed valid-looking state

`DecompressRegion` zeros output on decompression failure and `Select` still succeeds.

**Fix:** propagate corruption and reject the historical frame instead of presenting zero memory.

## REW-03 — Low — `CanReconstruct` checks presence, not payload validity

It does not validate RLE data or decoded-size compatibility.

**Fix:** validate on attachment or cache reconstructability.

## REW-04 — Medium — live rewind queue is count-bounded, not byte-bounded

Up to 1024 state blocks can queue, with large per-block limits.

**Fix:** bound by total bytes and drop obsolete dependency groups intelligently.
