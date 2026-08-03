// ConditionEval — evaluate a boolean guard expression for a conditional breakpoint or a
// gated tracepoint. Shares FormatString's IFormatContext (registers + memory), so the same
// vocabulary applies: registers/pseudos (r0..r15, pc, pr, sr, gbr, vbr, mach, macl, frame),
// memory derefs *<addr> (4-byte long by default; *<addr>:b / :w / :l for byte/word/long),
// numeric constants (decimal or 0x hex), and C-style operators:
//   || && | ^ &  == != < <= > >=  << >>  + -  and unary ! ~ -.
// A bare value is true when non-zero. An empty expression is always true.
//
// ImGui- and emulator-free so it is unit-testable in isolation.
#pragma once

#include <string>

namespace sfe
{

class IFormatContext;

// Evaluate 'expr' against 'ctx'. An empty/whitespace expr is true. On a parse or
// resolution error the result is fail-OPEN (true) — a broken guard must not silently
// swallow a breakpoint or tracepoint — and '*err' (if given) is set to the reason.
bool ConditionEval(const std::string& expr, const IFormatContext& ctx, std::string* err = nullptr);

// Validate syntax without a context (unknown-symbol resolution is deferred to eval time).
// Returns "" when the expression parses, else a human-readable error.
std::string ConditionValidate(const std::string& expr);

}  // namespace sfe
