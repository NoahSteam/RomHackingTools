#include "Decompiler/Sh2Decompiler.h"

#include "Decompiler/SaturnArchitecture.h"   // first among the engine headers (Win32 LoadImage)

#include "Debug/Sh2Disasm.h"

#include "funcdata.hh"
#include "printlanguage.hh"
#include "marshal.hh"
#include "prettyprint.hh"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <new>
#include <set>
#include <sstream>

namespace sfe
{
namespace decomp
{
namespace
{

using Clock = std::chrono::steady_clock;

double MsSince(Clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

std::string Hex8(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", v);
    return b;
}

// Builds an Architecture over 'snapshot'. If init fails before the Architecture took ownership
// of the loader, the loader is ours to delete.
std::unique_ptr<SaturnArchitecture> BuildArchitecture(const std::shared_ptr<const MemorySnapshot>& snapshot,
                                                      uint32_t maxInstructions, std::atomic<bool>* cancel,
                                                      std::ostream& errors)
{
    auto* image = new CaptureLoadImage(snapshot);
    std::unique_ptr<SaturnArchitecture> arch(new SaturnArchitecture(image, &errors));
    try
    {
        arch->Initialise(maxInstructions, cancel);
    }
    catch (...)
    {
        if (arch->loader == nullptr) delete image;
        throw;
    }
    return arch;
}

// What one pass learned from a function's raw p-code, before any action has run.
struct RawScan
{
    std::set<uint32_t> literalWords;   // start of each literal-pool load marked read-only
    uint32_t literalLoads = 0;
    std::vector<std::pair<uint32_t, int>> readonly;   // (address, size) ranges to mark
    bool unsupported = false;
    uint32_t unsupportedAt = 0;
};

// Literal pools and A3. SLEIGH emits "tmp = #addr; Rn = *(ram, tmp)" for mov.w/mov.l
// @(disp,PC), and raw-stage varnodes have no def links yet, so constants are tracked per
// instruction: fold any op whose inputs are known constants, and a LOAD through a known
// constant pointer is reading a literal-pool word. (Only the PC-relative forms produce a
// constant pointer inside one instruction; a load through a register never does.)
RawScan ScanRawPcode(const ghidra::Funcdata& fd, const MemorySnapshot& snap)
{
    RawScan scan;

    // A3: every instruction the flow reached, decoded on our side. A delayed branch followed by
    // a PC-relative load or mova means the spec's address for that load is wrong whenever the
    // branch is taken; that literal is excluded from folding.
    std::set<uint32_t> insns;
    for (auto it = fd.beginOpMain(); it != fd.endOpMain(); ++it)
        insns.insert(static_cast<uint32_t>(it->second->getAddr().getOffset()));
    std::vector<std::pair<uint32_t, uint32_t>> excluded;   // [lo, hi) literal addresses not to fold
    for (uint32_t a : insns)
    {
        uint16_t op = 0;
        if (!snap.ReadU16(a, op)) continue;
        const DisassembledInstruction branch = Sh2Decode(a, op);
        if (!branch.HasDelaySlot) continue;
        uint16_t slotOp = 0;
        if (!snap.ReadU16(a + 2, slotOp)) continue;
        const DisassembledInstruction slot = Sh2DecodeAfterBranch(a + 2, slotOp, branch);
        if (slot.PcRel == Sh2PcRel::None) continue;
        if (!scan.unsupported || a + 2 < scan.unsupportedAt)
        {
            scan.unsupported = true;
            scan.unsupportedAt = a + 2;
        }
        // The address SLEIGH computes (the slot's own PC); the slot-relative one is never loaded.
        const uint32_t size = slot.PcRel == Sh2PcRel::Word ? 2 : 4;
        excluded.emplace_back(slot.PcRelDirectAddress, slot.PcRelDirectAddress + size);
    }
    auto isExcluded = [&](uint32_t lo, uint32_t hi) {
        for (const auto& e : excluded)
            if (lo < e.second && e.first < hi) return true;
        return false;
    };

    std::map<std::pair<ghidra::uintb, ghidra::int4>, ghidra::uintb> consts;
    ghidra::Address cur;
    for (auto it = fd.beginOpMain(); it != fd.endOpMain(); ++it)
    {
        const ghidra::PcodeOp* op = it->second;
        if (op->getAddr() != cur)
        {
            cur = op->getAddr();
            consts.clear();
        }
        auto valueOf = [&](const ghidra::Varnode* vn, ghidra::uintb& v) {
            if (vn->isConstant())
            {
                v = vn->getOffset();
                return true;
            }
            if (vn->getSpace()->getType() != ghidra::IPTR_INTERNAL) return false;
            auto f = consts.find(std::make_pair(vn->getOffset(), vn->getSize()));
            if (f == consts.end()) return false;
            v = f->second;
            return true;
        };
        ghidra::uintb a = 0, b = 0;
        const ghidra::OpCode code = op->code();
        if (code == ghidra::CPUI_LOAD)
        {
            if (valueOf(op->getIn(1), a))
            {
                const uint32_t lo = static_cast<uint32_t>(a);
                const ghidra::int4 n = op->getOut()->getSize();
                if (!isExcluded(lo, lo + n))
                {
                    ++scan.literalLoads;
                    if (scan.literalWords.insert(lo).second) scan.readonly.emplace_back(lo, n);
                }
            }
            continue;
        }
        const ghidra::Varnode* out = op->getOut();
        if (out == nullptr || out->getSpace()->getType() != ghidra::IPTR_INTERNAL) continue;
        try
        {
            if (op->numInput() == 1 && valueOf(op->getIn(0), a))
                consts[std::make_pair(out->getOffset(), out->getSize())] =
                    op->getOpcode()->evaluateUnary(out->getSize(), op->getIn(0)->getSize(), a);
            else if (op->numInput() == 2 && valueOf(op->getIn(0), a) && valueOf(op->getIn(1), b))
                consts[std::make_pair(out->getOffset(), out->getSize())] =
                    op->getOpcode()->evaluateBinary(out->getSize(), op->getIn(0)->getSize(), a, b);
        }
        catch (ghidra::LowlevelError&)
        {
            // An op with no constant semantics (or a division by zero): just not a constant.
        }
    }
    return scan;
}

TokenKind KindForColor(int color)
{
    switch (color)
    {
    case 0: return TokenKind::Keyword;
    case 1: return TokenKind::Comment;
    case 2: return TokenKind::Type;
    case 3: return TokenKind::FuncName;
    case 4: return TokenKind::Variable;
    case 5: return TokenKind::Const;
    case 6: return TokenKind::Param;
    case 7: return TokenKind::Global;
    case 9: return TokenKind::Error;
    default: return TokenKind::Plain;
    }
}

// Turns the emitter's markup into lines of tokens. Each token's opref names the p-code op it
// was printed from; that op's address is the SH-2 instruction it came from. The markup is read
// in Ghidra's packed encoding, not XML: its XML parser drops whitespace-only text, which is every
// space between tokens.
std::vector<Line> LinesFromMarkup(const std::string& packed, const ghidra::Funcdata& fd, const ghidra::AddrSpaceManager* spaces)
{
    std::map<ghidra::uintm, const ghidra::PcodeOp*> ops;
    for (auto it = fd.beginOpAlive(); it != fd.endOpAlive(); ++it) ops[(*it)->getTime()] = *it;
    for (auto it = fd.beginOpDead(); it != fd.endOpDead(); ++it) ops[(*it)->getTime()] = *it;

    std::vector<Line> lines(1);
    ghidra::PackedDecode decoder(spaces);
    std::istringstream in(packed);
    decoder.ingestStream(in);
    const ghidra::uint4 kBreak = ghidra::ELEM_BREAK.getId();
    const ghidra::uint4 kLabel = ghidra::ELEM_LABEL.getId();
    const ghidra::uint4 kFuncName = ghidra::ELEM_FUNCNAME.getId();
    std::function<void()> walk = [&]() {
        const ghidra::uint4 elem = decoder.openElement();
        int color = 8, indent = 0;
        bool haveContent = false;
        std::string content;
        const ghidra::PcodeOp* op = nullptr;
        for (;;)
        {
            const ghidra::uint4 attr = decoder.getNextAttributeId();
            if (attr == 0) break;
            if (attr == ghidra::ATTRIB_COLOR.getId()) color = static_cast<int>(decoder.readUnsignedInteger());
            else if (attr == ghidra::ATTRIB_INDENT.getId()) indent = static_cast<int>(decoder.readSignedInteger());
            else if (attr == ghidra::ATTRIB_CONTENT.getId())
            {
                content = decoder.readString();
                haveContent = true;
            }
            else if (attr == ghidra::ATTRIB_OPREF.getId())
            {
                auto f = ops.find(static_cast<ghidra::uintm>(decoder.readUnsignedInteger()));
                if (f != ops.end()) op = f->second;
            }
        }
        if (elem == kBreak)
        {
            lines.emplace_back();
            lines.back().text.assign(static_cast<size_t>(std::max(indent, 0)), ' ');
        }
        if (haveContent && !content.empty())
        {
            Line& line = lines.back();
            Token t;
            t.begin = static_cast<uint16_t>(std::min<size_t>(line.text.size(), 0xFFFF));
            line.text += content;
            t.end = static_cast<uint16_t>(std::min<size_t>(line.text.size(), 0xFFFF));
            t.kind = elem == kLabel ? TokenKind::Label : elem == kFuncName ? TokenKind::FuncName : KindForColor(color);
            if (op)
            {
                t.addr = static_cast<uint32_t>(op->getAddr().getOffset());
                if (t.kind == TokenKind::FuncName)
                {
                    const ghidra::FuncCallSpecs* cs = fd.getCallSpecs(op);
                    if (cs && !cs->getEntryAddress().isInvalid())
                        t.target = static_cast<uint32_t>(cs->getEntryAddress().getOffset());
                }
            }
            line.tokens.push_back(t);
        }
        while (decoder.peekElement() != 0) walk();
        decoder.closeElement(elem);
    };
    while (decoder.peekElement() != 0) walk();

    std::vector<Line> out;
    for (Line& l : lines)
    {
        const bool blank = l.text.find_first_not_of(' ') == std::string::npos;
        if (blank && out.empty()) continue;   // the document's leading break
        if (blank) l.text.clear();
        for (const Token& t : l.tokens)
            if (t.addr && (l.firstAddr == 0 || t.addr < l.firstAddr)) l.firstAddr = t.addr;
        out.push_back(std::move(l));
    }
    while (!out.empty() && out.back().text.empty()) out.pop_back();
    return out;
}

struct AsmLines : public ghidra::AssemblyEmit
{
    std::vector<std::string> lines;
    void dump(const ghidra::Address& addr, const std::string& mnem, const std::string& body) override
    {
        char b[16];
        std::snprintf(b, sizeof b, "%X", static_cast<unsigned>(addr.getOffset()));
        lines.push_back(std::string(b) + "  " + mnem + " " + body);
    }
};

}  // namespace

const char* PhaseName(DecompilePhase phase)
{
    switch (phase)
    {
    case DecompilePhase::Idle: return "idle";
    case DecompilePhase::Init: return "init";
    case DecompilePhase::Flow: return "followFlow";
    case DecompilePhase::Actions: return "actions";
    case DecompilePhase::Print: return "print";
    case DecompilePhase::Teardown: return "teardown";
    }
    return "?";
}

void Sh2Decompiler::Enter(DecompilePhase phase)
{
    if (phase != DecompilePhase::Teardown) mWorkPhase = phase;
    if (mObserver) mObserver(phase);
}

std::string DecompileResult::Text() const
{
    std::string s;
    for (const Line& l : lines)
    {
        s += l.text;
        s += '\n';
    }
    return s;
}

Sh2Decompiler::Sh2Decompiler(std::string configDir) : mConfigDir(std::move(configDir))
{
}

DecompileResult Sh2Decompiler::Decompile(const std::shared_ptr<const MemorySnapshot>& snapshot,
                                         const DecompileRequest& request, std::atomic<bool>* cancel)
{
    const Clock::time_point t0 = Clock::now();
    DecompileResult r;
    r.key = snapshot->key;
    r.requestedEntry = request.entry;

    std::string why;
    uint32_t entry = 0;
    uint8_t probe[2];
    if (!CanonicalAddress(request.entry, entry, &why) || !snapshot->Read(entry, probe, 2, &why))
    {
        r.status = DecompileStatus::NoCode;
        r.entry = entry;
        r.error = "no code at " + Hex8(request.entry) + ": " + why;
        r.elapsedMs = MsSince(t0);
        return r;
    }
    r.entry = entry;

    if (!SaturnArchitecture::StartLibrary(mConfigDir, r.error))
    {
        r.status = DecompileStatus::Unavailable;
        r.elapsedMs = MsSince(t0);
        return r;
    }

    // Names first, so a FunctionNames rename shows; then the other known entries.
    std::map<uint32_t, std::string> named;
    for (const auto& n : request.names)
    {
        uint32_t c = 0;
        if (!n.second.empty() && CanonicalAddress(n.first, c)) named[c] = n.second;
    }
    std::set<uint32_t> known;
    for (uint32_t k : request.knownFunctions)
    {
        uint32_t c = 0;
        if (CanonicalAddress(k, c)) known.insert(c);
    }

    auto checkCancel = [&] {
        if (cancel && cancel->load()) throw ghidra::CancelError("cancelled");
    };

    // One pass: a fresh Architecture, every function registered, flow followed, read-only
    // literals applied, actions run. 'print' adds the markup output.
    auto runPass = [&](const std::set<uint32_t>& callees, bool print, std::set<uint32_t>* targets) {
        Enter(DecompilePhase::Init);
        std::ostringstream errors;
        std::unique_ptr<SaturnArchitecture> arch = BuildArchitecture(snapshot, request.maxInstructions, cancel, errors);
        // Declared after the Architecture so it is destroyed first: Teardown is reported as the
        // pass's Architecture starts to go, whichever way the pass ends.
        struct TeardownNote
        {
            Sh2Decompiler* self;
            ~TeardownNote() { self->Enter(DecompilePhase::Teardown); }
        } teardownNote{ this };
        ghidra::AddrSpace* ram = arch->getDefaultCodeSpace();
        ghidra::Scope* global = arch->symboltab->getGlobalScope();

        std::set<uint32_t> all(known);
        all.insert(callees.begin(), callees.end());
        for (const auto& n : named) all.insert(n.first);
        all.insert(entry);
        ghidra::Funcdata* fd = nullptr;
        for (uint32_t a : all)
        {
            const ghidra::Address addr(ram, a);
            std::string name;
            auto f = named.find(a);
            if (f != named.end()) name = f->second;
            else arch->nameFunction(addr, name);
            try
            {
                ghidra::Funcdata* f2 = global->addFunction(addr, name)->getFunction();
                if (a == entry) fd = f2;
            }
            catch (ghidra::LowlevelError& e)
            {
                // A clashing user name, say. The function is still decompiled, under the
                // engine's own name if need be.
                r.warnings.push_back("could not register " + Hex8(a) + " as \"" + name + "\": " + e.explain);
                if (a == entry)
                {
                    arch->nameFunction(addr, name);
                    fd = global->addFunction(addr, name)->getFunction();
                }
            }
        }

        Enter(DecompilePhase::Flow);
        fd->followFlow(ghidra::Address(ram, 0), ghidra::Address(ram, ram->getHighest()));
        if (fd->hasNoCode()) return std::string();

        const RawScan scan = ScanRawPcode(*fd, *snapshot);
        for (const auto& ro : scan.readonly)
            arch->symboltab->setPropertyRange(ghidra::Varnode::readonly,
                                              ghidra::Range(ram, ro.first, ro.first + ro.second - 1));

        // A check-point of ours at each phase boundary: the engine's last one in a phase can come
        // well before the phase ends (block generation after flow, the tail of the action tree),
        // and our own raw-p-code scan has none. A cancel requested anywhere in a phase is then
        // honoured in that phase, never carried into the next one's work.
        checkCancel();
        Enter(DecompilePhase::Actions);
        arch->allacts.getCurrent()->reset(*fd);
        arch->allacts.getCurrent()->perform(*fd);

        if (targets)
        {
            // Direct calls (bsr) carry their target; a jsr @rN through a literal pool becomes a
            // CALLIND on a constant once the literal is folded -- the engine leaves it indirect,
            // which is why the second pass registers it.
            for (auto it = fd->beginOpAlive(); it != fd->endOpAlive(); ++it)
            {
                const ghidra::PcodeOp* op = *it;
                const ghidra::Varnode* in0 = op->numInput() > 0 ? op->getIn(0) : nullptr;
                uint64_t t = 0;
                if (op->code() == ghidra::CPUI_CALL && in0 && in0->getSpace() == ram) t = in0->getOffset();
                else if (op->code() == ghidra::CPUI_CALLIND && in0 && in0->isConstant()) t = in0->getOffset();
                else continue;
                // Only targets whose code was captured are registered: a call into the BIOS
                // (never captured) stays an opaque pointer rather than naming a function nothing
                // can show.
                uint32_t c = 0;
                uint8_t code[2];
                if (CanonicalAddress(static_cast<uint32_t>(t), c) && snapshot->Read(c, code, 2)) targets->insert(c);
            }
        }
        if (!print) return std::string();

        checkCancel();
        Enter(DecompilePhase::Print);
        r.name = fd->getName();
        r.bodyBytes = static_cast<uint32_t>(fd->getSize());
        r.unimplemented = fd->hasUnimplemented();
        r.unsupported = scan.unsupported;
        r.unsupportedAt = scan.unsupportedAt;
        const CaptureLoadImage* image = arch->Image();
        r.truncated = fd->hasBadData() && image->HadUnavailable();
        r.truncatedAt = image->UnavailableAddress();
        r.truncatedWhy = image->UnavailableWhy();
        r.literalPools.assign(scan.literalWords.begin(), scan.literalWords.end());
        r.literalLoads = scan.literalLoads;
        if (scan.unsupported)
            r.warnings.push_back("PC-relative load in a delay slot at " + Hex8(scan.unsupportedAt) +
                                 ": pseudocode for this function is not trustworthy");
        if (r.truncated)
            r.warnings.push_back("flows into " + r.truncatedWhy);
        if (r.unimplemented) r.warnings.push_back("the SH-2 spec has no semantics for an instruction here");

        std::ostringstream markup;
        arch->print->setOutputStream(&markup);
        arch->print->setMarkup(true);
        arch->print->setPackedOutput(true);
        arch->print->docFunction(fd);
        arch->print->setMarkup(false);
        r.lines = LinesFromMarkup(markup.str(), *fd, arch.get());
        const std::string e = errors.str();
        if (!e.empty()) r.warnings.push_back(e);
        return std::string("ok");
    };

    try
    {
        std::set<uint32_t> callees;
        std::string printed;
        if (request.registerCallees)
        {
            runPass(std::set<uint32_t>(), false, &callees);
            printed = runPass(callees, true, nullptr);
        }
        else
        {
            printed = runPass(std::set<uint32_t>(), true, &callees);
        }
        if (printed.empty())
        {
            r.status = DecompileStatus::NoCode;
            r.error = "no code at " + Hex8(entry);
        }
        else
        {
            r.status = DecompileStatus::Ok;
            r.callees.assign(callees.begin(), callees.end());
        }
    }
    catch (ghidra::CancelError&)
    {
        r.status = DecompileStatus::Cancelled;
        r.error = "cancelled";
        r.cancelledIn = mWorkPhase;
        r.lines.clear();
    }
    catch (ghidra::LowlevelError& e)
    {
        r.status = DecompileStatus::Failed;
        r.error = e.explain;
        r.lines.clear();
    }
    catch (std::bad_alloc&)
    {
        r.status = DecompileStatus::Failed;
        r.error = "out of memory";
        r.lines.clear();
    }
    mWorkPhase = DecompilePhase::Idle;
    r.elapsedMs = MsSince(t0);
    return r;
}

std::vector<std::string> Sh2Decompiler::Disassemble(const std::shared_ptr<const MemorySnapshot>& snapshot,
                                                    uint32_t address, int count)
{
    std::string error;
    if (!SaturnArchitecture::StartLibrary(mConfigDir, error)) return std::vector<std::string>{ "<" + error + ">" };
    AsmLines em;
    try
    {
        std::ostringstream errors;
        std::unique_ptr<SaturnArchitecture> arch = BuildArchitecture(snapshot, 20000, nullptr, errors);
        ghidra::Address a(arch->getDefaultCodeSpace(), address);
        for (int i = 0; i < count; ++i)
        {
            const ghidra::int4 len = arch->translate->printAssembly(em, a);
            a = a + len;
        }
    }
    catch (ghidra::LowlevelError& e)
    {
        em.lines.push_back("<" + e.explain + ">");
    }
    return em.lines;
}

}  // namespace decomp
}  // namespace sfe
