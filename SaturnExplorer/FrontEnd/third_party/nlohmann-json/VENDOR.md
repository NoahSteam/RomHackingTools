# Vendored: JSON for Modern C++ (nlohmann/json)

- **Upstream:** https://github.com/nlohmann/json
- **Version:** `v3.11.3`, the single-header release (`single_include/nlohmann/json.hpp`),
  copied verbatim to `include/nlohmann/json.hpp`.
- **SHA-256 of that file:** `9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6`.
  The tag's `single_include/` file and the release asset `json.hpp` were downloaded separately and
  hash identically.
- **License:** MIT — `LICENSE.MIT` is upstream's, copied verbatim, and travels with the binary
  (`licenses/nlohmann-json/` beside the exe, `Resources/licenses/` in the macOS bundle).
- **Modified:** not at all. Update by replacing the header and the hash above together.

## Why it is here

The AI investigation feature (`Docs/AIInvestigation/PLAN.md` §6.2) imports a JSON response that an
external agent wrote, so the reader must be strict where the parser a casual project would reach
for is lenient. This library was chosen because:

- **Maintained and C++11 or later.** Active releases, supports MSVC 2015+ (the MSBuild project uses
  `v142`), GCC, Clang and Apple clang. One header, no build step, no link dependency.
- **It has an event (SAX) interface.** `json::sax_parse` reports every token to a handler and stops
  the moment the handler returns false. SE builds its own small value tree from those events
  (`FrontEnd/src/AIInvestigation/JsonReader.cpp`) so that the three things the plan demands are
  enforced by SE's code, not assumed of the library:
  1. **Duplicate object keys are rejected.** `nlohmann::json` itself keeps the last one silently;
     the SAX `key()` event lets the handler see each name and fail on a repeat.
  2. **Integers are lossless.** The library reports a non-negative integer as `uint64_t` and a
     negative one as `int64_t`; an integer outside both ranges arrives as a *float* event whose
     text is still digits only, which the handler rejects as out of range instead of rounding.
  3. **Limits are enforced while parsing**: input bytes (before the library sees any), nesting depth,
     array length, object member count and string length, so a hostile document fails early
     instead of after it has been built.
- **UTF-8 is validated by the lexer.** Malformed UTF-8 and unpaired `\u` surrogates in strings are
  parse errors.

Alternatives considered:

| Library | Why not |
|---|---|
| RapidJSON | Last release was 2016; the project is effectively unmaintained. |
| jsoncpp | Has `rejectDupKeys`, but no array/string limits, several source files to vendor, and a DOM that converts out-of-range integers to doubles. |
| simdjson | Fast and strict, but the on-demand API is built for read-only access, not for re-serialising a canonical form, and its portability on the MSBuild toolset is a risk this feature does not need. |
| A hand-written parser | The one thing a security-sensitive input path should not be. SE writes the *tree*, not the tokenizer. |

The header is large and slow to compile, so exactly one translation unit includes it
(`JsonReader.cpp`); everything else sees only SE's own `JsonValue`.
