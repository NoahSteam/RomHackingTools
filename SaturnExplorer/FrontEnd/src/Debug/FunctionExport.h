// FunctionExport -- the text behind SH-2 Assembly's "Export SH-2 Function" and "Export Decompiled
// Code": a function's disassembly and its decompiled pseudocode, as files. Pure: no ImGui, no
// engine, no file I/O, so the formatting is unit-tested; App finds the function (the decompiler's
// flow knows which instructions belong to it), reads the bytes and saves the text.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sfe
{

enum class FunctionExportKind : uint8_t
{
    None,
    Listing,      // "Export SH-2 Function": the disassembly, a .s file
    Pseudocode,   // "Export Decompiled Code": the pseudocode, a .c file
};

// The menu item's label, which also heads its messages.
const char* FunctionExportLabel(FunctionExportKind kind);

// What the file says about where the text came from, in its header comment.
struct FunctionExportInfo
{
    std::string name;        // the function's name ("" when it has none: sub_ and the entry, as FunctionNames spells it)
    uint32_t    entry = 0;   // canonical entry address
    int         cpu = 0;     // 0 master, 1 slave
    std::string source;      // "live, paused", "recorded frame", "dump / savestate", ...
    uint64_t    frame = 0;   // the frame the memory is from (0: not stated)
};

// 'text' as one C comment line: line breaks become blanks and "*/" is broken up, so it cannot end
// the comment early. Shared with the pseudocode panel's copy marks.
std::string CCommentLine(std::string text);

// Code ranges as the decompiler's flow reports them: each the lowest and highest instruction
// address of a basic block (inclusive, both instruction starts). Sorted and merged, so blocks that
// touch or overlap become one run; empty or inverted ranges are dropped.
std::vector<std::pair<uint32_t, uint32_t>> MergeCodeRanges(std::vector<std::pair<uint32_t, uint32_t>> ranges);

// Reads 'size' bytes at 'address' (big-endian, as memory holds them). False, with 'error', when
// they cannot be read.
using ExportMemoryReader = std::function<bool(uint32_t address, uint32_t size, std::vector<uint8_t>& out, std::string& error)>;

// The disassembly listing: a header, then for each merged range one line per instruction
// ("06010468  D102  mov.l   @(0x06010474),r1"), with the user's note from SH-2 Assembly after
// it when one is set at that address. A gap between ranges (a literal pool, data, code the flow
// never reaches) is one comment line naming it. False, with 'error' naming the range, when memory
// cannot be read; 'text' is then left empty.
bool FormatSh2FunctionListing(const FunctionExportInfo& info, const std::vector<std::pair<uint32_t, uint32_t>>& ranges,
                              const ExportMemoryReader& read, const std::unordered_map<uint32_t, std::string>* notes,
                              std::string& text, std::string& error);

// The pseudocode file: a header comment, then what Copy Function copies (the panel's warning
// marks and the code), ending in a newline.
std::string FormatDecompiledExport(const FunctionExportInfo& info, const std::string& functionText);

// The suggested file name: the function's name with anything but letters, digits, '_', '-' and
// '.' replaced by '_', then ".s" for the listing or ".c" for the pseudocode.
std::string FunctionExportFileName(const FunctionExportInfo& info, FunctionExportKind kind);

}  // namespace sfe
