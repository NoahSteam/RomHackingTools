// Unit tests for StructDefs: layout against hand-computed SH-2 offsets, the text format (accepted
// and rejected constructs with line numbers), Emit/Parse identity, field decode/encode, and
// robustness against truncated and garbled text.
#include "Debug/StructDefs.h"
#include "Debug/WriteDestText.h"

#include <cstdio>
#include <cstring>

using namespace sfe;

namespace
{

int gFail = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; }
}
void CheckStr(const std::string& got, const std::string& want, const char* what)
{
    if (got != want) { std::printf("FAIL: %s: got '%s' want '%s'\n", what, got.c_str(), want.c_str()); ++gFail; }
}

bool Parses(StructLibrary& lib, const std::string& text)
{
    std::vector<ParseError> errs;
    const bool ok = lib.Parse(text, errs);
    if (!ok)
        for (const ParseError& e : errs) std::printf("  parse error line %d: %s\n", e.line, e.message.c_str());
    return ok;
}

// Parse text that must fail; returns the first error (line, message) for the caller to inspect.
bool Fails(const std::string& text, int line, const char* needle, const char* what)
{
    StructLibrary lib;
    std::vector<ParseError> errs;
    const bool ok = lib.Parse(text, errs);
    bool match = !ok && !errs.empty();
    if (match)
    {
        match = false;
        for (const ParseError& e : errs)
            if ((line == 0 || e.line == line) && e.message.find(needle) != std::string::npos) match = true;
    }
    if (!match)
    {
        std::printf("FAIL: %s (ok=%d)\n", what, (int)ok);
        for (const ParseError& e : errs) std::printf("    line %d: %s\n", e.line, e.message.c_str());
        ++gFail;
    }
    return match;
}

uint32_t Off(const StructLibrary& lib, const char* s, size_t i)
{
    const StructLayout* L = lib.Layout(s);
    return (L && i < L->fields.size()) ? L->fields[i].offset : 0xDEADBEEFu;
}

std::string Enc(FieldKind k, FieldFormat fmt, const std::string& text, std::string* errOut = nullptr)
{
    FieldDef f;
    f.name = "v";
    f.kind = k;
    uint8_t b[4] = {0, 0, 0, 0};
    std::string err;
    const bool ok = EncodeField(f, fmt, text, b, err);
    if (errOut) *errOut = err;
    if (!ok) return "ERR";
    char buf[16];
    std::string s;
    for (uint32_t i = 0; i < FieldKindSize(k); ++i)
    {
        std::snprintf(buf, sizeof(buf), "%02X", b[i]);
        s += buf;
    }
    return s;
}

StructValue Dec(FieldKind k, FieldFormat fmt, std::initializer_list<uint8_t> bytes)
{
    FieldDef f;
    f.name = "v";
    f.kind = k;
    f.fmt = fmt;
    StructValue v;
    std::vector<uint8_t> b(bytes);
    DecodeField(f, b.data(), v);
    return v;
}

const char* kValid = R"(// sample
struct Vec3 size(0xC) {
    fixed x @0x00;
    fixed y @0x04;
    fixed z @0x08;
};

struct Actor {
    u8     state;
    u8     _pad[3];               // explicit padding is an ordinary field
    Vec3   pos;
    [[hex]] u16 flags @0x20;
    Vec3   vel;
    Actor* next;
    void*  userdata;
    u8*    bytes;
    rgb555 tint;
};

struct Packed packed {
    u8 a; u16 b; u8 c; u32 d; u8 e;
};
/* block
   comment */
view player = Actor[2] @ 0x060A1000;
view bad = Nope @ 1;
)";

