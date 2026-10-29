# Disc browsing, asset search, and patching

Inspected revision: `ff6a3b1fb1c54e4c0344613094ea65292b0023c2` (2026-10-20). Unless explicitly qualified, behavior below is verified from the named source implementations. This is a navigation map, not a promise that a rebuilt image boots every game.

## Purpose and responsibilities

These frontend-owned systems connect captured memory to game files and rebuild modified game data into a disc image. They provide three related but distinct operations:

* Browse an ISO filesystem and resolve a logical sector or live CD frame address to a file.
* Find exact captured bytes, or bytes inside PRS-compressed streams, in host files.
* Record user-accepted memory-to-file mappings, apply current memory bytes to those files through a generated Python script, then rebuild a data track and optionally preserve the original disc's other tracks.

They are **not** services implemented by the portable graphics core. The UI, orchestration, and current project state live in [App.cpp](../../../FrontEnd/src/App.cpp); algorithms live in non-ImGui frontend modules.

## Source locations and important types

| Responsibility | Sources and important symbols |
|---|---|
| Disc-image sector adapter | [Disc/DiscImage.h](../../../FrontEnd/src/Disc/DiscImage.h), [DiscImage.cpp](../../../FrontEnd/src/Disc/DiscImage.cpp): `DiscImage::Open`, `ReadSector`, `ReadBootHeader`, `Reader`, `ImagePath`, `UserOffset` |
| ISO filesystem model | [Disc/IsoFs.h](../../../FrontEnd/src/Disc/IsoFs.h), [IsoFs.cpp](../../../FrontEnd/src/Disc/IsoFs.cpp): `SectorReader`, `IsoEntry`, `IsoFs`, `IsoParse`, `IsoFs::FileAt` |
| Track layout | [Disc/CueSheet.h](../../../FrontEnd/src/Disc/CueSheet.h), [CueSheet.cpp](../../../FrontEnd/src/Disc/CueSheet.cpp): `CueSheet`, `CueTrack`, `CueIndex`, `CueTrackRange`, `ParseCueText`, `CueTrackRanges`, `ParseMsf` |
| Filesystem rebuild | [Disc/IsoBuilder.h](../../../FrontEnd/src/Disc/IsoBuilder.h), [IsoBuilder.cpp](../../../FrontEnd/src/Disc/IsoBuilder.cpp): `IsoBuildOptions`, `IsoBuildResult`, `IsoBuild` |
| Complete image orchestration | [Disc/DiscBuilder.h](../../../FrontEnd/src/Disc/DiscBuilder.h), [DiscBuilder.cpp](../../../FrontEnd/src/Disc/DiscBuilder.cpp): `DiscBuildOptions`, `DiscBuildResult`, `BuildDiscImage`, `VerifyDataTrackEncoding`; [CdSector.cpp](../../../FrontEnd/src/Disc/CdSector.cpp): `EncodeMode1Sector` |
| File search and worker | [DataSearch.h](../../../FrontEnd/src/DataSearch.h), [DataSearch.cpp](../../../FrontEnd/src/DataSearch.cpp): `DataSearchHit`, `SearchCompression`, `SearchProgress`, `SearchData`; [DataSearchRunner.cpp](../../../FrontEnd/src/DataSearchRunner.cpp): `DataSearchRunner`, `DataSearchRequest`, `DataSearchOutcome` |
| Compression | [Prs.h](../../../FrontEnd/src/Prs.h), [Prs.cpp](../../../FrontEnd/src/Prs.cpp): `PRSDecompressor::UncompressData`, `PRSCompressor`, `PuyoPrsCompressor` |
| Accepted mappings and script generation | [PatchLibrary.h](../../../FrontEnd/src/PatchLibrary.h), [PatchLibrary.cpp](../../../FrontEnd/src/PatchLibrary.cpp): `PatchLocation`, `PatchOutcome`, `PatchLocationValid`, `RelativePathUnder`, `PatchLibrary` |
| Shared output primitives | [FileWrite.h](../../../FrontEnd/src/FileWrite.h): `WriteFileAtomically`, `CreateStagingFile`, `PublishFile`; [Disc/PathUtil.h](../../../FrontEnd/src/Disc/PathUtil.h): path containment/identity and checked file-reading helpers; [BinaryWriter.h](../../../FrontEnd/src/BinaryWriter.h): byte emitters and BMP encoding |

