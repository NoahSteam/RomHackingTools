// StructDefs -- the UI-free model behind the Structs panel: C-like struct definitions, their
// SH-2 layout, the text format they are stored in, persisted "view" lines, and the big-endian
// decode/encode of one field. No ImGui and no emulator dependency: decoding works from a
// caller-supplied byte buffer, so the same code can run over a backend read, a MemSnapshot or a
// decompiler snapshot.
//
// Limits (enforced by Parse/AddView so layout arithmetic can never overflow; sizes are computed
// in 64 bits and checked against these before being narrowed):
//   kMaxStructSize 1 MiB per struct, kMaxArrayCount 65536 per field, kMaxFields 4096 per struct,
//   kMaxStructs 4096, kMaxViews 4096, kMaxViewCount 1<<20 elements per view, and a view's
//   count * struct size must fit the 32-bit address space.
//
// Text format, in one glance (the plan's "Text format (v1)"):
//   struct Actor packed {          // size 0x40     <- optional; a declared size may only pad
//       u8     state @0x00;
//       fixed  pos[3];
//       [[hex]] u16 flags @0x20;   // display attribute: hex | dec | fixed
//       Vec3   vel;                // nested by value
//       Actor* next;               // pointer, 4 bytes (void* and u8* etc. also accepted)
//   };
//   view player = Actor[1] @ 0x060A1000;
// Types: u8 s8 u16 s16 u32 s32 fixed rgb555, aliases uint8_t..int32_t, Uint8..Sint32, FIXED,
// ANGLE (s16). Big-endian only.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace sfe
{

constexpr uint32_t kMaxStructSize = 0x100000u;
constexpr uint32_t kMaxArrayCount = 65536u;
constexpr uint32_t kMaxFields     = 4096u;
constexpr uint32_t kMaxStructs    = 4096u;
constexpr uint32_t kMaxViews      = 4096u;
constexpr uint32_t kMaxViewCount  = 1u << 20;

enum class FieldKind { U8, S8, U16, S16, U32, S32, Fixed16_16, Pointer, Struct, RGB555 };
enum class FieldFormat { Default, Hex, Dec, Fixed };

// Source spelling of a scalar kind ("u8" .. "fixed", "rgb555"); "" for Pointer/Struct.
const char* FieldKindName(FieldKind k);
// Bytes in one element of a scalar kind (Pointer = 4); 0 for Struct, which needs a layout.
uint32_t    FieldKindSize(FieldKind k);

struct FieldDef
{
    std::string name;
    FieldKind   kind = FieldKind::U32;
    // Struct: the nested type. Pointer: the pointee struct ("" = void* or a scalar pointee).
    std::string structName;
    // Pointer with an empty structName only: a scalar pointee (u8* ...). FieldKind::Struct
    // here means "no scalar pointee", i.e. void*.
    FieldKind   pointee = FieldKind::Struct;
    uint32_t    count = 1;                // array length (1 when not an array)
    bool        isArray = false;          // written name[N]; keeps name[1] distinct from name
    bool        hasExplicitOffset = false;
    uint32_t    offset = 0;               // the written offset when hasExplicitOffset
    FieldFormat fmt = FieldFormat::Default;
};

struct StructDef
{
    std::string           name;
    bool                  packed = false;      // no padding, alignment 1 (hand-reversed layouts)
    std::vector<FieldDef> fields;
    // A "// size 0xNN" comment larger than the natural layout: explicit tail padding. 0 = none.
    // Only recorded when it exceeds the natural size, so Emit -> Parse is the identity.
    uint32_t              minSize = 0;
    uint32_t              size = 0;            // total bytes including tail padding (set by Parse)
    uint32_t              align = 1;           // set by Parse
    int                   line = 0;            // 1-based source line of the "struct" keyword
};

// Where one field landed. elemSize is one array element; size = elemSize * count.
struct FieldLayout
{
    uint32_t offset = 0;
    uint32_t elemSize = 0;
    uint32_t size = 0;
    uint32_t align = 1;
};

struct StructLayout
{
    uint32_t size = 0;
    uint32_t align = 1;
    std::vector<FieldLayout> fields;           // parallel to StructDef::fields
};

// A persisted instance: "view label = Type[count] @ expression;". The expression is kept as text;
// resolving it to an address is the panel's job (IExpressionResolver).
struct StructView
{
    uint64_t    id = 0;                        // runtime only, never written to the text
    std::string label;
    std::string expression;
    std::string structName;
    uint32_t    count = 1;
};

struct ParseError
{
    int         line = 0;                      // 1-based; 0 when not tied to a line
    std::string message;
};

// One decoded element, already formatted for display.
struct StructValue
{
    bool        valid = false;
    std::string text;                          // table cell text
    std::string editText;                      // pre-fill for the edit box; EncodeField accepts it
    std::string tooltip;                       // raw bytes / extra detail (fixed: raw hex)
    uint32_t    raw = 0;                       // big-endian value, zero-extended
    int64_t     numeric = 0;                   // sign-aware value (0 for rgb555)
    bool        isPointer = false;
    uint32_t    pointerTarget = 0;
    bool        hasSwatch = false;             // rgb555
    uint8_t     r = 0, g = 0, b = 0;
};

// Decode ONE element of a scalar field from 'bytes' (FieldKindSize(kind) bytes, big-endian).
// The caller advances by FieldLayout::elemSize for array elements. Returns false for
// FieldKind::Struct or a null buffer. The 4-argument form previews another format.
bool DecodeField(const FieldDef& f, const uint8_t* bytes, StructValue& out);
bool DecodeField(const FieldDef& f, FieldFormat fmt, const uint8_t* bytes, StructValue& out);

// Parse user text into big-endian bytes (out receives FieldKindSize(kind) bytes). Accepts hex
// ("0x1F", or bare hex under Hex format), decimal, fixed ("1.5", "-0.25", raw "0x00018000"),
// rgb555 ("R G B" or a 0x word). On failure returns false with a message fit for the user.
bool EncodeField(const FieldDef& f, FieldFormat fmt, const std::string& text,
                 uint8_t out[4], std::string& err);

// "u16[4]", "Actor*", "void*", "u8*[2]" for the Type column.
std::string FieldTypeText(const FieldDef& f);

// A scalar leaf of a (possibly nested) struct, for table columns.
struct LeafField
{
    std::string path;                          // "pos[1]", "vel.x"
    uint32_t    offset = 0;                    // from the start of the root struct
    FieldDef    field;                         // kind is never Struct; count is 1
};

class StructLibrary
{
public:
    // Replace the whole library from 'text'. On any error the library is left untouched, 'errors'
    // lists every problem with its line number, and false is returned. Empty text yields an empty
    // library. Views in the text replace the current ones (ids are assigned fresh, ascending).
    bool Parse(const std::string& text, std::vector<ParseError>& errors);

    // Canonical text: definition order, "@offset" on every field, a "// size" comment, then the
    // views. Emit(Parse(Emit(x))) == Emit(x).
    std::string Emit() const;

    void Clear();

    const std::vector<StructDef>& Structs() const { return mStructs; }
    const StructDef*    Find(const std::string& name) const;
    // Null for an unknown name. Every struct that parsed has a valid layout.
    const StructLayout* Layout(const std::string& name) const;

    // Bumps on every successful Parse/Clear; panels cache layouts against it.
    uint64_t Generation() const { return mGeneration; }

    // Flatten a struct into scalar leaves (arrays expanded element by element, nested structs
    // inlined, pointers kept as leaves), at most 'maxLeaves'; 'truncated' is set if more existed.
    // False for an unknown struct.
    bool FlattenLeaves(const std::string& structName, size_t maxLeaves,
                       std::vector<LeafField>& out, bool& truncated) const;

    // --- persisted views ---
    const std::vector<StructView>& Views() const { return mViews; }
    const StructView* FindView(uint64_t id) const;
    uint64_t ViewsGeneration() const { return mViewsGeneration; }
    // Return the new id, or 0 with 'err' set. Validates the label token, the struct, the count
    // (1..kMaxViewCount, count*size within 32-bit space) and that the expression is non-empty and
    // free of ';', braces, comment openers and newlines (so Emit always re-parses).
    uint64_t AddView(const std::string& label, const std::string& structName, uint32_t count,
                     const std::string& expression, std::string* err = nullptr);
    bool UpdateView(uint64_t id, const std::string& label, const std::string& structName,
                    uint32_t count, const std::string& expression, std::string* err = nullptr);
    bool RemoveView(uint64_t id);

    // Same definitions, layouts and views, ignoring view ids and whether an offset was written
    // explicitly (Emit writes them all). This is "Parse(Emit(lib)) is the identity".
    bool Equivalent(const StructLibrary& other) const;

private:
    bool ValidateView(const std::string& label, const std::string& structName, uint32_t count,
                      const std::string& expression, std::string* err) const;

    std::vector<StructDef>    mStructs;
    std::vector<StructLayout> mLayouts;        // parallel to mStructs
    std::unordered_map<std::string, size_t> mIndex;
    std::vector<StructView>   mViews;
    uint64_t mNextViewId = 1;
    uint64_t mGeneration = 1;
    uint64_t mViewsGeneration = 1;
};

}  // namespace sfe