void TestLayout()
{
    StructLibrary lib;
    // Mixed scalars: u16 aligns to 2, u32 to 4, tail pads to 4.
    Check(Parses(lib, "struct Mixed { u8 a; u16 b; u8 c; u32 d; u8 e; };"), "mixed parses");
    Check(Off(lib, "Mixed", 0) == 0 && Off(lib, "Mixed", 1) == 2 && Off(lib, "Mixed", 2) == 4 &&
          Off(lib, "Mixed", 3) == 8 && Off(lib, "Mixed", 4) == 12, "mixed offsets");
    Check(lib.Layout("Mixed")->size == 16 && lib.Layout("Mixed")->align == 4, "mixed size 16 align 4");

    Check(Parses(lib, "struct P packed { u8 a; u16 b; u8 c; u32 d; u8 e; };"), "packed parses");
    Check(Off(lib, "P", 1) == 1 && Off(lib, "P", 2) == 3 && Off(lib, "P", 3) == 4 && Off(lib, "P", 4) == 8 &&
          lib.Layout("P")->size == 9 && lib.Layout("P")->align == 1, "packed offsets, size 9");

    Check(Parses(lib, "struct T { u32 a; u8 b; };"), "tail parses");
    Check(lib.Layout("T")->size == 8, "tail padding to 8");

    // Order independence plus nesting and arrays.
    Check(Parses(lib,
        "struct Actor { u8 state; u8 pad[3]; Vec3 pos; u16 flags @0x20; Vec3 vel; Actor* next; void* ud; };"
        "struct Vec3 { fixed x; fixed y; fixed z; };"), "forward reference parses");
    Check(Off(lib, "Actor", 1) == 1 && Off(lib, "Actor", 2) == 4 && Off(lib, "Actor", 3) == 0x20 &&
          Off(lib, "Actor", 4) == 36 && Off(lib, "Actor", 5) == 48 && Off(lib, "Actor", 6) == 52 &&
          lib.Layout("Actor")->size == 56, "nested/explicit offsets, size 56");
    Check(lib.Layout("Vec3")->size == 12, "Vec3 size 12");
    Check(lib.Layout("Actor")->fields[1].elemSize == 1 && lib.Layout("Actor")->fields[1].size == 3,
          "array field elem/size");

    // Struct array: Small is size 2 align 1, so the u16 after lands at 6.
    Check(Parses(lib, "struct Small { u8 x; u8 y; }; struct H { Small s[3]; u16 w; };"), "array of struct parses");
    Check(Off(lib, "H", 1) == 6 && lib.Layout("H")->size == 8 && lib.Layout("H")->align == 2, "struct array layout");
    Check(lib.Layout("H")->fields[0].elemSize == 2, "struct elemSize");

    // Struct alignment follows its largest member.
    Check(Parses(lib, "struct In { u16 a; u8 b; }; struct Out { u8 x; In i; u8 y; };"), "align parses");
    // In: size 4 align 2 -> Out: x@0, i@2, y@6, size 8.
    Check(Off(lib, "Out", 1) == 2 && Off(lib, "Out", 2) == 6 && lib.Layout("Out")->size == 8, "nested alignment");

    // Explicit offset leaves a hole, the next implicit field follows the previous end.
    Check(Parses(lib, "struct E { u8 a; u32 b @0x10; u8 c; };"), "explicit parses");
    Check(Off(lib, "E", 1) == 0x10 && Off(lib, "E", 2) == 0x14 && lib.Layout("E")->size == 0x18, "explicit offsets");

    // Declared size: larger pads, equal is fine, smaller is rejected.
    Check(Parses(lib, "struct D size(0x20) { u8 a; };"), "declared bigger parses");
    Check(lib.Layout("D")->size == 0x20 && lib.Find("D")->minSize == 0x20, "declared size pads");
    Check(Parses(lib, "struct D2 size(4) { u8 a; };"), "declared equal parses");
    Check(lib.Layout("D2")->size == 4, "declared == natural");
    Fails("struct D3 size(4) {\n u32 a; u32 b; };", 1, "smaller than its layout", "declared size too small");
    Fails("\n\nstruct D4 packed size(0x4) { u32 a; u32 b; };", 3, "smaller than its layout", "size error line, either order");
    Fails("struct D5 size(4) size(8) { u32 a; };", 1, "given twice", "size twice");
    Fails("struct D6 size() { u32 a; };", 1, "expected size(N)", "empty size");
    Fails("struct D7 size(0x100001) { u32 a; };", 1, "exceeds the maximum", "size over max");
    // Comments are inert, whatever they say.
    Check(Parses(lib, "struct C1 { // size 1\n u32 a; u32 b; };\nstruct C2 { /* size(1) */ u32 a; // size of this is 12 words\n};"),
          "size comments are ignored");
    Check(lib.Layout("C1")->size == 8 && lib.Layout("C2")->size == 4 && lib.Find("C1")->minSize == 0, "comment does not change layout");
    Check(Parses(lib, "struct P2 size(0x20) packed { u8 a; };") && lib.Layout("P2")->size == 0x20 && lib.Find("P2")->packed,
          "modifiers in either order");
    Check(Parses(lib, "struct P3 size(12) { u8 a; };") && lib.Layout("P3")->size == 12, "decimal size");

    // Aliases.
    Check(Parses(lib, "struct Al { Uint8 a; Sint16 b; uint32_t c; FIXED d; ANGLE e; int8_t f; };"), "aliases parse");
    const StructDef* al = lib.Find("Al");
    Check(al && al->fields[1].kind == FieldKind::S16 && al->fields[3].kind == FieldKind::Fixed16_16 &&
          al->fields[4].kind == FieldKind::S16 && al->fields[5].kind == FieldKind::S8, "alias kinds");

    Check(lib.Find("Nope") == nullptr && lib.Layout("Nope") == nullptr, "unknown find is null");
}

