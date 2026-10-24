// Captured inspection bindings are checked against hand-assembled SH-2 instructions, not
// pseudocode names. The metadata survives destruction of every Ghidra object on return.
#include "DecompilerTestSupport.h"
#include "Decompiler/Sh2Decompiler.h"
#include "Decompiler/InspectionBinding.h"
#include "Decompiler/SaturnArchitecture.h"
#include "funcdata.hh"
#include "variable.hh"
#include "varmap.hh"
#include "database.hh"
#include "prettyprint.hh"
#include <sstream>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace sfe::decomp;

namespace
{
constexpr uint32_t kEntry = 0x06004000u;
constexpr uint32_t kGlobal = 0x06010000u;

std::shared_ptr<MemorySnapshot> Snapshot(std::initializer_list<uint16_t> words,
                                         uint32_t literal = kGlobal)
{
    std::vector<uint8_t> hw(0x100000u, 0);
    size_t at = kEntry - MemorySnapshot::kHwramBase;
    for (uint16_t word : words) { dts::Put16(hw, at, word); at += 2; }
    dts::Put32(hw, 0x4008, literal);
    dts::Put32(hw, kGlobal - MemorySnapshot::kHwramBase, 0x06020000u);
    dts::Put32(hw, 0x20000, 0x12345678u);
    return dts::MakeSnapshot(hw);
}

DecompileRequest Request()
{
    DecompileRequest q;
    q.entry = kEntry;
    q.registerCallees = false;
    return q;
}

void Dump(const char* label, const DecompileResult& r)
{
    std::printf("%s: status %d\n%s\n", label, int(r.status), r.Text().c_str());
    if (!r.error.empty()) std::printf("error: %s\n", r.error.c_str());
    for (const Line& line : r.lines)
        for (const Token& token : line.tokens)
            if (token.inspectionTarget && token.inspectionTarget <= r.inspectionTargets.size())
            {
                const InspectionTarget& t = r.inspectionTargets[token.inspectionTarget - 1];
                std::printf("  [%.*s] -> kind %d address %08X width %u display %d\n",
                            int(token.end - token.begin), line.text.c_str() + token.begin,
                            int(t.kind), t.address, unsigned(t.byteSize), int(t.display));
            }
}

void CheckReferences(const DecompileResult& r)
{
    for (const Line& line : r.lines)
        for (const Token& token : line.tokens)
        {
            CHECK(token.inspectionTarget <= r.inspectionTargets.size());
            if (!token.inspectionTarget) continue;
            CHECK(token.begin < token.end && token.end <= line.text.size());
            CHECK(token.kind != TokenKind::Param && token.kind != TokenKind::FuncName);
        }
}

bool BoundObject(const DecompileResult& r, uint32_t address, uint32_t width,
                 uint32_t originatingInstruction)
{
    for (const Line& line : r.lines)
        for (const Token& token : line.tokens)
        {
            if (!token.inspectionTarget || token.inspectionTarget > r.inspectionTargets.size()) continue;
            const InspectionTarget& target = r.inspectionTargets[token.inspectionTarget - 1];
            if (target.kind == InspectionTarget::Kind::AbsoluteObject && target.address == address &&
                target.byteSize == width && token.addr == originatingInstruction)
                return true;
        }
    return false;
}

void TestScalarLoadsAndStores(Sh2Decompiler& d)
{
    for (bool userVolatile : { false, true })
    for (uint16_t width = 0; width != 3; ++width)
    {
        const uint32_t bytes = 1u << width;
        DecompileRequest q = Request();
        if (userVolatile) q.volatileMemory.emplace_back(kGlobal, kGlobal + bytes - 1);
        // mov.l @(4,pc),r1; mov.[b/w/l] @r1,r0; rts; nop; .long kGlobal
        const auto load = Snapshot({ 0xD101, uint16_t(0x6010 | width), 0x000B, 0x0009 });
        DecompileResult read = d.Decompile(load, q);
        Dump(userVolatile ? "volatile scalar load" : "absolute scalar load", read);
        CHECK(read.status == DecompileStatus::Ok);
        CheckReferences(read);
        CHECK(BoundObject(read, kGlobal, bytes, kEntry + 2));
        // An engine "undefined" scalar is shown as an untyped hex word, never a guessed integer.
        if (!userVolatile && bytes == 4)
        {
            bool untyped = false;
            for (const auto& t : read.inspectionTargets)
                untyped = untyped || (t.address == kGlobal && t.display == InspectionTarget::Display::Untyped);
            CHECK(untyped);
        }
        // mov.l @(4,pc),r1; mov.[b/w/l] r4,@r1; rts; nop; .long kGlobal
        const auto store = Snapshot({ 0xD101, uint16_t(0x2140 | width), 0x000B, 0x0009 });
        DecompileResult write = d.Decompile(store, q);
        Dump(userVolatile ? "volatile scalar store" : "absolute scalar store", write);
        CHECK(write.status == DecompileStatus::Ok);
        CheckReferences(write);
        CHECK(BoundObject(write, kGlobal, bytes, kEntry + 2));
        // Values captured at this address are deliberately unrelated to the store argument:
        // descriptors describe storage, never the value produced by this source occurrence.
        CHECK(read.key == load->key && write.key == store->key);
    }
}

void TestPointerGlobal(Sh2Decompiler& d)
{
    // mov.l @(8,pc),r1; mov.l @r1,r1; mov.l @r1,r0; rts; nop; nop; .long kGlobal
    std::vector<uint8_t> hw(0x100000u, 0);
    const uint16_t words[] = { 0xD102, 0x6112, 0x6012, 0x000B, 0x0009, 0x0009 };
    for (size_t i = 0; i < sizeof words / sizeof *words; ++i) dts::Put16(hw, 0x4000 + i * 2, words[i]);
    dts::Put32(hw, 0x400C, kGlobal);
    dts::Put32(hw, 0x10000, 0x06020000u);
    dts::Put32(hw, 0x20000, 0x12345678u);
    auto snap = dts::MakeSnapshot(hw);
    const DecompileResult r = d.Decompile(snap, Request());
    Dump("pointer global", r);
    CHECK(r.status == DecompileStatus::Ok);
    CheckReferences(r);
    bool pointer = false;
    for (const auto& target : r.inspectionTargets)
        if (target.kind == InspectionTarget::Kind::AbsoluteObject && target.address == kGlobal &&
            target.byteSize == 4 && target.display == InspectionTarget::Display::Pointer)
            pointer = true;
    CHECK(pointer);
}

void TestPointerArgumentAddressForm(Sh2Decompiler& d)
{
    // A real callee establishes a typed pointer parameter; its caller passes a literal
    // address. Depending on printer context this is an address token or &global, never
    // the global's stored value. Keep the callee in the image and lend its real prototype.
    constexpr uint32_t callee = 0x06005000u;
    std::vector<uint8_t> hw(0x100000u, 0);
    const uint16_t caller[] = { 0x4F22, 0xD403, 0xB7FC, 0x0009, 0x4F26, 0x000B, 0x0009, 0x0009 };
    for (size_t i = 0; i < sizeof caller / sizeof *caller; ++i) dts::Put16(hw, 0x4000 + i * 2, caller[i]);
    dts::Put32(hw, 0x4010, kGlobal);
    dts::Put16(hw, 0x5000, 0x6042); // mov.l @r4,r0
    dts::Put16(hw, 0x5002, 0x000B); // rts
    dts::Put16(hw, 0x5004, 0x0009); // nop
    auto snap = dts::MakeSnapshot(hw);
    auto q = Request(); q.entry = callee;
    const DecompileResult c = d.Decompile(snap, q);
    CHECK(c.status == DecompileStatus::Ok);
    CHECK(c.prototype.known && c.prototype.params.size() == 1);
    if (!c.prototype.known || c.prototype.params.size() != 1) return;
    CHECK(c.prototype.params[0].kind == TypeKind::Pointer);
    q = Request();
    q.knownFunctions.push_back(callee);
    q.prototypes.emplace_back(callee, c.prototype);
    const DecompileResult r = d.Decompile(snap, q);
    Dump("typed pointer argument/address form", r);
    CHECK(r.status == DecompileStatus::Ok);
    CheckReferences(r);
    bool address = false;
    for (const auto& target : r.inspectionTargets)
        address = address || (target.kind == InspectionTarget::Kind::ConstantAddress && target.address == kGlobal);
    CHECK(address);
}

// The text of every bound token with the given kind and address, as the real printer emitted it.
std::vector<std::string> BoundTexts(const DecompileResult& r, InspectionTarget::Kind kind, uint32_t address)
{
    std::vector<std::string> out;
    for (const Line& line : r.lines)
        for (const Token& token : line.tokens)
        {
            if (!token.inspectionTarget || token.inspectionTarget > r.inspectionTargets.size()) continue;
            const InspectionTarget& t = r.inspectionTargets[token.inspectionTarget - 1];
            if (t.kind == kind && t.address == address)
                out.push_back(line.text.substr(token.begin, token.end - token.begin));
        }
    return out;
}

// The engine registers the Saturn's hardware registers as named global symbols, so real code
// reaches the printer's symbol paths: the token must be the register's name, and the metadata
// must name the same object.
void TestRealPrinterNamedGlobals(Sh2Decompiler& d)
{
    constexpr uint32_t kTvmd = 0x25F80000u;
    // mov.l @(4,pc),r1; mov.w @r1,r0; rts; nop; .long VDP2_TVMD
    DecompileResult load = d.Decompile(Snapshot({ 0xD101, 0x6011, 0x000B, 0x0009 }, kTvmd), Request());
    Dump("real printer named register load", load);
    CHECK(load.status == DecompileStatus::Ok);
    CheckReferences(load);
    const auto loadTexts = BoundTexts(load, InspectionTarget::Kind::AbsoluteObject, kTvmd);
    CHECK(loadTexts.size() == 1 && !loadTexts.empty() && loadTexts[0] == "VDP2_TVMD");
    // mov.l @(4,pc),r1; mov.w r4,@r1; rts; nop; .long VDP2_TVMD
    DecompileResult store = d.Decompile(Snapshot({ 0xD101, 0x2141, 0x000B, 0x0009 }, kTvmd), Request());
    Dump("real printer named register store", store);
    CHECK(store.status == DecompileStatus::Ok);
    CheckReferences(store);
    const auto storeTexts = BoundTexts(store, InspectionTarget::Kind::AbsoluteObject, kTvmd);
    CHECK(storeTexts.size() == 1 && !storeTexts.empty() && storeTexts[0] == "VDP2_TVMD");
    for (const DecompileResult* r : { &load, &store })
        for (const auto& t : r->inspectionTargets)
            if (t.address == kTvmd)
                CHECK(t.byteSize == 2 && t.display == InspectionTarget::Display::UnsignedInteger);
}

// A named register passed where a pointer is expected prints as &name; that is an address, and
// must never be offered as the register's value. (The engine registers only volatile hardware
// symbols, so an implied load or store through the same spacebase PTRSUB form needs the native
// fixtures below: a volatile access prints through the annotation path instead.)
void TestRealPrinterAddressOfNamedGlobal(Sh2Decompiler& d)
{
    constexpr uint32_t callee = 0x06005000u, kTvmd = 0x25F80000u;
    std::vector<uint8_t> hw(0x100000u, 0);
    const uint16_t caller[] = { 0x4F22, 0xD403, 0xB7FC, 0x0009, 0x4F26, 0x000B, 0x0009, 0x0009 };
    for (size_t i = 0; i < sizeof caller / sizeof *caller; ++i) dts::Put16(hw, 0x4000 + i * 2, caller[i]);
    dts::Put32(hw, 0x4010, kTvmd);
    dts::Put16(hw, 0x5000, 0x6042); // mov.l @r4,r0
    dts::Put16(hw, 0x5002, 0x000B);
    dts::Put16(hw, 0x5004, 0x0009);
    auto snap = dts::MakeSnapshot(hw);
    auto q = Request(); q.entry = callee;
    const DecompileResult c = d.Decompile(snap, q);
    CHECK(c.prototype.known && c.prototype.params.size() == 1);
    if (!c.prototype.known || c.prototype.params.size() != 1) return;
    q = Request();
    q.knownFunctions.push_back(callee);
    q.prototypes.emplace_back(callee, c.prototype);
    const DecompileResult r = d.Decompile(snap, q);
    Dump("real printer address of named register", r);
    CHECK(r.status == DecompileStatus::Ok);
    CheckReferences(r);
    // The real printer emits "(T *)&VDP2_TVMD": the token is the register's name, the binding is
    // its address, and nothing offers the register's stored value for this occurrence.
    const auto address = BoundTexts(r, InspectionTarget::Kind::ConstantAddress, kTvmd);
    CHECK(address.size() == 1 && !address.empty() && address[0] == "VDP2_TVMD");
    CHECK(BoundTexts(r, InspectionTarget::Kind::AbsoluteObject, kTvmd).empty());
    bool amp = false;
    for (const Line& line : r.lines)
        for (const Token& token : line.tokens)
            if (token.inspectionTarget && token.begin > 0 && line.text[token.begin - 1] == '&') amp = true;
    CHECK(amp);
    for (const auto& t : r.inspectionTargets)
        if (t.address == kTvmd)
            CHECK(t.typeLabel.size() > 2 && t.typeLabel.compare(t.typeLabel.size() - 2, 2, " *") == 0);
}

void TestGlobalArrayBaseStaysUnbound(Sh2Decompiler& d)
{
    // mov.l @(8,pc),r1; add r4,r1; mov.l @r1,r0; rts; nop; nop; .long kGlobal
    // The emitted base of an indexed access does not identify the accessed object.
    std::vector<uint8_t> hw(0x100000u, 0);
    const uint16_t words[] = { 0xD102, 0x314C, 0x6012, 0x000B, 0x0009, 0x0009 };
    for (size_t i = 0; i < sizeof words / sizeof *words; ++i) dts::Put16(hw, 0x4000 + i * 2, words[i]);
    dts::Put32(hw, 0x400C, kGlobal);
    const DecompileResult r = d.Decompile(dts::MakeSnapshot(hw), Request());
    Dump("indexed global base refusal", r);
    CHECK(r.status == DecompileStatus::Ok);
    CheckReferences(r);
    CHECK(r.inspectionTargets.empty());
}

// Native printer fixtures deliberately control the symbol mappings which SH-2 analysis
// does not let a caller inject. They call the same adapter used by the real markup decoder.
class NativeInspectionFixture
{
public:
    std::ostringstream errors;
    SaturnArchitecture arch;
    ghidra::Funcdata* fd = nullptr;
    ghidra::Scope* scope = nullptr;
    ghidra::AddrSpace* ram = nullptr;
    ghidra::Datatype* scalar = nullptr;

