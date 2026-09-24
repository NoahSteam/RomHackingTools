# Timeline / Rewind Verification

## Verified synthetic coverage
- `SaturnExplorerFrameRecorderTests` passed.
- state XOR/RLE codec tests passed.
- savestate-slot reconstruction tests passed.

## Blocked end-to-end
Real restore into an emulator, scrub edits, and "Play from here" resimulation were not executable without a patched emulator.

## Known gaps
The code-review finding that late state-block attachments can bypass immediate byte-budget eviction is not currently caught by tests.

## Web Live defect
Emscripten reports the 4 GiB `size_t` limit converts to zero on wasm32. Web Live rewind/history should be considered Failed until a representable limit is used and tested.