void TestRejections()
{
    Fails("struct A { u8 a; };\nstruct A { u8 b; };", 2, "already defined", "duplicate struct");
    Fails("struct A {\n u8 a;\n u16 a;\n};", 3, "duplicate field", "duplicate field");
    Fails("struct A {\n Missing m;\n};", 2, "unknown type 'Missing'", "unknown type");
    Fails("struct A {\n Missing* m;\n};", 2, "unknown type 'Missing'", "unknown pointer type");
    Fails("struct A {\n A inner;\n};", 2, "contains itself by value", "self recursion");
    Fails("struct A { B b; };\nstruct B { C c; };\nstruct C { A a; };", 0, "contains itself by value", "mutual recursion");
    {
        StructLibrary lib;
        Check(Parses(lib, "struct N { N* next; N* prev; u32 v; };"), "pointer cycle is fine");
        Check(Parses(lib, "struct A { B* b; }; struct B { A* a; };"), "mutual pointer cycle is fine");
    }
    Fails("struct A {\n u32 a @4;\n u32 b @6;\n};", 3, "overlaps", "overlapping offset");
    Fails("struct A {\n u32 a @8;\n u32 b @4;\n};", 3, "overlaps or precedes", "backward offset");
    Fails("struct A {\n u8 a : 3;\n};", 2, "bitfields are not supported", "bitfield");
    Fails("union U { u8 a; };", 1, "unions are not supported", "top-level union");
    Fails("struct A {\n union { u8 a; } x;\n};", 2, "unions are not supported", "nested union");
    Fails("enum E { X };", 1, "enums are not supported", "enum");
    Fails("typedef u32 Foo;", 1, "typedefs are not supported", "typedef");
    Fails("#define X 1\nstruct A { u8 a; };", 1, "preprocessor", "preprocessor");
    Fails("struct A {\n unsigned int a;\n};", 2, "multi-token", "multi-token type");
    Fails("struct A {\n long long a;\n};", 2, "not supported", "long long");
    Fails("struct A {\n u8 a[2][3];\n};", 2, "multi-dimensional", "2d array");
    Fails("struct A {\n u8 a[0];\n};", 2, "array length", "zero array");
    Fails("struct A {\n u8 a[4294967295];\n};", 2, "array length", "huge array");
    Fails("struct A {\n u8 a\n};", 0, "missing ';'", "missing semicolon");
    Fails("struct A {\n u8 a;\n", 1, "missing its closing", "unterminated struct");
    Fails("struct A { };", 1, "no fields", "empty struct");
    Fails("struct A {\n void v;\n};", 2, "'void'", "bare void");
    Fails("struct A {\n u8** p;\n};", 2, "pointers to pointers", "pointer to pointer");
    Fails("struct A {\n [[hexx]] u8 a;\n};", 2, "unknown display attribute", "bad attribute");
    Fails("struct A {\n [[fixed]] u8 a;\n};", 2, "32-bit", "fixed attribute on u8");
    Fails("struct u8 { u8 a; };", 1, "reserved", "type name as struct name");
    Fails("struct A { u8 a; /* open", 1, "unterminated", "unterminated comment");
    Fails("garbage", 1, "expected 'struct' or 'view'", "top-level garbage");
    Fails("struct A { u8 a; }; view v = B[1] @ 0;", 1, "unknown struct", "view of unknown struct");
    Fails("struct A { u8 a; }; view v = A[0] @ 0;", 1, "invalid count", "view count 0");
    Fails("struct A { u8 a; };\nview v = A[1048577] @ 0;", 2, "invalid count", "view count over limit");
    Fails("struct A { u8 a; };\nview v = A[1] @ ;", 2, "no address expression", "empty view expression");
    Fails("struct A { u8 a @0x100000; };", 1, "maximum size", "offset past max");
    Fails("struct A { u8 a @4294967295; };", 1, "maximum size", "offset 32-bit max");

    // Sizes: the maximum is accepted, one past it is not, no 32-bit wraparound.
    {
        StructLibrary lib;
        Check(Parses(lib, "struct Max { u8 a[65536]; };"), "64 KiB array ok");
        Check(Parses(lib, "struct Big { u8 a[65536]; u8 b[65536]; u8 c[65536]; u8 d[65536]; u8 e[65536]; u8 f[65536];"
                          " u8 g[65536]; u8 h[65536]; u8 i[65536]; u8 j[65536]; u8 k[65536]; u8 l[65536];"
                          " u8 m[65536]; u8 n[65536]; u8 o[65536]; u8 p[65536]; };"), "exactly 1 MiB ok");
        Check(lib.Layout("Big")->size == kMaxStructSize, "1 MiB size");
    }
    Fails("struct Big { u8 a[65536]; u8 b[65536]; u8 c[65536]; u8 d[65536]; u8 e[65536]; u8 f[65536];"
          " u8 g[65536]; u8 h[65536]; u8 i[65536]; u8 j[65536]; u8 k[65536]; u8 l[65536];"
          " u8 m[65536]; u8 n[65536]; u8 o[65536]; u8 p[65536]; u8 q; };", 1, "maximum size", "1 MiB + 1");
    Fails("struct Big { u32 a[65536]; u32 b[65536]; u32 c[65536]; u32 d[65536]; u32 e[65536]; };", 1, "maximum size",
          "u32 arrays overflowing 32 bits of cumulative size");
    Fails("struct L { u8 a[65536]; u8 b[65536]; u8 c[65536]; u8 d[65536]; u8 e[65536]; u8 f[65536]; u8 g[65536]; u8 h[65536]; };\n"
          "struct M { L l[65536]; L m[65536]; };", 2, "maximum size", "nested array size multiplication");
    {
        StructLibrary lib;
        Check(Parses(lib, "struct Q { u8 a[4096]; };\nview v = Q[1048576] @ 0;"), "count*size == 2^32 ok");
    }
    Fails("struct Q { u8 a[4097]; };\nview v = Q[1048576] @ 0;", 2, "invalid count", "count*size > 2^32");
}