`BinaryWriter` is shared export infrastructure, not the patch model: it owns little-endian `PushU16`/`PushU32`, `BuildBmp`, `BuildBmpRgba`, and `BuildIndexedBmp` for image/container output.

## Entry points and execution flows

### Browse a disc or identify a streamed file

`App::DrawDiscExplorer` → `DiscImage::Open` → `IsoParse(mDisc.Reader())` → `IsoFs::entries` for the table. `DiscImage::Open` locates `CD001` at logical sector 16 under candidate 2048-byte, raw MODE1/2352, or MODE2 Form 1/2352 layouts. Its `Reader` callback reads the 2048-byte user payload.

Manual address resolution calls `IsoFs::FileAt`. Live resolution obtains `se_cd_status` with `se_get_cd_status`, converts Saturn FAD to LBA by subtracting 150, then calls the same lookup. It identifies the file extent containing that sector; it does not prove which code or SCSP voice consumed that data. `App::DrawDiscExplorer` also displays `DiscImage::UserOffset(entry.lba)` as the image-file location.

### Find captured bytes in game data

`App::BuildTextureNeedle` reads **packed VDP1 VRAM bytes** through `se_read_vram`; it does not search the decoded RGBA image. `BeginTextureSearch` → `BeginByteSearch` → `RunPendingSearch` → `LaunchSearch`. Hex/assembly byte selections reuse `BeginByteSearch`. `BeginTextureSearchOptions` instead opens the scope/compression chooser before `LaunchSearch`.

`LaunchSearch` copies roots, needle, label, and destination into `DataSearchRequest`; `DataSearchRunner::Start` launches a worker, which calls `SearchData`. The exact path scans host files in overlapping 8 MiB chunks; PRS mode attempts bounded decompression at candidate byte offsets and searches the decompressed output. A PRS hit's offset names the **compressed block start**, unlike a raw hit's exact needle position. `App::PollSearchWorker` receives the joined worker's outcome and routes it to the requesting results window.

### Accept a mapping and apply memory edits to files

`App::BeginLocateSearch` reads the selected memory and surrounding context through `mMemBackend.ReadOne`, retaining selection bytes as the baseline. It launches a raw search over the Data Directory. `DrawLocateResults` adjusts each found context offset by `mLocateCtxBefore`; `AcceptLocateMatch` adds a `PatchLocation` mapping only the selected address/length to that adjusted file offset.

`App::ApplyChangesToDisc` → `PatchLibrary::EmitPython` reads each current memory range using the backend. Only bytes differing from their captured `expected` baseline enter the script. App atomically saves `se_patch.py` under the Data Directory and starts it with `IPlatform::LaunchTool` (`python3`, or Windows `py -3`/`python`). This writes the extracted game files; it does not rewrite the image directly. Launch success is not proof that the script completed successfully.

The emitted script checks baseline bytes before writing, rejects conflicting overlaps by actual file identity, and holds verified file descriptors through validation/writes. Baseline mismatches are skipped unless `--force`; a conflicting overlap prevents all writes. It reports skips/failures through its output/exit code. This is not a general rollback transaction: write-time failures can occur after earlier writes have succeeded (`PatchLibrary::EmitPython`, generated `patch_all`).

### Rebuild and optionally launch

`App::BuildDisc` constructs `DiscBuildOptions` from the Data Directory and selected ROM, falling back to the image opened in Disc Explorer. It takes layout and boot data from the same source; a Data Directory `IP.BIN` takes precedence for boot bytes. Crucially it passes `DiscImage::ImagePath()` (the CUE), not `Path()` (the data-track file), so audio layout remains available. Output is placed beside the Data Directory.

