# 08 — CPU Debugger

## CPU-01 — Medium — conditional breakpoint lookup ignores CPU

The wire descriptor includes CPU and the emulator install hook receives CPU, but `ConditionalExecutionAt(addr)` ignores CPU and the App evaluates the first guarded breakpoint at that address.

Master/slave breakpoints at the same PC with different conditions can therefore use the wrong guard.

**Fix:** settle semantics. If CPU-specific, use `(stopCpu, stopPc)` consistently. If truly shared, remove CPU from the execution-breakpoint representation.

## CPU-02 — Medium — disabled execution BP can suppress access-log auto-resume

`IsAccessLogHalt(pc)` treats any execution breakpoint at the PC as owning the halt without checking `enabled`.

**Fix:** only enabled execution breakpoints should suppress access-log handling.

## CPU-03 — Low — heuristic function address is not a real recovered function start

The heuristic call stack sets `functionAddress = returnAddress` after validating the preceding call opcode.

**Fix:** mark function entry unknown unless a target can actually be decoded.