void TestTextRoundTrip()
{
    StructLibrary lib;
    Check(Parses(lib, kValid) == false, "view of an undefined struct is rejected");
    // Remove the deliberately bad view line.
    std::string good = kValid;
    good = good.substr(0, good.find("view bad"));
    Check(Parses(lib, good), "valid sample parses");
    Check(lib.Views().size() == 1 && lib.Views()[0].label == "player" && lib.Views()[0].structName == "Actor" &&
          lib.Views()[0].count == 2 && lib.Views()[0].expression == "0x060A1000", "view parsed");
    const StructDef* act = lib.Find("Actor");
    Check(act && act->fields[3].fmt == FieldFormat::Hex && act->fields[5].kind == FieldKind::Pointer &&
          act->fields[5].structName == "Actor" && act->fields[6].kind == FieldKind::Pointer &&
          act->fields[6].structName.empty() && act->fields[6].pointee == FieldKind::Struct &&
          act->fields[7].pointee == FieldKind::U8 && act->fields[8].kind == FieldKind::RGB555, "field kinds");
    Check(lib.Find("Vec3")->minSize == 0, "size comment equal to layout is not padding");

    const std::string e1 = lib.Emit();
    StructLibrary lib2;
    Check(Parses(lib2, e1), "emitted text parses");
    CheckStr(lib2.Emit(), e1, "Emit(Parse(Emit)) is stable");
    Check(lib.Equivalent(lib2), "Parse(Emit(lib)) equivalent");
    Check(e1.find("@0x20;") != std::string::npos && e1.find("size(0x40)") != std::string::npos,
          "emit writes offsets and total size");
    Check(e1.find("[[hex]] u16 flags") != std::string::npos, "emit writes attribute");
    Check(e1.find("struct Packed packed size(") != std::string::npos, "emit writes packed");
    Check(e1.find("view player = Actor[2] @ 0x060A1000;") != std::string::npos, "emit writes views");

    // Padding comment, formats, struct order survive.
    StructLibrary lib3;
    Check(Parses(lib3, "struct B size(0x40) {\n [[dec]] u32 a; [[fixed]] s32 b; fixed c[2]; };\nstruct A { B b; };"), "pad parses");
    const std::string e3 = lib3.Emit();
    StructLibrary lib4;
    Check(Parses(lib4, e3) && lib4.Equivalent(lib3) && lib4.Emit() == e3, "padded struct round-trips");
    Check(lib4.Layout("B")->size == 0x40 && lib4.Structs()[0].name == "B", "padded size and order kept");

    // Empty text is a valid empty library; Emit of empty is empty.
    StructLibrary empty;
    Check(Parses(empty, "  // nothing\n") && empty.Structs().empty() && empty.Emit().empty(), "empty library");

    // Mutating views through the API keeps the text re-parseable.
    std::string err;
    const uint64_t id = lib.AddView("second one", "Vec3", 3, "0x06000000 + 4", &err);
    Check(id != 0, "AddView");
    StructLibrary lib5;
    Check(Parses(lib5, lib.Emit()) && lib5.Equivalent(lib), "view with spaces round-trips");
    Check(lib.AddView("a=b", "Vec3", 1, "0", &err) == 0 && !err.empty(), "label with '=' rejected");
    Check(lib.AddView("x", "Nope", 1, "0", &err) == 0, "view of unknown struct rejected");
    Check(lib.AddView("x", "Vec3", 1, "1; 2", &err) == 0, "expression with ';' rejected");
    Check(lib.AddView("x", "Vec3", 1, "1 // c", &err) == 0, "expression with comment rejected");
    Check(lib.AddView("x", "Vec3", 0, "1", &err) == 0, "count 0 rejected");
    Check(lib.UpdateView(id, "renamed", "Vec3", 2, "0x1000", &err) && lib.FindView(id)->label == "renamed", "UpdateView");
    Check(lib.RemoveView(id) && lib.FindView(id) == nullptr && !lib.RemoveView(id), "RemoveView");
}

void TestLineEndingsAndEmpty()
{
    StructLibrary lib;
    std::vector<ParseError> errs;
    const std::string bom = "\xEF\xBB\xBF";
    Check(lib.Parse(bom + "// hdr\r\nstruct A {\r\n  u8 a;\r\n  u32 b;\r\n};\r\n", errs) && lib.Layout("A")->size == 8,
          "CRLF + BOM parses");
    // Errors report the right line with CRLF, a BOM, and a multi-line block comment before them.
    StructLibrary bad;
    Check(!bad.Parse(bom + "/* a\r\n b */\r\nstruct A {\r\n  u8 a : 2;\r\n};\r\n", errs) && !errs.empty() && errs[0].line == 4,
          "line number with CRLF/BOM/block comment");
    // Old-Mac lone CR is whitespace, not a line break; it must at least not crash or hang.
    StructLibrary cr;
    cr.Parse("struct A {\r u8 a;\r};", errs);
    // Empty library: emits nothing; Parse("") deliberately clears.
    StructLibrary empty;
    CheckStr(empty.Emit(), "", "empty library emits empty text");
    Check(Parses(lib, "") && lib.Structs().empty() && lib.Views().empty() && lib.Emit().empty(), "Parse(\"\") clears the library");
}

