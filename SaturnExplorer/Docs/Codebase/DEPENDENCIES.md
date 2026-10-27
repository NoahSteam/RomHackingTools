# Dependencies and modification boundaries

Inspected revision: `ff6a3b1fb1c54e4c0344613094ea65292b0023c2` (the update-checker paragraph under
"External services and programs" was rechecked at `de6c3bd3`).
Integration facts are verified against [CMakeLists.txt](../../CMakeLists.txt) and linked contracts.
This is a navigation map, not a complete license audit or software bill of materials.

## Application-owned code

`Core/`, `Drivers/`, public `include/saturnexplorer/`, portable `FrontEnd/src/`, platform backends
and `Integration/` implement SE-specific behavior. Core and savestate code use C++14 and the
flat public C ABI. Driver and frontend capabilities are independent axes: an OS backend is not
a memory-source driver. Shared Saturn format/address rules live in `Drivers/Common/`,
`Integration/Common/`, `FrontEnd/src/Sh2Address.*`, `SaturnRegions.h` and the public headers;
reuse the relevant existing map rather than inventing new mirror or region rules.

## Embedded and external components

| Component | Location/build use | Modification contract |
|---|---|---|
| Dear ImGui | [FrontEnd/third_party/imgui](../../FrontEnd/third_party/imgui); shared UI and backend adapters | Keep ImGui types in frontend/platform code; core API has no UI dependency. Use SE widget helpers before patching ImGui. |
| Ghidra native decompiler + SuperH spec | [VENDOR.md](../../FrontEnd/third_party/ghidra-decompiler/VENDOR.md), `SaturnExplorerGhidraDecomp` | Pinned upstream and reproducible local patch series; modify through patches/vendor workflow, not an unexplained edit to copied engine files. Read bump checklist and current patch README. |
| zlib | [FrontEnd/third_party/zlib](../../FrontEnd/third_party/zlib), `SaturnExplorerZlib` on desktop | Supports compressed Ghidra language data and Mednafen gzip states; web uses Emscripten zlib port. Do not make behavior depend on an incidental system installation. |
| nlohmann JSON | [VENDOR.md](../../FrontEnd/third_party/nlohmann-json/VENDOR.md), single header v3.11.3 | Copied verbatim; replacement must update the recorded hash. Only `JsonReader.cpp` includes it; SE SAX handling enforces duplicate-key, integer and input-limit rules. |
| Fonts | [FrontEnd/third_party/fonts](../../FrontEnd/third_party/fonts) | Ship the font license notices with the binary. |
| Windows platform libraries | [WindowsPlatform.cpp](../../FrontEnd/Platforms/Windows/WindowsPlatform.cpp), Win32/D3D11/WinHTTP | OS/GPU/network code stays behind Seam C; process/argument and save-error contracts are shared with other backends. |
| SDL2 / OpenGL / optional curl | [WebPlatform.cpp](../../FrontEnd/Platforms/Web/WebPlatform.cpp), native macOS/Linux frontend | CMake requires SDL2/OpenGL for the native frontend; HTTPS is optional through `SE_HAVE_CURL`. Native macOS menu bridge is [MacMenuBar.mm](../../FrontEnd/Platforms/macOS/MacMenuBar.mm). |
| Emscripten | [WebMain.cpp](../../FrontEnd/Platforms/Web/WebMain.cpp), WebGL2/SDL2 ports | Browser lifecycle/filesystem limits apply; decompiler forced off. Optional `SE_WEB_LIVE` needs pthreads and WebSocket-to-TCP proxy with cross-origin isolation. Default Pages build is viewer-only. |
| Mednafen / Yabause | [Integration/Mednafen](../../Integration/Mednafen/README.md), [Integration/Yabause](../../Integration/Yabause/README.md) | Separate emulator source/build; SE's patch scripts/glue are the maintained integration. `_emu/` is generated/untracked; editing only that checkout does not persist the integration. |

[Ghidra patch README](../../FrontEnd/third_party/ghidra-decompiler/patches/README.md) explains
why individual patches exist and their tests. Do not remove unusual load preservation or alias
handling as redundant: captured memory, device volatility and instruction semantics differ from
ordinary executable-file decompilation. [Decompiler](Systems/Decompiler.md) explains the host contract.

## Generated code and artifacts

| Artifact | Source of truth / regeneration |
|---|---|
| Ghidra `sources.cmake` and copied engine/spec files | `vendor.py` + pinned upstream + `patches/` |
| `processors/SuperH/sh-2.sla`, `FrontEnd/src/Decompiler/Sh2SpecData.h` | SuperH SLEIGH inputs; CMake target `se-regen-sla`; freshness tests check both embedded and compiled forms |
| Build identity header | [BuildInfo.h.in](../../FrontEnd/BuildInfo.h.in), [GenerateBuildInfo.cmake](../../cmake/GenerateBuildInfo.cmake); generated in build tree |
| AI schema/action list | [ActionRegistry.cpp](../../FrontEnd/src/AIInvestigation/ActionRegistry.cpp); `SaturnExplorerActionRegistryTests` freshness/write mode, see [AI investigation](Systems/AIInvestigation.md) |
| Patched emulator working copy | `Integration/Mednafen/apply.py` and integration scripts, not manual edits under `_emu/` |
| Bundled macOS dylibs/signatures | CMake install/packaging steps in [BUILD.md](../../BUILD.md); rewriting library paths requires re-signing |

## Integration contracts

The live client/server share [SeLiveProtocol.h](../../Integration/Common/SeLiveProtocol.h),
including wire version, sizes, commands and capabilities. Protocol additions must account for
older servers, snapshot ownership and asynchronous command application, not just add a field
on one side. The emulator invokes hooks in its own execution contexts; restore must happen at
a safe frame boundary. See [StateSources](Systems/StateSources.md).

Mednafen savestate hooks use internal APIs and can silently become stub implementations after
an upstream bump. The existing [Mednafen README](../../Integration/Mednafen/README.md) rewind
section is mandatory before modifying that integration; verify the real path, not merely that
it compiles. Distribution obligations differ between SE and emulator binaries; consult
[Integration/DISTRIBUTION.md](../../Integration/DISTRIBUTION.md) before packaging.

## External services and programs

The update checker calls GitHub's `releases/latest` API through `IPlatform::HttpsGet` and compares the
build's baked-in release tag (`kBuildVersion`) with the latest published release's tag. A build with no
tag (built from source, or an untagged CI run) is instead compared with the latest commit on the default
branch (`commits/HEAD`), which indicates difference, not ancestry, and is shown the latest release. It
never downloads anything. There is no AI
provider/network integration in M0. Live state is an emulator connection, not a hosted service.
External diff programs and generated patch scripts are launched through platform process APIs;
their availability, exit/completion and tool-specific output are separate from launch success.
The patch writer depends on the Python interpreter chosen by its App launch path. Disc ECC
encoder validation is part of `App::VerifyEncoder`; see [DiscAndPatching](Systems/DiscAndPatching.md).

CI entry points (repository-root paths):
[verification](../../../.github/workflows/saturn-explorer-verify.yml),
[decompiler](../../../.github/workflows/saturn-explorer-decompiler.yml),
[web deployment](../../../.github/workflows/web.yml). Exact matrix/commands should be read from
those files when changing build or release behavior; they were surveyed here, not rerun.
