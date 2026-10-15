# Findings ledger

One row per finding, kept for ever. `id` is `A<area>-<n>`. Status: `open`, `fixed <commit>`, or
`accepted: <reason>`. The fixture column names the fixture in the suite once fixed, or holds the
reviewer's words and literals until then. Rounds before this ledger are recorded in
`Docs/FunctionalityVerification/15_DECOMPILER_STEP5.md`.

| id | area | severity | finding | fixture | status | found | closed |
|---|---|---|---|---|---|---|---|
| A10-1 | 10 | P3 | `se-sleighc` (upstream SLEIGH compiler, `slgh_compile.cc:2340-2341`) downcasts an `OperandSymbol` to `LabelSymbol` (UBSan vptr). Build tool only; the engine does not run it. | none (CI sanitizer job, informational log) | open | program/ci | |
| A10-2 | 10 | P3 | `se-sleighc` loads an out-of-range `v_field` enum (`semantics.hh:88`). | none (CI sanitizer job, informational log) | open | program/ci | |
| A10-3 | 10 | P3 | `se-sleighc` leaks a few bytes per run (LeakSanitizer), so `SlaFreshnessTests` fails under LSan; the freshness test runs as informational in CI until fixed or `se-sleighc` is built unsanitized. | none | open | program/ci | |
