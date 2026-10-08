#include "Sh2Dump.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>

#include "SaturnRegions.h"   // kWramSize -- one home for the Saturn region sizes

namespace sfe
{

namespace
{
// Sizes from SaturnRegions.h rather than spelled again here: that header exists so every memory
// consumer agrees on one set of hardware constants.
const Sh2DumpRegion kRegions[kSh2DumpRegionCount] = {
    { "LWRAM", "Low work RAM",  0x00200000u, kWramSize },
    { "HWRAM", "High work RAM", 0x06000000u, kWramSize },
};

// Rows decoded per block. A block decodes as the panel's pages do: with the opcode before it, so
// its first row is judged after a delayed branch exactly as it would be inside one run.
constexpr uint32_t kBlockRows = 1024;

// Column where a comment starts when the instruction text is shorter, so the ";" line up.
constexpr size_t kCommentColumn = 28;

// Fixed-width uppercase hex, straight into the buffer. Not Appendf: vsnprintf interprets a
// format string and copies out of a stack buffer, which measured at ~47 ns a call against ~4 ns
// here -- and this runs twice per row, a million rows per dump.
void AppendHex(std::string& out, uint32_t value, int digits)
{
    for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4)
        out += "0123456789ABCDEF"[(value >> shift) & 0xFu];
}

void Appendf(std::string& out, const char* fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    const int n = std::vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    if (n > 0) out.append(buf, std::min<size_t>(static_cast<size_t>(n), sizeof buf - 1));
}
}  // namespace

const Sh2DumpRegion& Sh2DumpRegionAt(size_t index) { return kRegions[index]; }

Sh2DumpJob::Sh2DumpJob(Sh2DumpInput input) : mIn(std::move(input))
{
    // Counted the same way Step will emit them, so Progress() cannot promise rows that are not
    // coming (a short memory vector, reachable through this public input even if not through App).
    for (size_t i = 0; i < kSh2DumpRegionCount; ++i) mTotal += RowsIn(i);
    mText.reserve(static_cast<size_t>(std::min<uint64_t>(EstimateBytes(mIn.opt, mTotal), 1ull << 30)));
    mLine.reserve(128);
    mAutoComments = mIn.opt.comments && mIn.opt.autoComments && mIn.haveRegs;
    // Built once rather than per row. Captures only 'this'; mIn is owned and does not move again.
    mReadMem = [this](uint32_t a, uint32_t n, uint32_t& out)
    {
        const uint32_t c = a & 0x07FFFFFFu;
        for (size_t i = 0; i < kSh2DumpRegionCount; ++i)
        {
            const Sh2DumpRegion& r = kRegions[i];
            const std::vector<uint8_t>& m = mIn.memory[i];
            if (c < r.base || c - r.base + n > m.size()) continue;
            out = 0;
            for (uint32_t k = 0; k < n; ++k) out = (out << 8) | m[c - r.base + k];
            return true;
        }
        return false;
    };
}

uint32_t Sh2DumpJob::RowsIn(size_t i) const
{
    if (!mIn.opt.regions[i]) return 0;
    return static_cast<uint32_t>(std::min<size_t>(kRegions[i].size, mIn.memory[i].size()) / 2);
}

uint64_t Sh2DumpJob::EstimateInstructions(const Sh2DumpOptions& opt, const bool available[kSh2DumpRegionCount])
{
    uint64_t n = 0;
    for (size_t i = 0; i < kSh2DumpRegionCount; ++i)
        if (opt.regions[i] && available[i]) n += kRegions[i].size / 2;
    return n;
}

uint64_t Sh2DumpJob::EstimateBytes(const Sh2DumpOptions& opt, uint64_t instructions)
{
    // A row is its columns plus separators and a newline; comments are the unknown, so allow a
    // typical one. Only for sizing a buffer and telling the user roughly what they will get.
    uint64_t row = 1;
    if (opt.addresses) row += 10;
    if (opt.bytes) row += 6;
    if (opt.opcodes) row += 24;
    if (opt.comments) row += 12;
    return instructions * row;
}

