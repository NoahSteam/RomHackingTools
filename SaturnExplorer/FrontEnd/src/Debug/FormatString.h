// FormatString — the tracepoint output mini-syntax (see EXECUTION_ACTIONS.md).
// Literal text with `{value[:spec]}` substitutions, e.g.
//   "HP = {*0x6034F20:u16}"  ->  "HP = 235"
//   "Portrait = {r5:X8}"      ->  "Portrait = 0603A420"
//   "Dialogue {r4}"           ->  "Dialogue 182"
//   "Name = {*r4:string}"     ->  "Name = Sakura"
//
// value:  a register/pseudo (pc, pr, sr, gbr, vbr, mach, macl, frame, cycle, r0..r15)
//         or a memory deref *<addr> where <addr> is a register/constant, optionally
//         +offset (e.g. *r4, *0x6034F20, *r3+8).
// spec:   X8/X4/X2 (upper hex, zero-padded), x8.. (lower hex), u8/u16/u32 (unsigned
//         decimal), s8/s16/s32 (signed decimal), string. The spec's width also sets a
//         memory deref's read size. Default: address-like registers (pc/pr/gbr/vbr)
//         print as X8, other registers as decimal, memory as u32 decimal.
//
// Evaluation is deliberately independent of se_context/ImGui (an IFormatContext
// abstracts registers + memory) so it is unit-testable and reusable by both the live
// preview and, later, the client-side formatting of emulator-captured values.
#pragma once

#include <cstdint>
#include <string>

namespace sfe
{

// Supplies the register + memory values a template references at evaluation time.
class IFormatContext
{
public:
    virtual ~IFormatContext() = default;

    // Resolve a lowercase register/pseudo name to its value. False if unknown/absent.
    virtual bool GetValue(const std::string& name, uint32_t& out) const = 0;

    // Read 'size' (1/2/4) big-endian bytes at 'addr' as an unsigned value. False on
    // an unreadable address.
    virtual bool ReadMem(uint32_t addr, uint32_t size, uint32_t& out) const = 0;

    // Read a NUL-terminated string at 'addr' (printable ASCII, capped at maxLen).
    // False on an unreadable address.
    virtual bool ReadString(uint32_t addr, std::string& out, size_t maxLen) const = 0;
};

// Render 'tmpl' against 'ctx'. Unresolvable tokens render as "?"; literal braces are
// written as "{{" / "}}". Never throws.
std::string FormatEvaluate(const std::string& tmpl, const IFormatContext& ctx);

// Validate 'tmpl' without evaluating it. Returns "" if every token is well-formed,
// otherwise a one-line description of the first problem (for the editor).
std::string FormatValidate(const std::string& tmpl);

}  // namespace sfe
