# 06 — Live Transport / Driver

## LIVE-01 — Medium — capabilities are advertised before protocol negotiation

`se_live_open` advertises versioned features including rewind before the server version is known. The frontend guards rewind with a version check, but the public API can still report rewind support and send `LST` to a pre-v16 server.

An old server treats an unknown verb like GET and does not consume the attached payload, desynchronizing the stream.

**Fix:** negotiated capabilities or version checks inside every versioned operation.

## LIVE-02 — Medium — breakpoint/tracepoint APIs trust pointer/count pairs

`se_live_set_breakpoints` and `se_live_set_tracepoints` construct vector ranges directly from `descs` and `count`.

Issues: null pointer with nonzero count, unchecked multiplication, unbounded allocation, and exception leakage.

**Fix:** validate, checked multiplication, protocol caps, and no-throw behavior.

## LIVE-03 — Medium — pending write queues are unbounded

Each memory poke creates a vector and only one is shipped per poll cycle. A fast producer can grow memory indefinitely.

**Fix:** coalesce writes and impose a queued-byte budget/backpressure.

## LIVE-04 — Medium — rewind wire-width truncation

`uint64_t` frame and `size_t` lengths are silently cast to 32-bit protocol fields.

**Fix:** reject values outside protocol limits before serialization.