std::string Sh2DumpJob::CommentFor(uint32_t addr, bool readable,
                                   const DisassembledInstruction& ins) const
{
    // A user note replaces the generated one, as in the panel -- and is looked up first, so a
    // row that has one never pays for a generated comment it would discard.
    const auto it = mIn.userComments.find(addr);
    if (it != mIn.userComments.end() && !it->second.empty()) return it->second;
    if (mAutoComments && readable) return Sh2Comment(ins, mIn.regs, mReadMem);
    return std::string();
}

void Sh2DumpJob::EmitRow(uint32_t addr, uint16_t op, bool readable, const DisassembledInstruction& ins)
{
    const Sh2DumpOptions& o = mIn.opt;
    std::string& line = mLine;
    line.clear();   // keeps the capacity: no allocation after the first row
    auto sep = [&line] { if (!line.empty()) line += "  "; };

    if (o.addresses)
    {
        sep();   // a no-op while this is the first column, and not a trap if one is added before it
        AppendHex(line, addr, 8);
    }
    if (o.bytes)
    {
        sep();
        if (readable) AppendHex(line, op, 4); else line += "----";
    }
    if (o.opcodes)
    {
        sep();
        if (readable)
        {
            // Straight into 'line' rather than via a temporary: the column width is measured
            // from where this column started, so the result is identical without the extra
            // allocation and copy.
            const size_t col = line.size();
            line += ins.Mnemonic;
            const size_t width = line.size() - col;
            if (width < 8) line.append(8 - width, ' ');   // the panel's Instr column
            else line += ' ';
            line += ins.Operands;
        }
        else line += "????";
    }
    if (o.comments)
    {
        const std::string note = CommentFor(addr, readable, ins);
        if (!note.empty())
        {
            // Aligned only when other columns precede it; a comments-only dump is just the notes.
            if (!line.empty())
            {
                if (line.size() < kCommentColumn) line.append(kCommentColumn - line.size(), ' ');
                else line += "  ";
                line += "; ";
            }
            line += note;
        }
    }
    // The only trailing blanks possible are the mnemonic's padding on an operand-less instruction.
    while (!line.empty() && line.back() == ' ') line.pop_back();
    mText += line;
    mText += '\n';
}

bool Sh2DumpJob::Step(uint32_t maxInstructions)
{
    if (Done()) return true;
    if (!mHeaderDone)
    {
        mText += "; Saturn Explorer SH-2 disassembly\n";
        Appendf(mText, "; CPU: %s%s\n", mIn.cpu ? "Slave SH-2" : "Master SH-2",
                mAutoComments ? " (generated comments resolved against its registers)" : "");
        for (size_t i = 0; i < kSh2DumpRegionCount; ++i)
            if (mIn.opt.regions[i] && mIn.memory[i].empty())
                Appendf(mText, "; %s: not available from this source, skipped\n", kRegions[i].name);
        mHeaderDone = true;
    }

    uint32_t budget = maxInstructions;
    while (budget > 0)
    {
        if (Done()) return true;

        const Sh2DumpRegion& r = kRegions[mRegion];
        const std::vector<uint8_t>& mem = mIn.memory[mRegion];
        // One test covers every reason to move on: not selected, not readable, or finished.
        // RowsIn is zero for the first two, so they never reach the banner below.
        const uint32_t rows = RowsIn(mRegion);
        if (mRow >= rows) { ++mRegion; mRow = 0; continue; }

        // mRow == 0 is a region's first visit: n is always >= 1 below, so the banner cannot
        // repeat and needs no flag of its own.
        if (mRow == 0)
            Appendf(mText, "; ---- %s  %08X-%08X ----\n", r.name, r.base, r.base + r.size - 1u);

        const uint32_t n = std::min({ kBlockRows, rows - mRow, budget });
        const uint32_t first = mRow * 2;
        const uint8_t* prev = mRow ? &mem[first - 2] : nullptr;
        const std::vector<Sh2WindowLine> block =
            Sh2DecodeWindow(r.base + first, &mem[first], static_cast<size_t>(n) * 2,
                            static_cast<int>(n), prev);
        for (const Sh2WindowLine& l : block) EmitRow(l.addr, l.op, l.readable, l.ins);
        mRow += n;
        mEmitted += n;
        budget -= n;
    }
    return Done();
}

}  // namespace sfe
