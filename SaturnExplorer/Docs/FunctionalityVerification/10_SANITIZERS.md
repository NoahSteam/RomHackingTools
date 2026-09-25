# Sanitizer Diagnostics

This document records **development-only diagnostics**. Linux is not a supported Saturn Explorer platform.

Ubuntu 24.04 / GCC 13.3 was used with AddressSanitizer + UndefinedBehaviorSanitizer during the initial verification pass because it provided useful instrumentation.

## Result

24/26 tests passed in that diagnostic environment.

Findings:

- ISO-builder functional assertion failure.
- Shadow-call-stack UBSan failure for negative BSR displacement shift.

No additional ASan memory violation was reported before the suite finished.

## Why these findings still matter

Although Linux itself is unsupported, both findings are in portable/shared C/C++ code and can affect Windows/macOS behavior. They should therefore be treated as code-quality/correctness defects, not Linux platform defects.

## Additional compiler diagnostics

- `SavestateSlots.cpp`: timestamp `snprintf` may truncate for extreme year values.
- Emscripten/wasm32: FrameRecorder 4 GiB limit converts to zero. This is irrelevant to supported-platform status unless Web becomes supported later.
