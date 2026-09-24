# Sanitizer Verification

Ubuntu 24.04 / GCC 13.3 with AddressSanitizer + UndefinedBehaviorSanitizer.

## Result
24/26 tests passed.

Failures:
- ISO-builder functional assertion.
- Shadow-call-stack UBSan failure for negative BSR displacement shift.

No additional ASan memory violation was reported before the suite finished.

## Compiler warnings
- `SavestateSlots.cpp`: timestamp `snprintf` may truncate for extreme year values.
- Emscripten/wasm32: FrameRecorder 4 GiB limit converts to zero.
