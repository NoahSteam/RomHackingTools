# Media / Disc Verification

## Passed
- IsoFs tests
- MODE1/2352 EDC/ECC tests
- CUE parser/range tests
- DiscBuilder tests
- SCSP mix tests

## Fixed — ISO zero-length-file overlap
The builder gives a zero-length file no sectors, so its recorded LBA is the next file's first
sector. `IsoFs::FileAt` used to treat empty files as one sector and returned whichever came
first in enumeration order. It now skips empty files and computes the extent end in 64 bits.
Covered by `IsoFsTests` (synthetic shared LBA) and `IsoBuilderTests` (built image).

## Fixed — DISC-01 transactional build
A later-track copy failure fails the build (since the earlier fix), and every output is now
staged in exclusively created files and only moved into place once the whole set is built, so
a failed rebuild leaves the previous outputs intact. Output names that are (links to) a source
file, outputs inside the packed Data Directory, and device/directory output paths are refused
before anything is written. `DiscBuilderTests` covers each case.

## Refused layouts
Rather than produce a wrong image, the builder refuses: a MODE2 first track, a first track with
INDEX 00 or extra indices (a PREGAP is carried over), non-BINARY track files (WAVE, MOTOROLA,
...), and track files that are not a whole number of sectors or whose indices are out of order.
