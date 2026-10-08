#include "Sh2Dump.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>

#include "Debug/Sh2Disasm.h"
#include "Sh2Operands.h"

namespace sfe
{

namespace
{
const Sh2DumpRegion kRegions[kSh2DumpRegionCount] = {
    { "LWRAM", 0x00200000u, 0x100000u },
    { "HWRAM", 0x06000000u, 0x100000u },
};

// Rows decoded per block. A block decodes as the panel's pages do: with the opcode before it, so
// its first row is judged after a delayed branch exactly as it would be inside one run.
constexpr uint32_t kBlockRows = 1024;

// Column where a comment starts when the instruction text is shorter, so the ";" line up.
constexpr size_t kCommentColumn = 28;

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
    bool available[kSh2DumpRegionCount];
    for (size_t i = 0; i < kSh2DumpRegionCount; ++i) available[i] = !mIn.memory[i].empty();
    mTotal = EstimateInstructions(mIn.opt, available);
    mText.reserve(static_cast<size_t>(std::min<uint64_t>(EstimateBytes(mIn.opt, mTotal), 1ull << 30)));
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

void Sh2DumpJob::BeginRegion()
{
    const Sh2DumpRegion& r = kRegions[mRegion];
    Appendf(mText, "; ---- %s  %08X-%08X ----\n", r.name, r.base, r.base + r.size - 1u);
    mRow = 0;
    mRegionOpen = true;
}

void Sh2DumpJob::EmitRow(uint32_t addr, uint16_t op, bool readable, const DisassembledInstruction& ins)
{
    const Sh2DumpOptions& o = mIn.opt;
    std::string line;
    line.reserve(80);
    auto sep = [&] { if (!line.empty()) line += "  "; };

    if (o.addresses) Appendf(line, "%08X", addr);
    if (o.bytes)
    {
        sep();
        if (readable) Appendf(line, "%04X", op); else line += "----";
    }
    if (o.opcodes)
    {
        sep();
        std::string text;
        if (readable)
        {
            text = ins.Mnemonic;
            if (text.size() < 8) text.append(8 - text.size(), ' ');   // the panel's Instr column
            else text += ' ';
            text += ins.Operands;
        }
        else text = "????";
        line += text;
    }
    if (o.comments)
    {
        // A user note replaces the generated one, as in the panel.
        std::string note;
        const auto it = mIn.userComments.find(addr);
        if (it != mIn.userComments.end() && !it->second.empty()) note = it->second;
        else if (o.autoComments && mIn.haveRegs && readable)
        {
            note = Sh2Comment(ins, mIn.regs,
                [this](uint32_t a, uint32_t n, uint32_t& out)
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
                });
        }
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
    if (mDone) return true;
    if (!mHeaderDone)
    {
        mText += "; Saturn Explorer SH-2 disassembly\n";
        Appendf(mText, "; CPU: %s%s\n", mIn.cpu ? "Slave SH-2" : "Master SH-2",
                mIn.opt.comments && mIn.opt.autoComments && mIn.haveRegs
                    ? " (generated comments resolved against its registers)" : "");
        for (size_t i = 0; i < kSh2DumpRegionCount; ++i)
            if (mIn.opt.regions[i] && mIn.memory[i].empty())
                Appendf(mText, "; %s: not available from this source, skipped\n", kRegions[i].name);
        mHeaderDone = true;
    }

    uint32_t budget = maxInstructions;
    while (budget > 0)
    {
        // Find the next region with something to write.
        while (mRegion < kSh2DumpRegionCount &&
               (!mIn.opt.regions[mRegion] || mIn.memory[mRegion].empty()))
            ++mRegion, mRegionOpen = false;
        if (mRegion >= kSh2DumpRegionCount) { mDone = true; return true; }

        const Sh2DumpRegion& r = kRegions[mRegion];
        const std::vector<uint8_t>& mem = mIn.memory[mRegion];
        if (!mRegionOpen) BeginRegion();

        const uint32_t rows = static_cast<uint32_t>(std::min<size_t>(r.size, mem.size()) / 2);
        if (mRow >= rows) { ++mRegion; mRegionOpen = false; continue; }

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
    return false;
}

}  // namespace sfe