void TestAtomicParse()
{
    StructLibrary lib;
    Check(Parses(lib, "struct Keep { u32 a; };\nview k = Keep @ 0x06000000;"), "baseline");
    const std::string before = lib.Emit();
    const uint64_t gen = lib.Generation();
    std::vector<ParseError> errs;
    // The first struct is fine, the second is broken: nothing of the new text may land.
    Check(!lib.Parse("struct New { u8 x; };\nstruct Bad { u8 y : 2; };", errs) && !errs.empty(), "bad text rejected");
    CheckStr(lib.Emit(), before, "library untouched after a failed parse");
    Check(lib.Generation() == gen && lib.Find("New") == nullptr && lib.Find("Keep") != nullptr, "no partial library");
    Check(lib.Parse("struct New { u8 x; };", errs) && lib.Find("Keep") == nullptr && lib.Find("New") != nullptr &&
              lib.Generation() > gen && lib.Views().empty(), "success replaces completely");
}

void TestFlatten()
{
    StructLibrary lib;
    Check(Parses(lib, "struct V { s16 x; s16 y; };\nstruct S { u8 a; V v[2]; V* p; fixed f[3]; };"), "flatten sample");
    std::vector<LeafField> leaves;
    bool trunc = false;
    Check(lib.FlattenLeaves("S", 100, leaves, trunc) && !trunc && leaves.size() == 1 + 4 + 1 + 3, "leaf count");
    Check(leaves[1].path == "v[0].x" && leaves[1].offset == 2 && leaves[4].path == "v[1].y" && leaves[4].offset == 8 &&
          leaves[5].path == "p" && leaves[5].offset == 12 && leaves[8].path == "f[2]" && leaves[8].offset == 24,
          "leaf paths and offsets");
    Check(lib.FlattenLeaves("S", 3, leaves, trunc) && trunc && leaves.size() == 3, "leaf cap");
    Check(!lib.FlattenLeaves("Zzz", 3, leaves, trunc), "flatten unknown");
    // A huge array must stop at the cap rather than build 65536 leaves.
    StructLibrary big;
    Check(Parses(big, "struct B { u32 a[65536]; };"), "big array");
    Check(big.FlattenLeaves("B", 64, leaves, trunc) && trunc && leaves.size() == 64, "big array capped");
}

void TestDecode()
{
    StructValue v = Dec(FieldKind::U16, FieldFormat::Default, {0x12, 0x34});
    CheckStr(v.text, "1234", "u16 default hex");
    Check(v.raw == 0x1234 && v.numeric == 0x1234, "u16 raw");
    CheckStr(Dec(FieldKind::U8, FieldFormat::Dec, {0xFF}).text, "255", "u8 dec");
    CheckStr(Dec(FieldKind::S8, FieldFormat::Default, {0xFF}).text, "-1", "s8 default dec");
    CheckStr(Dec(FieldKind::S8, FieldFormat::Hex, {0xFF}).text, "FF", "s8 hex");
    CheckStr(Dec(FieldKind::S16, FieldFormat::Default, {0x80, 0x00}).text, "-32768", "s16 min");
    CheckStr(Dec(FieldKind::U32, FieldFormat::Default, {0xDE, 0xAD, 0xBE, 0xEF}).text, "DEADBEEF", "u32 hex");
    CheckStr(Dec(FieldKind::S32, FieldFormat::Default, {0xFF, 0xFF, 0xFF, 0xFE}).text, "-2", "s32 dec");
    Check(Dec(FieldKind::S32, FieldFormat::Default, {0xFF, 0xFF, 0xFF, 0xFE}).numeric == -2, "s32 numeric");
    CheckStr(Dec(FieldKind::Fixed16_16, FieldFormat::Default, {0xFF, 0xFF, 0x80, 0x00}).text, "-0.5", "fixed -0.5");
    CheckStr(Dec(FieldKind::Fixed16_16, FieldFormat::Default, {0x00, 0x01, 0x80, 0x00}).text, "1.5", "fixed 1.5");
    CheckStr(Dec(FieldKind::Fixed16_16, FieldFormat::Default, {0x00, 0x00, 0x00, 0x00}).text, "0.0", "fixed 0");
    CheckStr(Dec(FieldKind::Fixed16_16, FieldFormat::Default, {0x00, 0x01, 0x80, 0x00}).tooltip, "raw 0x00018000", "fixed tooltip raw");
    CheckStr(Dec(FieldKind::Fixed16_16, FieldFormat::Hex, {0x00, 0x01, 0x80, 0x00}).text, "00018000", "fixed hex");
    CheckStr(Dec(FieldKind::U32, FieldFormat::Fixed, {0x00, 0x02, 0x00, 0x00}).text, "2.0", "u32 [[fixed]]");
    CheckStr(Dec(FieldKind::Fixed16_16, FieldFormat::Default, {0x7F, 0xFF, 0xFF, 0xFF}).text, "32767.999985",
             "fixed max display");
    CheckStr(Dec(FieldKind::Fixed16_16, FieldFormat::Default, {0x80, 0x00, 0x00, 0x00}).text, "-32768.0", "fixed min");

    v = Dec(FieldKind::RGB555, FieldFormat::Default, {0x7C, 0x1F});   // R=31 G=0 B=31
    CheckStr(v.text, "R31 G0 B31", "rgb555 text");
    Check(v.hasSwatch && v.r == 255 && v.g == 0 && v.b == 255, "rgb555 swatch");
    CheckStr(v.editText, "31 0 31", "rgb555 edit text");
    CheckStr(Dec(FieldKind::RGB555, FieldFormat::Hex, {0x7C, 0x1F}).text, "7C1F", "rgb555 hex");

    v = Dec(FieldKind::Pointer, FieldFormat::Default, {0x06, 0x0A, 0x10, 0x00});
    Check(v.isPointer && v.pointerTarget == 0x060A1000, "pointer target");
    CheckStr(v.text, "-> 0x060A1000", "pointer text");
    CheckStr(Dec(FieldKind::Pointer, FieldFormat::Default, {0, 0, 0, 0}).text, "null", "null pointer");

    FieldDef st;
    st.kind = FieldKind::Struct;
    uint8_t z[4] = {0};
    StructValue sv;
    Check(!DecodeField(st, z, sv) && !sv.valid, "struct kind is not a scalar");
    FieldDef sc;
    sc.kind = FieldKind::U8;
    Check(!DecodeField(sc, nullptr, sv), "null buffer");
}

