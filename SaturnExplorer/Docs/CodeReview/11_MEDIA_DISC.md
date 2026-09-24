# 11 — Media / Disc

## DISC-01 — High — BIN/CUE build can report success with missing tracks

When copying tracks after Track 01, a failed copy only adds a warning and continues. The final result still sets `ok = true`.

That violates the stated goal of preserving all non-data tracks verbatim.

**Fix:** fail closed by default; make partial builds an explicit opt-in. Build transactionally into temporary outputs and rename only after all tracks succeed.

## DISC-02 — Medium — duplicate-name suffixing can violate ISO Level-1 limits

Names are first mapped to Level-1 widths, but collision suffixes are inserted/appended without retruncating.

**Fix:** reserve suffix space inside the allowed filename/directory component width.

## DISC-03 — Medium — aggregate ISO arithmetic is unchecked

Files >4 GiB are filtered individually, but aggregate sector/path-table arithmetic uses `uint32_t`.

**Fix:** checked additions and an explicit Saturn/CD maximum sector budget.

## MEDIA-01 — Low — SCSP preview decoder is intentionally partial

Noise/zero sources and SBCTL manipulation are not reconstructed. Label those cases unsupported rather than implying bit-accurate export.