`BuildDiscImage` parses/checks source layout → `IsoBuild` regenerates ISO-9660 data → `EncodeMode1Sector` converts data to raw MODE1/2352 when requested → copies later track ranges → emits CUE → stages/publishes outputs. ISO mode emits only the 2048-byte data track and a single-track CUE. With Build & Launch, App subsequently calls `LaunchSession` with the resulting CUE. The build itself is synchronous in `App::BuildDisc`; file search uses a worker.

## Interactions, state, and lifetime

* `App` owns `mDisc`, parsed `mDiscFs`, search runner/results, locate baseline/context, Data Directory, and `mPatchLib` ([App.h](../../../FrontEnd/src/App.h)). `DiscImage::Reader` captures `this`; it is valid only while that image object remains alive/open.
* Search worker owns its request and private outcome. `Poll` joins before transferring results; only progress atomics are read while it runs. Starting another request cancels the active search and queues one replacement; a third request replaces that queued request. `Stop`/destruction cancels and joins (`DataSearchRunner.cpp`).
* Locate outcomes carry their original roots. `App::PollSearchWorker` and `AcceptLocateMatch` reject mappings if the Data Directory changed; relative paths cannot silently retarget another directory.
* `PatchLibrary::Serialize`/`Deserialize` persist mappings and baseline bytes; `App::DoSaveProject`/`DoOpenProject` provide file/UI orchestration. `Deserialize` refuses malformed records rather than partially replacing the model. `AddOrUpdate` keys entries by `(file, fileOffset)` and tracks dirty state.
* The disc resolver consumes live CD state through the core host API; patch application consumes memory through the frontend backend. Neither is an implicit Saturn-address-to-disc-address translator.

## Extension points and constraints

| Development task | Start here; preserve this behavior |
|---|---|
| Add search compression or scope | `SearchCompression`, `SearchData`, `DataSearchRequest`, then App search options/settings. Keep result offset semantics explicit and expose incomplete searches. |
| Trace a new captured resource to files | Build its packed-byte needle and reuse `BeginByteSearch`/`LaunchSearch`; require a user-accepted `PatchLocation` before treating a content match as ownership. |
| Support another image/track layout | Change `DiscImage` for reading; `CueSheet`/`DiscBuilder` separately for rebuilding. Current rebuild rejection is deliberate, not missing parser plumbing. |
| Change filesystem layout | `IsoBuilder::IsoBuild` and its scanner/name assignment. It rebuilds fresh LBAs; games with hard-coded sector addresses may require additional game-specific work. |
| Improve patch format or apply semantics | `PatchLibrary`, its serialization, generated Python, then App orchestration. Preserve baseline, relative-path, identity, and overlap checks. |
| Export an image | Reuse `BinaryWriter`; transparency requires `BuildBmpRgba`. `BuildIndexedBmp` reconstructs indices by color match, resolves duplicate colors to the first index, and falls back to index 0 on a miss. |

Verified limits:

* ISO reader handles the primary ISO-9660 tree, not Joliet/Rock Ridge names. `IsoParse` bounds depth, entry count, directory-sector work, and repeated extents. A readable PVD with a damaged tree yields `ok=true, complete=false`; callers must surface incompleteness.
* `DiscImage` CUE opening uses its first quoted `FILE` reference and detects the volume relative to that file; this is narrower than the complete track model used by `ParseCueText`.
* Rebuilds use MODE1 for track 1. CUE MODE2 sources, first-track INDEX 00/extra indices, non-BINARY later tracks, and malformed/non-whole track ranges are refused. Later tracks are copied, not regenerated. Missing tracks fail unless `allowPartialTracks` is explicitly set.
* Build outputs cannot be inside the packed directory or alias source files. `Staging::Publish` moves prior outputs aside and restores them on publication failure; a failure to restore is reported with surviving backup paths. This provides handled-error rollback, not an atomic multi-file filesystem transaction.
* PRS searches skip files above 64 MiB, cap each decoded block at 1 MiB, and budget 4 GiB of emitted output per file by default, counting failed decode work too. `DataSearchRunner` reports skipped/budget-exhausted coverage. General decompression defaults to 32 MiB. Exact search checks cancellation between files; PRS also polls within the offset loop. Directory traversal skips directory links and caps depth at 32.
* Patching/build UI is under `SE_ENABLE_LIVE`. Filesystem workflows are desktop-oriented; the Emscripten runner runs inline because its search has no host filesystem. Do not infer desktop functionality from merely finding the modules in a web target.