void TestEncode()
{
    std::string err;
    CheckStr(Enc(FieldKind::U8, FieldFormat::Default, "FF"), "FF", "u8 bare hex");
    CheckStr(Enc(FieldKind::U8, FieldFormat::Default, "0xff"), "FF", "u8 0x hex");
    CheckStr(Enc(FieldKind::U8, FieldFormat::Dec, "255"), "FF", "u8 dec");
    CheckStr(Enc(FieldKind::U8, FieldFormat::Dec, "0x10"), "10", "dec format still takes 0x");
    CheckStr(Enc(FieldKind::U8, FieldFormat::Default, "100", &err), "ERR", "u8 out of range");
    Check(err.find("out of range for u8") != std::string::npos && err.find("0..255") != std::string::npos, "u8 range message");
    CheckStr(Enc(FieldKind::U8, FieldFormat::Dec, "256"), "ERR", "u8 dec 256");
    CheckStr(Enc(FieldKind::U8, FieldFormat::Dec, "-1", &err), "ERR", "u8 negative");
    Check(err.find("negative") != std::string::npos, "u8 negative message");
    CheckStr(Enc(FieldKind::U8, FieldFormat::Default, "zz", &err), "ERR", "u8 garbage");
    Check(err.find("not a valid hex") != std::string::npos, "garbage message");
    CheckStr(Enc(FieldKind::U8, FieldFormat::Default, "  ", &err), "ERR", "empty");
    Check(err == "Enter a value.", "empty message");
    CheckStr(Enc(FieldKind::U16, FieldFormat::Default, "1234"), "1234", "u16 hex BE");
    CheckStr(Enc(FieldKind::U16, FieldFormat::Dec, "65535"), "FFFF", "u16 max");
    CheckStr(Enc(FieldKind::U16, FieldFormat::Dec, "65536"), "ERR", "u16 overflow");
    CheckStr(Enc(FieldKind::U32, FieldFormat::Default, "DEADBEEF"), "DEADBEEF", "u32 BE");
    CheckStr(Enc(FieldKind::U32, FieldFormat::Default, "100000000"), "ERR", "u32 overflow");
    CheckStr(Enc(FieldKind::U32, FieldFormat::Default, "99999999999999999999999999"), "ERR", "u32 huge literal");
    CheckStr(Enc(FieldKind::S8, FieldFormat::Default, "-128"), "80", "s8 min");
    CheckStr(Enc(FieldKind::S8, FieldFormat::Default, "127"), "7F", "s8 max");
    CheckStr(Enc(FieldKind::S8, FieldFormat::Default, "128"), "ERR", "s8 dec overflow");
    CheckStr(Enc(FieldKind::S8, FieldFormat::Default, "-129"), "ERR", "s8 underflow");
    CheckStr(Enc(FieldKind::S8, FieldFormat::Default, "0xFF"), "FF", "s8 hex bit pattern");
    CheckStr(Enc(FieldKind::S16, FieldFormat::Default, "-1"), "FFFF", "s16 -1");
    CheckStr(Enc(FieldKind::S16, FieldFormat::Hex, "8000"), "8000", "s16 hex format");
    CheckStr(Enc(FieldKind::S16, FieldFormat::Default, "-32769"), "ERR", "s16 underflow");
    CheckStr(Enc(FieldKind::S32, FieldFormat::Default, "-2147483648"), "80000000", "s32 min");
    CheckStr(Enc(FieldKind::S32, FieldFormat::Default, "2147483648"), "ERR", "s32 overflow");
    CheckStr(Enc(FieldKind::S32, FieldFormat::Default, "-2"), "FFFFFFFE", "s32 -2");

    CheckStr(Enc(FieldKind::Fixed16_16, FieldFormat::Default, "-0.5"), "FFFF8000", "fixed -0.5");
    CheckStr(Enc(FieldKind::Fixed16_16, FieldFormat::Default, "1.5"), "00018000", "fixed 1.5");
    CheckStr(Enc(FieldKind::Fixed16_16, FieldFormat::Default, "0x00018000"), "00018000", "fixed raw hex");
    CheckStr(Enc(FieldKind::Fixed16_16, FieldFormat::Default, "-0.25"), "FFFFC000", "fixed -0.25");
    CheckStr(Enc(FieldKind::Fixed16_16, FieldFormat::Default, "3"), "00030000", "fixed integer");
    CheckStr(Enc(FieldKind::Fixed16_16, FieldFormat::Default, ".5"), "00008000", "fixed leading dot");
    CheckStr(Enc(FieldKind::Fixed16_16, FieldFormat::Default, "-32768"), "80000000", "fixed min");
    CheckStr(Enc(FieldKind::Fixed16_16, FieldFormat::Default, "32768", &err), "ERR", "fixed overflow");
    Check(err.find("out of range for fixed") != std::string::npos, "fixed range message");
    CheckStr(Enc(FieldKind::Fixed16_16, FieldFormat::Default, "-32769"), "ERR", "fixed underflow");
    CheckStr(Enc(FieldKind::Fixed16_16, FieldFormat::Default, "1.2.3"), "ERR", "fixed garbage");
    CheckStr(Enc(FieldKind::Fixed16_16, FieldFormat::Default, "0x123456789"), "ERR", "fixed raw too long");
    CheckStr(Enc(FieldKind::U32, FieldFormat::Fixed, "1.5"), "00018000", "u32 [[fixed]] input");
    CheckStr(Enc(FieldKind::Fixed16_16, FieldFormat::Dec, "-1"), "FFFFFFFF", "fixed kind with dec format is raw");

    CheckStr(Enc(FieldKind::RGB555, FieldFormat::Default, "31 0 31"), "7C1F", "rgb555 triple");
    CheckStr(Enc(FieldKind::RGB555, FieldFormat::Default, "R1 G2 B3"), "0C41", "rgb555 labelled");
    CheckStr(Enc(FieldKind::RGB555, FieldFormat::Default, "0x7FFF"), "7FFF", "rgb555 word");
    CheckStr(Enc(FieldKind::RGB555, FieldFormat::Default, "32 0 0", &err), "ERR", "rgb555 component range");
    Check(err.find("0-31") != std::string::npos, "rgb555 range message");
    CheckStr(Enc(FieldKind::RGB555, FieldFormat::Default, "1 2"), "ERR", "rgb555 too few");
    CheckStr(Enc(FieldKind::RGB555, FieldFormat::Default, "1 2 3 4"), "ERR", "rgb555 too many");
    CheckStr(Enc(FieldKind::RGB555, FieldFormat::Default, "red"), "ERR", "rgb555 garbage");
    CheckStr(Enc(FieldKind::RGB555, FieldFormat::Hex, "7C1F"), "7C1F", "rgb555 hex format");
    CheckStr(Enc(FieldKind::RGB555, FieldFormat::Hex, "10000"), "ERR", "rgb555 word overflow");

    CheckStr(Enc(FieldKind::Pointer, FieldFormat::Default, "0x060A1000"), "060A1000", "pointer 0x");
    CheckStr(Enc(FieldKind::Pointer, FieldFormat::Default, "060A1000"), "060A1000", "pointer bare hex");
    CheckStr(Enc(FieldKind::Pointer, FieldFormat::Dec, "256"), "00000100", "pointer dec");
    CheckStr(Enc(FieldKind::Pointer, FieldFormat::Default, "-1"), "ERR", "pointer negative");
    CheckStr(Enc(FieldKind::Pointer, FieldFormat::Default, "123456789"), "ERR", "pointer over 32 bits");

    CheckStr(Enc(FieldKind::Struct, FieldFormat::Default, "1"), "ERR", "struct not editable");
    // Garbled bytes in a text edit box must never crash.
    CheckStr(Enc(FieldKind::U32, FieldFormat::Default, std::string("\xff\xfe\x00zz", 5)), "ERR", "binary garbage");

    // Every decoded value's editText must encode back to the same bytes, for every kind/format.
    const FieldKind kinds[] = {FieldKind::U8, FieldKind::S8, FieldKind::U16, FieldKind::S16, FieldKind::U32,
                               FieldKind::S32, FieldKind::Fixed16_16, FieldKind::Pointer, FieldKind::RGB555};
    const FieldFormat fmts[] = {FieldFormat::Default, FieldFormat::Hex, FieldFormat::Dec, FieldFormat::Fixed};
    unsigned s = 12345;
    int rtFail = 0;
    for (FieldKind k : kinds)
        for (FieldFormat fm : fmts)
            for (int n = 0; n < 200; ++n)
            {
                uint8_t b[4];
                for (int i = 0; i < 4; ++i) { s = s * 1664525u + 1013904223u; b[i] = (uint8_t)(s >> 24); }
                if (n == 0) std::memset(b, 0, 4);
                if (n == 1) std::memset(b, 0xFF, 4);
                FieldDef f;
                f.name = "v"; f.kind = k; f.fmt = fm;
                StructValue v;
                DecodeField(f, b, v);
                uint8_t o[4] = {0};
                std::string e;
                if (!EncodeField(f, fm, v.editText, o, e) || std::memcmp(o, b, FieldKindSize(k)) != 0)
                {
                    if (rtFail++ < 5) std::printf("  roundtrip kind %d fmt %d text '%s' err '%s'\n", (int)k, (int)fm, v.editText.c_str(), e.c_str());
                }
            }
    Check(rtFail == 0, "editText round-trips through EncodeField for every kind and format");
}

