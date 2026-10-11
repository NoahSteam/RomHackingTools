# Patches to the vendored Ghidra decompiler

Applied on top of Ghidra 12.3 (`918d44e`) by the vendoring step. Each patch is small, has a
reason here, and a test that fails without it.

| Patch | Why | Pinned by |
|---|---|---|
| `0001-cancel-flag.patch` | The bare library has no cancellation hook (Ghidra cancels by killing its decompiler process). Adds a host-owned `std::atomic<bool>*` on `Architecture` and `CancelError` check-points in `Action::perform`, `ActionPool::apply` (every 1024 ops) and `FlowInfo::generateOps`. 21 lines, 5 files. | `cancel_spike.cpp` now; `DecompilerRunnerTests` in Step 2 |