    NativeInspectionFixture() : arch(dts::MakeSnapshot({}), &errors)
    {

        arch.Initialise(20000, nullptr);
        ram = arch.getDefaultDataSpace();
        scope = arch.symboltab->getGlobalScope();
        fd = scope->addFunction(ghidra::Address(ram, kEntry), "fixture")->getFunction();

        fd->setHighLevel();
        scalar = arch.types->getBase(4, ghidra::TYPE_UINT);
    }

    ghidra::Symbol* Symbol(uint32_t address, ghidra::Datatype* type, const char* name)
    {
        auto entry = scope->addSymbol(name, type, ghidra::Address(ram, address), ghidra::Address());
        auto symbol = entry->getSymbol();
        scope->setAttribute(symbol, ghidra::Varnode::persist | ghidra::Varnode::addrtied);
        return symbol;
    }

    ghidra::Varnode* Variable(uint32_t address, ghidra::Datatype* type)
    {
        auto vn = fd->newVarnode(type->getSize(), ghidra::Address(ram, address), type);
        vn = fd->setInputVarnode(vn);
        fd->linkSymbol(vn);
        return vn;
    }

    ghidra::PcodeOp* Spacebase(uint32_t address, ghidra::AddrSpace* space = nullptr)
    {
        auto baseType = arch.types->getTypeSpacebase(space ? space : ram, 0);
        auto pointerType = arch.types->getTypePointer(4, baseType, 1);
        auto base = fd->newConstant(4, 0);
        base->updateType(pointerType, true, true);
        auto offset = fd->newConstant(4, address);
        auto op = fd->newOp(2, ghidra::Address(ram, kEntry));
        fd->opSetOpcode(op, ghidra::CPUI_PTRSUB);
        fd->opSetInput(op, base, 0);
        fd->opSetInput(op, offset, 1);
        auto outType = arch.types->getTypePointer(4, scalar, 1);
        fd->opSetOutput(op, fd->newUnique(4, outType));
        fd->linkSymbolReference(offset);
        return op;
    }

