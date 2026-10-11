# Vendored: zlib

- **Upstream:** https://github.com/madler/zlib
- **Version:** 1.3.1, tag `v1.3.1`, commit `51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf` — the
  same release Ghidra 12.3 vendors for its own decompiler build.
- **License:** zlib — see `LICENSE` (attribution only).
- **Unmodified.** The files are byte-identical to the tag.

## Why it is vendored

Two consumers on the desktop builds, and neither may depend on whether the machine happens to
have zlib installed:

- The Ghidra decompiler (`../ghidra-decompiler`) reads compressed `.sla` files, and
  `se-sleighc` writes them.
- The savestate driver inflates gzip-wrapped Mednafen states. It used to call
  `find_package(ZLIB)` and silently lose that ability when zlib was absent, and the
  checked-in Visual Studio projects never had it. With this copy `SE_HAVE_ZLIB` is defined
  on every desktop build (PLAN.md A6).

The web build keeps Emscripten's own zlib port (`-sUSE_ZLIB=1`) and never compiles this copy.

## What's included

The library core (`adler32 compress crc32 deflate infback inffast inflate inftrees trees
uncompr zutil` and their headers, plus `gzguts.h`, which `zutil.c` includes). The `gz*.c`
file-I/O layer, the contrib tree, the examples and upstream's build files are left out;
nothing here opens `.gz` files by name. Built as the static target `SaturnExplorerZlib`
(`CMakeLists.txt` at the SaturnExplorer root, `SaturnExplorerZlib.vcxproj` here).

## Updating

Copy the same file list from a new tag, update the version and commit above, rebuild, and run
`SaturnExplorerSavestateShapeTests` (the gzip case) and `SaturnExplorerSlaFreshnessTests`
(a different deflate can change the compressed `.sla` bytes; regenerate with `se-regen-sla`).
