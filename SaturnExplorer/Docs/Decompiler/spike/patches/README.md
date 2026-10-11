# Patches to the vendored Ghidra decompiler

Applied on top of Ghidra 12.3 (`918d44e`) by the vendoring step. Each patch is small, has a
reason here, and a test that fails without it.

| Patch | Why | Pinned by |
|---|---|---|
| `0001-cancel-flag.patch` | The bare library has no cancellation hook (Ghidra cancels by killing its decompiler process). Adds a host-owned `std::atomic<bool>*` on `Architecture` and `CancelError` check-points in `FlowInfo::fallthru` (per instruction, so every worklist loop and straight-line run), the jump-table loop of `FlowInfo::generateOps`, `Action::perform` (every action, every level) and `ActionPool::apply` (every 1024 ops). 21 lines, 5 files. Cooperative: `Architecture` construction, the body of one action, printing and teardown have no check-point. | `cancel_spike.cpp` now; `DecompilerRunnerTests` in Step 2 |