    ghidra::PcodeOp* Read(ghidra::PcodeOp* pointer, uint32_t width = 4, bool implied = true)
    {
        if (implied) pointer->getOut()->setImplied();
        auto op = fd->newOp(2, ghidra::Address(ram, kEntry + 2));
        fd->opSetOpcode(op, ghidra::CPUI_LOAD);
        fd->opSetInput(op, fd->newVarnodeSpace(ram), 0);
        fd->opSetInput(op, pointer->getOut(), 1);
        fd->opSetOutput(op, fd->newUnique(width, arch.types->getBase(width, ghidra::TYPE_UINT)));
        return op;
    }

    ghidra::PcodeOp* Write(ghidra::PcodeOp* pointer)
    {
        pointer->getOut()->setImplied();
        auto op = fd->newOp(3, ghidra::Address(ram, kEntry + 2));
        fd->opSetOpcode(op, ghidra::CPUI_STORE);
        fd->opSetInput(op, fd->newVarnodeSpace(ram), 0);
        fd->opSetInput(op, pointer->getOut(), 1);
        auto value = fd->newConstant(4, 42);
        value->updateType(scalar, true, true);
        fd->opSetInput(op, value, 2);
        return op;
    }

    ghidra::PcodeOp* AddressUse(ghidra::PcodeOp* pointer, ghidra::OpCode code = ghidra::CPUI_COPY)
    {
        pointer->getOut()->setImplied();
        auto op = fd->newOp(code == ghidra::CPUI_COPY ? 1 : 2, ghidra::Address(ram, kEntry + 2));
        fd->opSetOpcode(op, code);
        fd->opSetInput(op, pointer->getOut(), 0);
        if (code != ghidra::CPUI_COPY) fd->opSetInput(op, fd->newConstant(4, 1), 1);
        fd->opSetOutput(op, fd->newUnique(4, arch.types->getTypePointer(4, scalar, 1)));
        return op;
    }
};

void CheckPackedVariable(NativeInspectionFixture& f, const ghidra::Varnode* variable,
                         const ghidra::PcodeOp* operation, bool expectedBinding,
                         InspectionTarget::Kind expectedKind = InspectionTarget::Kind::AbsoluteObject)
{
    std::ostringstream stream;
    {
        ghidra::EmitMarkup emitter;
        emitter.setPackedOutput(true);
        emitter.setOutputStream(&stream);
        emitter.tagVariable("whole_global", ghidra::EmitMarkup::global_color, variable, operation);
        emitter.flush();
    }
    std::vector<InspectionTarget> targets;
    const auto lines = DecodeDecompilerMarkup(stream.str(), *f.fd, &f.arch, targets);
    CHECK(lines.size() == 1);
    if (lines.size() != 1) return;
    CHECK(lines[0].text == "whole_global" && lines[0].tokens.size() == 1);
    if (lines[0].tokens.size() != 1) return;
    CHECK(bool(lines[0].tokens[0].inspectionTarget) == expectedBinding);
    CHECK(targets.size() == (expectedBinding ? 1u : 0u));
    if (expectedBinding && targets.size() == 1)
        CHECK(targets[0].kind == expectedKind && targets[0].address == kGlobal);
}

void TestNativeDirectWholeAndPartialMappings()
{
    InspectionTarget target;
    {
        NativeInspectionFixture f;
        f.Symbol(kGlobal, f.scalar, "whole_global");
        auto whole = f.Variable(kGlobal, f.scalar);
        CHECK(whole->getHigh() && whole->getHigh()->getSymbol());
        CHECK(whole->isAddrTied() && whole->isPersist());
        CHECK(ResolvePrintedInspection(whole, nullptr, &f.arch, target));
        CHECK(target.kind == InspectionTarget::Kind::AbsoluteObject && target.address == kGlobal && target.byteSize == 4);
        CHECK(target.display == InspectionTarget::Display::UnsignedInteger);
        CheckPackedVariable(f, whole, nullptr, true);
    }
    {
        // Separate function: Ghidra forbids overlapping full and partial input Varnodes.
        NativeInspectionFixture f;
        auto symbol = f.Symbol(kGlobal, f.scalar, "whole_global");
        auto partial = f.Variable(kGlobal + 2, f.arch.types->getBase(2, ghidra::TYPE_UINT));
        CHECK(partial->getHigh()->getSymbol() == symbol);
        CHECK(!ResolvePrintedInspection(partial, nullptr, &f.arch, target));
        CheckPackedVariable(f, partial, nullptr, false);
    }
    {
        NativeInspectionFixture f;
        auto split = f.Symbol(kGlobal + 0x100, f.scalar, "split_global");
        f.scope->addMapPoint(split, ghidra::Address(f.ram, kGlobal + 0x200), ghidra::Address());
        CHECK(split->numEntries() == 2);
        auto splitVar = f.Variable(kGlobal + 0x100, f.scalar);
        CHECK(splitVar->getHigh()->getSymbol() == split);
        CHECK(!ResolvePrintedInspection(splitVar, nullptr, &f.arch, target));
    }
}

void TestNativeNullVarrefGlobalsAndAddress()
{
    for (int form = 0; form < 3; ++form)
    {
        NativeInspectionFixture f;
        f.Symbol(kGlobal, f.scalar, "whole_global");
        auto pointer = f.Spacebase(kGlobal);
        CHECK(pointer->getIn(1)->getHigh()->getSymbol());
        CHECK(pointer->getIn(1)->getHigh()->getSymbolOffset() == 0);
        if (form == 0) f.Read(pointer);
        else if (form == 1) f.Write(pointer);
        else f.AddressUse(pointer);
        InspectionTarget target;
        CHECK(ResolvePrintedInspection(nullptr, pointer, &f.arch, target));
        CHECK(target.address == kGlobal && target.byteSize == 4);
        CHECK(target.kind == (form == 2 ? InspectionTarget::Kind::ConstantAddress : InspectionTarget::Kind::AbsoluteObject));
        // &global names a pointer to the symbol's type; the stored value keeps the plain type.
        CHECK((target.typeLabel.size() > 2 && target.typeLabel.compare(target.typeLabel.size() - 2, 2, " *") == 0) == (form == 2));
        CheckPackedVariable(f, nullptr, pointer, true, target.kind);
    }
    // An explicitly named pointer temporary prints &global in its definition even
    // when its sole later use is a LOAD; it must not become an object read.
    {
        NativeInspectionFixture f;
        f.Symbol(kGlobal, f.scalar, "whole_global");
        auto pointer = f.Spacebase(kGlobal);
        f.Read(pointer, 4, false);
        pointer->getOut()->setExplicit();
        InspectionTarget target;
        CHECK(ResolvePrintedInspection(nullptr, pointer, &f.arch, target));
        CHECK(target.kind == InspectionTarget::Kind::ConstantAddress);
        CheckPackedVariable(f, nullptr, pointer, true, InspectionTarget::Kind::ConstantAddress);
    }
    // No symbol is required for the printer's unnamed whole global path.
    NativeInspectionFixture unnamed;
    auto pointer = unnamed.Spacebase(kGlobal);
    CHECK(unnamed.fd->linkSymbolReference(pointer->getIn(1)) == nullptr);
    unnamed.Read(pointer);
    InspectionTarget target;
    CHECK(ResolvePrintedInspection(nullptr, pointer, &unnamed.arch, target));
    CHECK(target.address == kGlobal && target.byteSize == 4);
}

// Emits one packed variable token followed by plain text, then runs the production decoder.
// Returns the first token's binding: the final filters in LinesFromMarkup are what is under test,
// so the candidate itself is one the resolver accepts.
bool BoundWithSuffix(NativeInspectionFixture& f, const ghidra::Varnode* variable, const char* name,
                     const char* suffix)
{
    std::ostringstream stream;
    {
        ghidra::EmitMarkup emitter;
        emitter.setPackedOutput(true);
        emitter.setOutputStream(&stream);
        emitter.tagVariable(name, ghidra::EmitMarkup::global_color, variable, nullptr);
        if (*suffix) emitter.print(suffix);
        emitter.flush();
    }
    std::vector<InspectionTarget> targets;
    const auto lines = DecodeDecompilerMarkup(stream.str(), *f.fd, &f.arch, targets);
    CHECK(lines.size() == 1);
    if (lines.size() != 1 || lines[0].tokens.empty()) return false;
    CHECK(lines[0].text == std::string(name) + suffix);
    const bool bound = lines[0].tokens[0].inspectionTarget != 0;
    // A filtered token drops its reference; the unreferenced descriptor is never reachable.
    CHECK(!bound || (targets.size() == 1 && lines[0].tokens[0].inspectionTarget == 1));
    return bound;
}

void TestNativeFieldArrayAndLiteralFiltersRefused()
{
    {
        NativeInspectionFixture f;
        f.Symbol(kGlobal, f.scalar, "whole_global");
        auto whole = f.Variable(kGlobal, f.scalar);
        InspectionTarget candidate;
        CHECK(ResolvePrintedInspection(whole, nullptr, &f.arch, candidate));
        // The candidate is bindable alone and before ordinary punctuation...
        for (const char* ok : { "", ";", " = 0;", ")", " + 1", " ," })
            CHECK(BoundWithSuffix(f, whole, "whole_global", ok));
        // ...but a base followed by member or element syntax is not the whole printed object.
        for (const char* base : { ".x", " .x", "\t.x", "[0]", " [1]", "->x", " ->x" })
            CHECK(!BoundWithSuffix(f, whole, "whole_global", base));
    }
    {
        // A pointer-typed literal: only a plain number is the complete address. A character,
        // string or named representation may print part of it.
        NativeInspectionFixture f;
        auto pointer = f.arch.types->getTypePointer(4, f.scalar, 1);
        auto literal = f.fd->newConstant(4, kGlobal);
        literal->updateType(pointer, true, true);
        InspectionTarget target;
        CHECK(ResolvePrintedInspection(literal, nullptr, &f.arch, target));
        CHECK(target.kind == InspectionTarget::Kind::ConstantAddress && target.address == kGlobal);
        CHECK(BoundWithSuffix(f, literal, "0x6010000", ""));
        CHECK(BoundWithSuffix(f, literal, "0", ""));
        for (const char* text : { "'A'", "\"A\"", "L'A'", "SOME_EQUATE", "-1" })
            CHECK(!BoundWithSuffix(f, literal, text, ""));
    }
}

void TestNativeAmbiguousPrinterFormsRefused()
{
    // Partial-symbol PTRSUB, plus a valid global used as an array/field base.
    for (int form = 0; form < 5; ++form)
    {
        NativeInspectionFixture f;
        f.Symbol(kGlobal, f.scalar, "whole_global");
        auto pointer = f.Spacebase(form == 0 ? kGlobal + 2 : kGlobal);
        if (form == 0) f.Read(pointer, 2);
        else if (form == 1) f.AddressUse(pointer, ghidra::CPUI_PTRSUB);
        else if (form == 2) f.AddressUse(pointer, ghidra::CPUI_PTRADD);
        else if (form == 3) { f.Read(pointer); f.Write(pointer); }
        else f.Read(pointer, 2); // Whole symbol, partial-width access.
        InspectionTarget target;
        CHECK(!ResolvePrintedInspection(nullptr, pointer, &f.arch, target));
    }
    // Stack-spacebase offsets are not global bus storage, even when numerically plausible.
    NativeInspectionFixture stack;
    auto pointer = stack.Spacebase(kGlobal, stack.arch.getSpaceByName("stack"));
    stack.Read(pointer);
    InspectionTarget target;
    CHECK(!ResolvePrintedInspection(nullptr, pointer, &stack.arch, target));
}

void TestNativeArrayStructAndDynamicMappingsRefused()
{
    {
        NativeInspectionFixture f;
        auto array = f.arch.types->getTypeArray(4, f.arch.types->getBase(1, ghidra::TYPE_UINT));
        f.Symbol(kGlobal, array, "array_global");
        auto pointer = f.Spacebase(kGlobal);
        f.Read(pointer);
        InspectionTarget target;
        // PrintC changes a spacebase array expression into array_global[0].
        CHECK(!ResolvePrintedInspection(nullptr, pointer, &f.arch, target));
        CheckPackedVariable(f, nullptr, pointer, false);
    }
    {
        NativeInspectionFixture f;
        auto structure = f.arch.types->getTypeStruct("inspection_structure");
        auto half = f.arch.types->getBase(2, ghidra::TYPE_UINT);
        std::vector<ghidra::TypeField> fields;
        fields.emplace_back(0, -1, "first", half);
        fields.emplace_back(1, -1, "second", half);
        std::vector<ghidra::TypeBitField> bits;
        f.arch.types->assignRawFields(structure, fields, bits);
        f.Symbol(kGlobal, structure, "struct_global");
        auto pointer = f.Spacebase(kGlobal + 2);
        f.Read(pointer, 2);
        InspectionTarget target;
        CHECK(!ResolvePrintedInspection(nullptr, pointer, &f.arch, target));
        CheckPackedVariable(f, nullptr, pointer, false);
    }
    {
        NativeInspectionFixture f;
        // A storage Varnode attached to a dynamic mapping must never become an
        // absolute object merely because its current storage happens to be WRAM.
        auto local = f.fd->getScopeLocal();
        auto entry = local->addSymbol("dynamic_local", f.scalar,
                                     ghidra::Address(f.ram, kGlobal), ghidra::Address());
        auto vn = f.Variable(kGlobal, f.scalar);
        f.fd->remapDynamicVarnode(vn, entry->getSymbol(), ghidra::Address(f.ram, kEntry), 12345);
        CHECK(vn->getHigh()->getSymbol());
        CHECK(vn->getHigh()->getSymbolEntry()->isDynamic());
        InspectionTarget target;
        CHECK(!ResolvePrintedInspection(vn, nullptr, &f.arch, target));
        CheckPackedVariable(f, vn, nullptr, false);
    }
}

void TestDynamicAndLocalFormsStayUnbound(Sh2Decompiler& d)
{
    // The addresses of parameters/locals/array operations cannot prove absolute storage.
    for (auto words : { std::vector<uint16_t>{ 0x6042, 0x000B, 0x0009 }, // return *r4
                        std::vector<uint16_t>{ 0x6044, 0x6040, 0x000B, 0x0009 }, // byte p[1]
                        std::vector<uint16_t>{ 0x7FFC, 0x2F42, 0x60F2, 0x7F04, 0x000B, 0x0009 } })
    {
        std::vector<uint8_t> hw(0x100000u, 0);
        for (size_t i = 0; i < words.size(); ++i) dts::Put16(hw, 0x4000 + i * 2, words[i]);
        const DecompileResult r = d.Decompile(dts::MakeSnapshot(hw), Request());
        Dump("dynamic/local refusal", r);
        CHECK(r.status == DecompileStatus::Ok);
        CheckReferences(r);
        CHECK(r.inspectionTargets.empty());
        for (const Line& line : r.lines)
            for (const Token& token : line.tokens) CHECK(token.inspectionTarget == 0);
    }
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) { std::fprintf(stderr, "usage: %s <scratch dir>\n", argv[0]); return 2; }
    dts::PrepareProcess();
    const auto start = std::chrono::steady_clock::now();
    const std::string scratch = argv[1];
    dts::RemoveTree(scratch);
    CHECK(sfe::MakeDirectory(scratch));
    const std::string config = dts::Join(scratch, "config");
    CHECK(sfe::MakeDirectory(config));
    try
    {
        Sh2Decompiler d(config);
        RUN(TestScalarLoadsAndStores(d));
        RUN(TestPointerGlobal(d));
        RUN(TestPointerArgumentAddressForm(d));
        RUN(TestRealPrinterNamedGlobals(d));
        RUN(TestRealPrinterAddressOfNamedGlobal(d));
        RUN(TestGlobalArrayBaseStaysUnbound(d));
        RUN(TestDynamicAndLocalFormsStayUnbound(d));
        RUN(TestNativeDirectWholeAndPartialMappings());
        RUN(TestNativeNullVarrefGlobalsAndAddress());
        RUN(TestNativeAmbiguousPrinterFormsRefused());
        RUN(TestNativeFieldArrayAndLiteralFiltersRefused());
        RUN(TestNativeArrayStructAndDynamicMappingsRefused());
    }
    catch (const ghidra::LowlevelError& e) { std::fprintf(stderr, "FATAL: %s\n", e.explain.c_str()); return 3; }
    catch (const std::exception& e) { std::fprintf(stderr, "FATAL: %s\n", e.what()); return 3; }
    dts::RemoveTree(scratch);
    return dts::Finish("CapturedInspectionMetadataTests", start);
}