## Testing

From `SaturnExplorer`, configure/build with the commands in [BUILD.md](../../BUILD.md), then run:

```sh
ctest --test-dir build --output-on-failure -R 'SaturnExplorer(IsoFs|IsoBuilder|DiscBuilder|CdSector|CueSheet|PatchLibrary|PrsSearch|DataSearchRunner|DataSearchWalk|FileWrite)Tests'
```

Targets are registered in [CMakeLists.txt](../../../CMakeLists.txt). POSIX directory-walk tests are conditionally available; use the full CTest run for frontend integration coverage.

| Tests | What to consult/update |
|---|---|
| [IsoFsTests.cpp](../../../FrontEnd/tests/IsoFsTests.cpp) | In-memory directory parsing, sector ownership, malformed/partial trees |
| [IsoBuilderTests.cpp](../../../FrontEnd/tests/IsoBuilderTests.cpp) | Built-image round trips through `DiscImage`/`IsoParse`, identifiers, boot area, filenames, scan failures |
| [CueSheetTests.cpp](../../../FrontEnd/tests/CueSheetTests.cpp), [CdSectorTests.cpp](../../../FrontEnd/tests/CdSectorTests.cpp) | Track/index/MSF/range parsing and raw-sector encoding |
| [DiscBuilderTests.cpp](../../../FrontEnd/tests/DiscBuilderTests.cpp) | Data/audio preservation, refused layouts, source identity/output containment, open-CUE rebuild, staged publication fault injection, corrupted-sector detection |
| [PatchLibraryTests.cpp](../../../FrontEnd/tests/PatchLibraryTests.cpp) | Mapping validity, serialization, script generation/execution, baseline/overlap/path defenses |
| [PrsSearchTests.cpp](../../../FrontEnd/tests/PrsSearchTests.cpp) | PRS round trips/search bounds, emitted-work budgets, partial coverage |
| [DataSearchRunnerTests.cpp](../../../FrontEnd/tests/DataSearchRunnerTests.cpp), [DataSearchWalkTests.cpp](../../../FrontEnd/tests/DataSearchWalkTests.cpp) | Result delivery, queue replacement/cancel/join, symlink-cycle avoidance and depth cap |
| [TopBarTests.cpp](../../../FrontEnd/tests/TopBarTests.cpp) | Build-command enablement/routing and shared menu model |

Tests were inspected, not executed as part of this subsystem investigation. A matching rebuilt-image encoding test verifies consistency with this encoder; `VerifyDataTrackEncoding` can additionally compare re-encoded bytes against an actual original raw track. Neither establishes game boot/LBA compatibility.

## Related documentation and investigation limits

Read [ARCHITECTURE.md](../../ARCHITECTURE.md) for the original component split and [06_MEDIA_DISC.md](../../FunctionalityVerification/06_MEDIA_DISC.md) for historical media verification. Source is authoritative over those records. [MemoryAndHistory.md](MemoryAndHistory.md) covers memory reads/edits and [StateSources.md](StateSources.md) covers emulator transport.

This pass traced the principal App flows, algorithms, ownership, and representative regression tests. It did not validate a real game's bootability, the launched Python process's runtime UI/reporting, or every platform's filesystem failure behavior. The PRS compressor is present/reused in tests; no general compression/extraction UI was established from this investigation. Asset search is content matching plus accepted mappings, not a complete provenance graph.