void TestWriteDestText()
{
    using D = IMemoryBackend::WriteDest;
    CheckStr(WriteDestText(D::Emulator), "Queued for emulator", "dest emulator");
    CheckStr(WriteDestText(D::Staged), "Staged for Play From Here", "dest staged");
    CheckStr(WriteDestText(D::ViewOnly), "Modified (this view only)", "dest view only");
}

// Deterministic generator (xorshift) so a failure reproduces.
struct Rng
{
    uint64_t s = 0x9E3779B97F4A7C15ull;
    uint32_t Next()
    {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        return (uint32_t)(s >> 16);
    }
};

void TestFuzz()
{
    StructLibrary base;
    std::string good = kValid;
    good = good.substr(0, good.find("view bad"));
    Check(Parses(base, good), "fuzz base parses");
    const std::string baseEmit = base.Emit();

    // Hold a library, throw damaged text at it: a failure must leave it exactly as it was, a
    // success must produce text that re-parses to the same library.
    auto probe = [&](const std::string& text, const char* kind)
    {
        StructLibrary lib;
        std::vector<ParseError> e0;
        lib.Parse(good, e0);
        std::vector<ParseError> errs;
        const bool ok = lib.Parse(text, errs);
        if (!ok)
        {
            if (errs.empty() || lib.Emit() != baseEmit)
            {
                std::printf("FAIL: %s: failed parse changed the library or gave no error\n", kind);
                ++gFail;
            }
            return;
        }
        StructLibrary again;
        std::vector<ParseError> e2;
        const std::string em = lib.Emit();
        if (!again.Parse(em, e2) || again.Emit() != em || !again.Equivalent(lib))
        {
            std::printf("FAIL: %s: accepted text does not round-trip\n", kind);
            ++gFail;
        }
    };

    for (size_t n = 0; n <= good.size(); ++n) probe(good.substr(0, n), "prefix");
    for (size_t n = 0; n < good.size(); n += 7) probe(good.substr(n), "suffix");

    Rng rng;
    const char junk[] = "{}[];@*:#/\\\"'=,. \n\t0123456789xabcdefstructview";
    for (int i = 0; i < 4000; ++i)
    {
        std::string t = good;
        const int muts = 1 + (int)(rng.Next() % 6);
        for (int m = 0; m < muts && !t.empty(); ++m)
        {
            const size_t pos = rng.Next() % t.size();
            switch (rng.Next() % 5)
            {
            case 0: t[pos] = junk[rng.Next() % (sizeof(junk) - 1)]; break;
            case 1: t.erase(pos, 1 + rng.Next() % 8); break;
            case 2: t.insert(pos, 1, junk[rng.Next() % (sizeof(junk) - 1)]); break;
            case 3: t[pos] = (char)(rng.Next() & 0xFF); break;
            default:
            {
                const size_t from = rng.Next() % t.size();
                t.insert(pos, t.substr(from, rng.Next() % 24));
                break;
            }
            }
        }
        probe(t, "mutation");
    }

    // Deep brace nesting and a very long run of tokens must neither recurse nor blow up.
    probe(std::string(200000, '{'), "braces");
    probe("struct A {" + std::string(100000, ';') + "u8 a; };", "semicolons");
    {
        std::string many;
        for (int i = 0; i < 5000; ++i) many += "struct S" + std::to_string(i) + " { u8 a; };\n";
        StructLibrary lib;
        std::vector<ParseError> errs;
        Check(!lib.Parse(many, errs), "more than kMaxStructs rejected");
    }
    {
        std::string chain;   // each struct holds the previous by value: a deep but legal chain
        chain += "struct C0 { u8 a; };\n";
        for (int i = 1; i < 2000; ++i)
            chain += "struct C" + std::to_string(i) + " { C" + std::to_string(i - 1) + " c; };\n";
        StructLibrary lib;
        std::vector<ParseError> errs;
        Check(lib.Parse(chain, errs) && lib.Layout("C1999")->size == 1, "deep by-value chain");
    }
}

}  // namespace

int main()
{
    TestLayout();
    TestRejections();
    TestTextRoundTrip();
    TestLineEndingsAndEmpty();
    TestAtomicParse();
    TestFlatten();
    TestDecode();
    TestEncode();
    TestWriteDestText();
    TestFuzz();
    if (gFail == 0) std::printf("StructDefsTests: all passed\n");
    else std::printf("StructDefsTests: %d failure(s)\n", gFail);
    return gFail == 0 ? 0 : 1;
}
