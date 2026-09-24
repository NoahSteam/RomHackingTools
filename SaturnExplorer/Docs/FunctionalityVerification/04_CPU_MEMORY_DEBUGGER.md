# CPU / Memory Debugger Verification

## Verified automated coverage
The following passed on Linux/macOS:
- ConditionEval
- MemorySearch
- AccessLog
- FunctionNames
- M68K disassembly
- SH-2 operand hover
- SH-2 register metadata

This verifies substantial parser/model/UI-interaction behavior.

## Partial / Blocked
Actual live SH-2 breakpoint hits, master/slave conditional behavior, watchpoint stops, Step Into/Over/Out, live register changes, confirmed call-stack reconciliation, and hex-editor write persistence require a live emulator harness.
