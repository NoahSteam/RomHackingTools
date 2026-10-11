# Patches to the vendored Ghidra decompiler

Applied on top of Ghidra 12.3 (`918d44e`) by `../vendor.py`, in file-name order, with
`git apply --check` first so that a patch which no longer applies stops the vendoring step
instead of being skipped. The files under `../cpp/` are never edited by hand: a change to
the engine is a new numbered patch here, with a reason in this table and a test that fails
without it.

Each patched file also gets a one-line "Modified for SaturnExplorer" notice in its licence
header, because Apache-2.0 §4(b) asks modified files to say so.

| Patch | Why | Pinned by |
|---|---|---|
| `0001-cancel-flag.patch` | The bare library has no cancellation hook (Ghidra cancels by killing its decompiler process). Adds a host-owned `std::atomic<bool>*` on `Architecture` and `CancelError` check-points in `FlowInfo::fallthru` (per instruction, so every worklist loop and straight-line run), the jump-table loop of `FlowInfo::generateOps`, `Action::perform` (every action, every level) and `ActionPool::apply` (every 1024 ops). 21 functional lines in 5 files, plus the notices. Cooperative: `Architecture` construction, the body of one action, printing and teardown have no check-point. Identical in behaviour to the spike's `Docs/Decompiler/spike/patches/0001-cancel-flag.patch`; the only differences are the notices and `CancelError` placed before `RecovError`'s doc comment instead of inside it. | `Docs/Decompiler/spike/cancel_spike.cpp`; `DecompilerRunnerTests` from Step 2 |
