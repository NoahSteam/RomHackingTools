# Media / Disc Verification

## Passed
- IsoFs tests
- MODE1/2352 EDC/ECC tests
- CUE parser/range tests
- DiscBuilder tests
- SCSP mix tests

## Failed — ISO zero-length-file overlap
Linux consistently fails `SaturnExplorerIsoBuilderTests` at:
`FAIL: FileAt resolves BGM01`

Root cause:
1. builder allocates zero sectors to a zero-length file;
2. next file may receive the same LBA;
3. reader's `FileAt` treats zero-length files as one sector;
4. lookup becomes order-dependent.

macOS passes only because directory enumeration order avoids exposing that overlap in this fixture.

## Additional unverified risk
DiscBuilder tests do not force a later-track copy failure, so the code-review issue where a missing copied track can still yield overall success remains untested.
