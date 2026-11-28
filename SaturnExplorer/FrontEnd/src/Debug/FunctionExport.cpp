#include "Debug/FunctionExport.h"

#include "Debug/Sh2Disasm.h"

#include <algorithm>
#include <cstdio>

namespace sfe
{
namespace
{
std::string Hex8(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof b, "%08X", v);
    return b;
}

// The name the label, the header and the file use. FunctionNames' default spelling, so the export
// names an unnamed function as the Call Stack and the Profiler do.
std::string DisplayName(const FunctionExportInfo& info)
{
    if (!info.name.empty()) return info.name;
    char b[16];
    std::snprintf(b, sizeof b, "sub_%06X", info.entry & 0x00FFFFFFu);
    return b;
}

// "sub_010468 -- master SH-2, entry 06010468" and "memory: live, paused, frame 34794": the two
// header lines both files share, without their comment markers.
std::vector<std::string> HeaderLines(const FunctionExportInfo& info)
{
    std::string first = DisplayName(info) + " -- " + (info.cpu ? "slave" : "master") + " SH-2, entry " + Hex8(info.entry);
    std::string second = "memory: " + (info.source.empty() ? std::string("unknown source") : info.source);
    if (info.frame) second += ", frame " + std::to_string(static_cast<unsigned long long>(info.frame));
    return { first, second };
}
}  // namespace

const char* FunctionExportLabel(FunctionExportKind kind)
{
    return kind == FunctionExportKind::Pseudocode ? "Export Decompiled Code" : "Export SH-2 Function";
}

std::string CCommentLine(std::string text)
{
    for (char& c : text)
        if (c == '\n' || c == '\r') c = ' ';
    for (size_t at; (at = text.find("*/")) != std::string::npos;) text.replace(at, 2, "* /");   // a comment cannot hold its own end
    return "/* " + text + " */\n";
}

std::vector<std::pair<uint32_t, uint32_t>> MergeCodeRanges(std::vector<std::pair<uint32_t, uint32_t>> ranges)
{
    ranges.erase(std::remove_if(ranges.begin(), ranges.end(),
                                [](const std::pair<uint32_t, uint32_t>& r) { return r.second < r.first; }),
                 ranges.end());
    std::sort(ranges.begin(), ranges.end());
    std::vector<std::pair<uint32_t, uint32_t>> out;
    for (const auto& r : ranges)
    {
        // Touching: the next block starts at the instruction after this one's last.
        if (!out.empty() && static_cast<uint64_t>(r.first) <= static_cast<uint64_t>(out.back().second) + 2)
            out.back().second = (std::max)(out.back().second, r.second);
        else
            out.push_back(r);
    }
    return out;
}

bool FormatSh2FunctionListing(const FunctionExportInfo& info, const std::vector<std::pair<uint32_t, uint32_t>>& ranges,
                              const ExportMemoryReader& read, const std::unordered_map<uint32_t, std::string>* notes,
                              std::string& text, std::string& error)
{
    text.clear();
    const std::vector<std::pair<uint32_t, uint32_t>> runs = MergeCodeRanges(ranges);
    if (runs.empty())
    {
        error = "the decompiler reported no instructions for this function";
        return false;
    }
    std::string out;
    for (const std::string& h : HeaderLines(info)) out += "; " + h + "\n";
    out += "; The instructions the decompiler's flow reaches; a gap is a literal pool, data or code it never reaches.\n\n";
    out += DisplayName(info) + ":\n";

    for (size_t i = 0; i < runs.size(); ++i)
    {
        const uint32_t lo = runs[i].first, hi = runs[i].second;
        if (i > 0)
            out += "        ; " + Hex8(runs[i - 1].second + 2) + "-" + Hex8(lo - 1) +
                   ": not reached by the function's flow (literal pool, data or unreached code)\n";
        const uint32_t size = hi - lo + 2;
        std::vector<uint8_t> bytes;
        std::string why;
        if (!read(lo, size, bytes, why) || bytes.size() < size)
        {
            error = "memory at " + Hex8(lo) + "-" + Hex8(hi + 1) + " cannot be read" + (why.empty() ? std::string() : ": " + why);
            return false;
        }
        // The two bytes before the run say whether its first instruction sits in a delay slot (Sh2DecodeWindow).
        std::vector<uint8_t> prev;
        std::string ignored;
        const bool havePrev = lo >= 2 && read(lo - 2, 2, prev, ignored) && prev.size() >= 2;
        const std::vector<Sh2WindowLine> lines = Sh2DecodeWindow(lo, bytes.data(), bytes.size(),
                                                                 static_cast<int>(size / 2), havePrev ? prev.data() : nullptr);
        for (const Sh2WindowLine& w : lines)
        {
            char row[96];
            std::snprintf(row, sizeof row, "    %08X  %04X  %-8s %s", w.addr, w.op, w.ins.Mnemonic.c_str(), w.ins.Operands.c_str());
            std::string line = row;
            if (notes)
            {
                const auto it = notes->find(w.addr);
                if (it != notes->end() && !it->second.empty()) line += "  ; " + it->second;
            }
            while (!line.empty() && line.back() == ' ') line.pop_back();
            out += line;
            out += '\n';
        }
    }
    text = std::move(out);
    return true;
}

std::string FormatDecompiledExport(const FunctionExportInfo& info, const std::string& functionText)
{
    std::string out;
    for (const std::string& h : HeaderLines(info)) out += CCommentLine(h);
    out += functionText;
    if (out.empty() || out.back() != '\n') out += '\n';
    return out;
}

std::string FunctionExportFileName(const FunctionExportInfo& info, FunctionExportKind kind)
{
    std::string base = DisplayName(info);
    for (char& c : base)
    {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!ok) c = '_';
    }
    return base + (kind == FunctionExportKind::Pseudocode ? ".c" : ".s");
}

}  // namespace sfe
