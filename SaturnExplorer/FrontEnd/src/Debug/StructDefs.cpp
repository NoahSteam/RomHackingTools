#include "Debug/StructDefs.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

namespace sfe
{

namespace
{

// ---------------------------------------------------------------------------------------------
// Kinds and names
// ---------------------------------------------------------------------------------------------

struct ScalarName { const char* name; FieldKind kind; };

// Canonical spellings first (FieldKindName returns the first match), then the single-token
// aliases from SGL/SBL headers. ANGLE is s16 there.
const ScalarName kScalarNames[] = {
    {"u8", FieldKind::U8},   {"s8", FieldKind::S8},   {"u16", FieldKind::U16},
    {"s16", FieldKind::S16}, {"u32", FieldKind::U32}, {"s32", FieldKind::S32},
    {"fixed", FieldKind::Fixed16_16}, {"rgb555", FieldKind::RGB555},
    {"uint8_t", FieldKind::U8},   {"int8_t", FieldKind::S8},
    {"uint16_t", FieldKind::U16}, {"int16_t", FieldKind::S16},
    {"uint32_t", FieldKind::U32}, {"int32_t", FieldKind::S32},
    {"Uint8", FieldKind::U8},   {"Sint8", FieldKind::S8},
    {"Uint16", FieldKind::U16}, {"Sint16", FieldKind::S16},
    {"Uint32", FieldKind::U32}, {"Sint32", FieldKind::S32},
    {"FIXED", FieldKind::Fixed16_16}, {"ANGLE", FieldKind::S16},
};

bool LookupScalar(const std::string& n, FieldKind& out)
{
    for (const ScalarName& s : kScalarNames)
        if (n == s.name) { out = s.kind; return true; }
    return false;
}

// Words that start a C construct we do not model. Naming the construct in the error is the whole
// point of listing them, so each maps to the sentence shown to the user.
const char* UnsupportedWord(const std::string& w)
{
    static const struct { const char* word; const char* what; } kWords[] = {
        {"union",    "unions are not supported"},
        {"enum",     "enums are not supported"},
        {"typedef",  "typedefs are not supported"},
        {"class",    "classes are not supported"},
        {"struct",   "inline 'struct' types are not supported; define the struct separately and use its name"},
        {"unsigned", "multi-token C types are not supported; use u8/u16/u32"},
        {"signed",   "multi-token C types are not supported; use s8/s16/s32"},
        {"int",      "C type 'int' is not supported; use s32"},
        {"char",     "C type 'char' is not supported; use u8 or s8"},
        {"short",    "C type 'short' is not supported; use s16 or u16"},
        {"long",     "C type 'long' is not supported; use s32 or u32"},
        {"float",    "floating-point types are not supported; use fixed"},
        {"double",   "floating-point types are not supported; use fixed"},
        {"const",    "type qualifiers are not supported"},
        {"volatile", "type qualifiers are not supported"},
        {"static",   "storage classes are not supported"},
        {"bool",     "C type 'bool' is not supported; use u8"},
    };
    for (const auto& k : kWords)
        if (w == k.word) return k.what;
    return nullptr;
}

bool IsReservedName(const std::string& n)
{
    FieldKind k;
    return n == "struct" || n == "view" || n == "packed" || n == "void" ||
           LookupScalar(n, k) || UnsupportedWord(n) != nullptr;
}

uint32_t KindAlign(FieldKind k)
{
    switch (k)
    {
    case FieldKind::U8: case FieldKind::S8: return 1;
    case FieldKind::U16: case FieldKind::S16: case FieldKind::RGB555: return 2;
    default: return 4;   // 32-bit scalars, fixed, pointers
    }
}

bool IsSignedKind(FieldKind k)
{
    return k == FieldKind::S8 || k == FieldKind::S16 || k == FieldKind::S32;
}

// ---------------------------------------------------------------------------------------------
// Small text helpers
// ---------------------------------------------------------------------------------------------

std::string Trim(const std::string& s)
{
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    return s.substr(b, e - b);
}

std::string Hex(uint64_t v, int minDigits = 1)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%0*llX", minDigits, (unsigned long long)v);
    return buf;
}

bool IsIdentStart(unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
bool IsDigit(unsigned char c) { return c >= '0' && c <= '9'; }
bool IsIdentChar(unsigned char c) { return IsIdentStart(c) || IsDigit(c); }

int HexVal(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Unsigned magnitude in 'base' (or 0x-prefixed hex when allowPrefix). Saturates at kSat so a
// huge literal is just "too big" to the caller, never an overflow.
constexpr uint64_t kSat = 1ull << 48;

bool ParseMagnitude(const std::string& s, int base, bool allowPrefix, uint64_t& out)
{
    size_t i = 0;
    if (allowPrefix && s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
    {
        base = 16;
        i = 2;
    }
    if (i >= s.size()) return false;
    uint64_t v = 0;
    for (; i < s.size(); ++i)
    {
        const int d = HexVal((unsigned char)s[i]);
        if (d < 0 || d >= base) return false;
        v = v * (uint64_t)base + (uint64_t)d;
        if (v > kSat) v = kSat;
    }
    out = v;
    return true;
}

// ---------------------------------------------------------------------------------------------
// Tokenizer
// ---------------------------------------------------------------------------------------------

enum class TokType { Ident, Number, Punct, End };

struct Tok
{
    TokType     type = TokType::End;
    std::string text;
    int         line = 0;
    size_t      begin = 0, end = 0;   // offsets into the comment-stripped text
};

constexpr size_t kMaxTextBytes = 4u << 20;

// Replace comments with spaces (newlines kept so line numbers stay true). Comments never carry
// meaning: pasted C headers say anything in them, so none may change the layout or raise an error.
bool StripComments(const std::string& in, std::string& out, int& errLine)
{
    out = in;
    int line = 1;
    const size_t n = in.size();
    for (size_t i = 0; i < n;)
    {
        const char c = in[i];
        if (c == '\n') { ++line; ++i; continue; }
        if (c == '/' && i + 1 < n && in[i + 1] == '/')
        {
            size_t j = i + 2;
            while (j < n && in[j] != '\n') ++j;
            for (size_t k = i; k < j; ++k) out[k] = ' ';
            i = j;
            continue;
        }
        if (c == '/' && i + 1 < n && in[i + 1] == '*')
        {
            const int startLine = line;
            size_t j = i + 2;
            while (j + 1 < n && !(in[j] == '*' && in[j + 1] == '/'))
            {
                if (in[j] == '\n') ++line;
                ++j;
            }
            if (j + 1 >= n)
            {
                errLine = startLine;
                return false;
            }
            for (size_t k = i; k < j + 2; ++k)
                if (out[k] != '\n') out[k] = ' ';
            i = j + 2;
            continue;
        }
        ++i;
    }
    return true;
}

std::vector<Tok> Tokenize(const std::string& s)
{
    std::vector<Tok> toks;
    int line = 1;
    const size_t n = s.size();
    for (size_t i = 0; i < n;)
    {
        const unsigned char c = (unsigned char)s[i];
        if (c == '\n') { ++line; ++i; continue; }
        if (c == ' ' || c == '\t' || c == '\r') { ++i; continue; }
        Tok t;
        t.line = line;
        t.begin = i;
        if (IsIdentStart(c))
        {
            size_t j = i + 1;
            while (j < n && IsIdentChar((unsigned char)s[j])) ++j;
            t.type = TokType::Ident;
            t.text = s.substr(i, j - i);
            i = j;
        }
        else if (IsDigit(c))
        {
            size_t j = i + 1;
            while (j < n && IsIdentChar((unsigned char)s[j])) ++j;
            t.type = TokType::Number;
            t.text = s.substr(i, j - i);
            i = j;
        }
        else
        {
            t.type = TokType::Punct;
            t.text = std::string(1, (char)c);
            ++i;
        }
        t.end = i;
        toks.push_back(std::move(t));
    }
    return toks;
}

// ---------------------------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------------------------

uint32_t RoundUp(uint64_t v, uint32_t a) { return (uint32_t)((v + a - 1) / a * a); }

struct Pending
{
    StructDef         def;
    std::vector<int>  fieldLines;
    uint64_t          declared = 0;
    bool              hasDeclared = false;
};

std::string BaseTypeText(const FieldDef& f)
{
    switch (f.kind)
    {
    case FieldKind::Struct:  return f.structName;
    case FieldKind::Pointer:
        if (!f.structName.empty()) return f.structName + "*";
        if (f.pointee != FieldKind::Struct) return std::string(FieldKindName(f.pointee)) + "*";
        return "void*";
    default: return FieldKindName(f.kind);
    }
}

const char* AttrName(FieldFormat f)
{
    switch (f)
    {
    case FieldFormat::Hex:   return "hex";
    case FieldFormat::Dec:   return "dec";
    case FieldFormat::Fixed: return "fixed";
    default: return "";
    }
}

// ---------------------------------------------------------------------------------------------
// Field value formatting
// ---------------------------------------------------------------------------------------------

enum class Eff { Hex, Dec, Fixed, Rgb, Pointer };

Eff EffectiveFormat(FieldKind k, FieldFormat fmt)
{
    const bool is32 = (k == FieldKind::U32 || k == FieldKind::S32 || k == FieldKind::Fixed16_16);
    switch (k)
    {
    case FieldKind::Fixed16_16:
        return (fmt == FieldFormat::Hex) ? Eff::Hex : (fmt == FieldFormat::Dec) ? Eff::Dec : Eff::Fixed;
    case FieldKind::RGB555:
        return (fmt == FieldFormat::Hex) ? Eff::Hex : (fmt == FieldFormat::Dec) ? Eff::Dec : Eff::Rgb;
    case FieldKind::Pointer:
        return (fmt == FieldFormat::Dec) ? Eff::Dec : (fmt == FieldFormat::Hex) ? Eff::Hex : Eff::Pointer;
    default:
        break;
    }
    if (fmt == FieldFormat::Fixed && is32) return Eff::Fixed;
    if (fmt == FieldFormat::Hex) return Eff::Hex;
    if (fmt == FieldFormat::Dec) return Eff::Dec;
    return IsSignedKind(k) ? Eff::Dec : Eff::Hex;
}

// Exact decimal expansion of a 16.16 value: 1/65536 has 16 decimal digits, so nothing is lost
// and EncodeField reads it back to the same raw. 'digits' < 16 rounds for display instead.
std::string FixedText(int32_t raw, int digits)
{
    const bool neg = raw < 0;
    uint64_t mag = neg ? (uint64_t)(-(int64_t)raw) : (uint64_t)raw;
    uint64_t ip = mag >> 16;
    const uint64_t frac = mag & 0xFFFF;
    uint64_t scaled;
    uint64_t scale = 1;
    for (int i = 0; i < digits; ++i) scale *= 10;
    if (digits >= 16)
        scaled = frac * 152587890625ull;   // 10^16 / 65536, exact
    else
        scaled = (frac * scale + 32768) >> 16;
    if (scaled >= scale) { scaled -= scale; ++ip; }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s%llu.%0*llu", neg ? "-" : "", (unsigned long long)ip,
                  digits, (unsigned long long)scaled);
    std::string s = buf;
    while (s.size() > 2 && s.back() == '0' && s[s.size() - 2] != '.') s.pop_back();
    return s;
}

bool ParseFixedDecimal(const std::string& t, int32_t& raw, std::string& err)
{
    size_t i = 0;
    bool neg = false;
    if (i < t.size() && (t[i] == '-' || t[i] == '+')) { neg = (t[i] == '-'); ++i; }
    uint64_t ip = 0;
    int nInt = 0, nFrac = 0;
    uint64_t F = 0;
    for (; i < t.size() && IsDigit((unsigned char)t[i]); ++i, ++nInt)
        ip = (std::min)(ip * 10 + (uint64_t)(t[i] - '0'), (uint64_t)1000000);
    if (i < t.size() && t[i] == '.')
    {
        ++i;
        for (; i < t.size() && IsDigit((unsigned char)t[i]); ++i, ++nFrac)
            if (nFrac < 9) F = F * 10 + (uint64_t)(t[i] - '0');
        // Pad a short fraction out to 9 digits so F is always in units of 1e-9.
        for (int k = (std::min)(nFrac, 9); k < 9; ++k) F *= 10;
    }
    if (i != t.size() || (nInt == 0 && nFrac == 0))
    {
        err = "'" + t + "' is not a fixed-point number (try 1.5, -0.25 or 0x00018000)";
        return false;
    }
    const int64_t total = (int64_t)ip * 65536 + (int64_t)((F * 65536ull + 500000000ull) / 1000000000ull);
    const int64_t v = neg ? -total : total;
    if (v < -2147483648LL || v > 2147483647LL)
    {
        err = "'" + t + "' is out of range for fixed (-32768 to 32767.99998)";
        return false;
    }
    raw = (int32_t)v;
    return true;
}

void PutBE(uint8_t* out, uint32_t v, uint32_t size)
{
    for (uint32_t i = 0; i < size; ++i)
        out[i] = (uint8_t)(v >> (8 * (size - 1 - i)));
}

uint32_t GetBE(const uint8_t* p, uint32_t size)
{
    uint32_t v = 0;
    for (uint32_t i = 0; i < size; ++i) v = (v << 8) | p[i];
    return v;
}

}  // namespace

const char* FieldKindName(FieldKind k)
{
    for (const ScalarName& s : kScalarNames)
        if (s.kind == k && k != FieldKind::Pointer && k != FieldKind::Struct) return s.name;
    return "";
}

uint32_t FieldKindSize(FieldKind k)
{
    switch (k)
    {
    case FieldKind::U8: case FieldKind::S8: return 1;
    case FieldKind::U16: case FieldKind::S16: case FieldKind::RGB555: return 2;
    case FieldKind::U32: case FieldKind::S32: case FieldKind::Fixed16_16: case FieldKind::Pointer: return 4;
    default: return 0;
    }
}

std::string FieldTypeText(const FieldDef& f)
{
    std::string s = BaseTypeText(f);
    if (f.isArray) s += "[" + std::to_string(f.count) + "]";
    return s;
}

// ---------------------------------------------------------------------------------------------
// Decode / encode
// ---------------------------------------------------------------------------------------------

bool DecodeField(const FieldDef& f, const uint8_t* bytes, StructValue& out)
{
    return DecodeField(f, f.fmt, bytes, out);
}

bool DecodeField(const FieldDef& f, FieldFormat fmt, const uint8_t* bytes, StructValue& out)
{
    out = StructValue();
    const uint32_t size = FieldKindSize(f.kind);
    if (size == 0 || bytes == nullptr) return false;

    const uint32_t raw = GetBE(bytes, size);
    int64_t num = raw;
    switch (f.kind)
    {
    case FieldKind::S8:  num = (int8_t)raw; break;
    case FieldKind::S16: num = (int16_t)raw; break;
    case FieldKind::S32: case FieldKind::Fixed16_16: num = (int32_t)raw; break;
    default: break;
    }
    out.valid = true;
    out.raw = raw;
    out.numeric = (f.kind == FieldKind::RGB555) ? 0 : num;

    const int digits = (int)size * 2;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "0x%0*X", digits, raw);
    const std::string hexText = buf;
    std::snprintf(buf, sizeof(buf), "%0*X", digits, raw);
    const std::string bareHex = buf;
    std::snprintf(buf, sizeof(buf), "%lld", (long long)num);
    const std::string decText = buf;

    const Eff e = EffectiveFormat(f.kind, fmt);
    switch (e)
    {
    case Eff::Hex:
        out.text = bareHex; out.editText = hexText; out.tooltip = hexText + " (" + decText + ")";
        break;
    case Eff::Dec:
        out.text = decText; out.editText = decText; out.tooltip = hexText;
        break;
    case Eff::Fixed:
        out.text = FixedText((int32_t)raw, 6);
        out.editText = FixedText((int32_t)raw, 16);
        out.tooltip = "raw " + hexText;
        break;
    case Eff::Rgb:
    {
        const uint16_t w = (uint16_t)raw;
        out.r = (uint8_t)((w & 0x1F) * 255 / 31);
        out.g = (uint8_t)(((w >> 5) & 0x1F) * 255 / 31);
        out.b = (uint8_t)(((w >> 10) & 0x1F) * 255 / 31);
        out.hasSwatch = true;
        std::snprintf(buf, sizeof(buf), "R%d G%d B%d", w & 0x1F, (w >> 5) & 0x1F, (w >> 10) & 0x1F);
        out.text = buf;
        // Bit 15 has no R G B spelling; fall back to the word so editing never drops it.
        if (w & 0x8000) std::snprintf(buf, sizeof(buf), "0x%04X", w);
        else std::snprintf(buf, sizeof(buf), "%d %d %d", w & 0x1F, (w >> 5) & 0x1F, (w >> 10) & 0x1F);
        out.editText = buf;
        out.tooltip = hexText;
        break;
    }
    case Eff::Pointer:
        out.text = raw == 0 ? std::string("null") : "-> " + hexText;
        out.editText = hexText;
        out.tooltip = hexText;
        break;
    }
    if (f.kind == FieldKind::Pointer)
    {
        out.isPointer = true;
        out.pointerTarget = raw;
    }
    return true;
}

bool EncodeField(const FieldDef& f, FieldFormat fmt, const std::string& textIn,
                 uint8_t out[4], std::string& err)
{
    err.clear();
    const uint32_t size = FieldKindSize(f.kind);
    if (size == 0) { err = "This field cannot be edited."; return false; }
    const std::string t = Trim(textIn);
    if (t.empty()) { err = "Enter a value."; return false; }

    const Eff e = EffectiveFormat(f.kind, fmt);
    const bool hasSign = (t[0] == '-' || t[0] == '+');
    const bool neg = (t[0] == '-');
    const std::string body = hasSign ? t.substr(1) : t;
    const bool prefixed = body.size() >= 2 && body[0] == '0' && (body[1] == 'x' || body[1] == 'X');

    if (e == Eff::Fixed)
    {
        int32_t raw = 0;
        if (prefixed)
        {
            uint64_t v;
            if (hasSign || !ParseMagnitude(body, 16, true, v) || body.size() > 10)
            {
                err = "'" + t + "' is not a raw 16.16 word (up to 8 hex digits, e.g. 0x00018000)";
                return false;
            }
            raw = (int32_t)(uint32_t)v;
        }
        else if (!ParseFixedDecimal(t, raw, err))
            return false;
        PutBE(out, (uint32_t)raw, 4);
        return true;
    }

    if (e == Eff::Rgb && !prefixed)
    {
        int vals[4];
        int n = 0;
        for (size_t i = 0; i < t.size();)
        {
            const unsigned char c = (unsigned char)t[i];
            if (IsDigit(c))
            {
                int v = 0;
                while (i < t.size() && IsDigit((unsigned char)t[i]))
                {
                    v = (std::min)(v * 10 + (t[i] - '0'), 1000);
                    ++i;
                }
                if (n < 4) vals[n] = v;
                ++n;
            }
            else if (c == ' ' || c == ',' || c == '\t' || c == 'R' || c == 'G' || c == 'B' ||
                     c == 'r' || c == 'g' || c == 'b')
                ++i;
            else
            {
                err = "'" + t + "' is not a colour: enter R G B (0-31 each) or a 0x word";
                return false;
            }
        }
        if (n != 3)
        {
            err = "Enter three values, R G B (0-31 each), or a 0x word";
            return false;
        }
        for (int i = 0; i < 3; ++i)
            if (vals[i] > 31)
            {
                err = "Colour components are 0-31 (got " + std::to_string(vals[i]) + ")";
                return false;
            }
        PutBE(out, (uint32_t)(vals[0] | (vals[1] << 5) | (vals[2] << 10)), 2);
        return true;
    }

    // Integer path. Bare digits follow the displayed base; a 0x prefix is always hex.
    const bool hexBase = prefixed || e == Eff::Hex || e == Eff::Pointer || e == Eff::Rgb;
    uint64_t mag;
    if (!ParseMagnitude(body, hexBase ? 16 : 10, true, mag))
    {
        err = "'" + t + "' is not a valid " + (hexBase ? "hex" : "decimal") + " number";
        return false;
    }

    const uint32_t bits = size * 8;
    const uint64_t umax = (bits == 32) ? 0xFFFFFFFFull : ((1ull << bits) - 1);
    const char* name = f.kind == FieldKind::Pointer ? "a pointer" : FieldKindName(f.kind);
    char range[96];
    uint32_t value = 0;
    if (IsSignedKind(f.kind) || (f.kind == FieldKind::Fixed16_16))
    {
        const int64_t smin = -(int64_t)(1ull << (bits - 1));
        const int64_t smax = (int64_t)((1ull << (bits - 1)) - 1);
        std::snprintf(range, sizeof(range), "%lld..%lld", (long long)smin, (long long)smax);
        if (neg)
        {
            if (mag > (1ull << (bits - 1)))
            {
                err = "'" + t + "' is out of range for " + name + " (" + range + ")";
                return false;
            }
            value = (uint32_t)(0 - mag);
        }
        else
        {
            // A hex literal is a bit pattern ("FFFF" is -1); decimal must fit the signed range.
            const uint64_t lim = hexBase ? umax : (uint64_t)smax;
            if (mag > lim)
            {
                err = "'" + t + "' is out of range for " + name + " (" + range + ")";
                return false;
            }
            value = (uint32_t)mag;
        }
    }
    else
    {
        std::snprintf(range, sizeof(range), "0..%llu", (unsigned long long)umax);
        if (neg && mag != 0)
        {
            err = "'" + t + "' is out of range for " + name + ": negative values are not allowed (" + range + ")";
            return false;
        }
        if (mag > umax)
        {
            err = "'" + t + "' is out of range for " + name + " (" + range + ")";
            return false;
        }
        value = (uint32_t)mag;
    }
    PutBE(out, value, size);
    return true;
}

// ---------------------------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------------------------

namespace
{

constexpr size_t kMaxErrors = 100;
constexpr size_t kMaxNameLen = 64;

class Parser
{
public:
    Parser(const std::string& text, std::vector<ParseError>& errs) : mErrs(errs), mText(text) {}

    bool Run(std::vector<Pending>& structs, std::vector<StructView>& views,
             std::vector<int>& viewLines)
    {
        if (mText.size() > kMaxTextBytes)
        {
            Err(0, "definition text is larger than " + std::to_string(kMaxTextBytes >> 20) + " MiB");
            return false;
        }
        int errLine = 0;
        if (!StripComments(mText, mClean, errLine))
        {
            Err(errLine, "unterminated /* comment");
            return false;
        }
        mToks = Tokenize(mClean);

        while (Peek().type != TokType::End)
        {
            const Tok t = Peek();
            if (t.type == TokType::Ident && t.text == "struct") ParseStruct(structs);
            else if (t.type == TokType::Ident && t.text == "view") ParseView(views, viewLines);
            else if (t.type == TokType::Punct && t.text == "#")
            {
                Err(t.line, "the preprocessor is not supported ('#' directives)");
                SkipLine(t.line);
            }
            else
            {
                const char* what = t.type == TokType::Ident ? UnsupportedWord(t.text) : nullptr;
                if (what) Err(t.line, what);
                else Err(t.line, "expected 'struct' or 'view' but found '" + t.text + "'");
                SkipStatement();
            }
            if (mErrs.size() > kMaxErrors) break;
        }
        return mErrs.empty();
    }

private:
    const Tok& Peek(size_t k = 0) const
    {
        static const Tok kEnd;
        return mPos + k < mToks.size() ? mToks[mPos + k] : kEnd;
    }
    Tok Next()
    {
        Tok t = Peek();
        if (mPos < mToks.size()) ++mPos;
        return t;
    }
    bool IsPunct(char c, size_t k = 0) const
    {
        const Tok& t = Peek(k);
        return t.type == TokType::Punct && t.text[0] == c;
    }
    void Err(int line, const std::string& msg)
    {
        if (mErrs.size() == kMaxErrors) { mErrs.push_back({line, "too many errors; stopping"}); return; }
        if (mErrs.size() < kMaxErrors) mErrs.push_back({line, msg});
    }

    void SkipLine(int line)
    {
        while (Peek().type != TokType::End && Peek().line == line) Next();
    }

    // Skip one top-level statement: through its ';' or its balanced '{ ... }' (and a trailing ';').
    void SkipStatement()
    {
        int depth = 0;
        bool first = true;
        while (Peek().type != TokType::End)
        {
            const Tok t = Next();
            first = false;
            if (t.type != TokType::Punct) continue;
            if (t.text[0] == '{') ++depth;
            else if (t.text[0] == '}')
            {
                if (--depth <= 0) { if (IsPunct(';')) Next(); return; }
            }
            else if (t.text[0] == ';' && depth == 0) return;
        }
        (void)first;
    }

    // Skip the rest of a broken field: through its ';', stopping before the struct's '}'.
    void SkipField()
    {
        int depth = 0;
        while (Peek().type != TokType::End)
        {
            if (IsPunct('}') && depth == 0) return;
            const Tok t = Next();
            if (t.type != TokType::Punct) continue;
            if (t.text[0] == '{') ++depth;
            else if (t.text[0] == '}') --depth;
            else if (t.text[0] == ';' && depth == 0) return;
        }
    }

    bool CheckName(const Tok& t, const char* what)
    {
        if (t.text.size() > kMaxNameLen)
        {
            Err(t.line, std::string(what) + " name is longer than " + std::to_string(kMaxNameLen) + " characters");
            return false;
        }
        return true;
    }

    bool Number32(const Tok& t, uint32_t& out, const char* what)
    {
        uint64_t v;
        if (t.type != TokType::Number || !ParseMagnitude(t.text, 10, true, v) || v > 0xFFFFFFFFull)
        {
            Err(t.line, std::string("invalid ") + what + " '" + t.text + "'");
            return false;
        }
        out = (uint32_t)v;
        return true;
    }

    void ParseStruct(std::vector<Pending>& structs)
    {
        const Tok kw = Next();
        Pending p;
        p.def.line = kw.line;
        const size_t errsAtStart = mErrs.size();

        const Tok nameTok = Peek();
        if (nameTok.type != TokType::Ident)
        {
            Err(kw.line, "expected a struct name after 'struct'");
            SkipStatement();
            return;
        }
        Next();
        p.def.name = nameTok.text;
        if (!CheckName(nameTok, "struct")) {}
        else if (IsReservedName(nameTok.text))
            Err(nameTok.line, "'" + nameTok.text + "' is a reserved word and cannot name a struct");
        else
            for (const Pending& o : structs)
                if (o.def.name == nameTok.text)
                {
                    Err(nameTok.line, "struct '" + nameTok.text + "' is already defined (line " +
                                          std::to_string(o.def.line) + ")");
                    break;
                }

        // Modifiers, either order, each at most once: "packed" and "size(N)".
        bool sawPacked = false;
        while (Peek().type == TokType::Ident && (Peek().text == "packed" || Peek().text == "size"))
        {
            const Tok m = Next();
            if (m.text == "packed")
            {
                if (sawPacked) Err(m.line, "'packed' given twice");
                sawPacked = true;
                p.def.packed = true;
                continue;
            }
            if (p.hasDeclared) Err(m.line, "'size(...)' given twice");
            uint32_t v = 0;
            if (!IsPunct('(') || Peek(1).type != TokType::Number || !IsPunct(')', 2))
            {
                Err(m.line, "expected size(N), e.g. size(0x40)");
                SkipStatement();
                return;
            }
            Next();
            if (!Number32(Next(), v, "struct size")) { SkipStatement(); return; }
            Next();
            p.hasDeclared = true;
            p.declared = v;
        }
        if (IsPunct(':'))
        {
            Err(Peek().line, "inheritance is not supported");
            SkipStatement();
            return;
        }
        if (!IsPunct('{'))
        {
            Err(Peek().type == TokType::End ? kw.line : Peek().line,
                "expected '{' after struct '" + nameTok.text + "'");
            SkipStatement();
            return;
        }
        Next();

        bool tooMany = false;
        for (;;)
        {
            const Tok& t = Peek();
            if (t.type == TokType::End)
            {
                Err(kw.line, "struct '" + nameTok.text + "' is missing its closing '}'");
                break;
            }
            if (IsPunct('}')) { Next(); break; }
            if (IsPunct(';')) { Next(); continue; }
            if (p.def.fields.size() >= kMaxFields)
            {
                if (!tooMany) Err(t.line, "struct '" + nameTok.text + "' has more than " + std::to_string(kMaxFields) + " fields");
                tooMany = true;
                SkipField();
                if (IsPunct('}')) { Next(); break; }
                continue;
            }
            const size_t before = mPos;
            if (!ParseField(p)) SkipField();
            if (mPos == before) Next();   // never stall on a token nothing consumed
            if (mErrs.size() > kMaxErrors) return;
        }
        if (IsPunct(';')) Next();

        if (p.def.fields.empty() && mErrs.size() == errsAtStart)
            Err(kw.line, "struct '" + nameTok.text + "' has no fields");
        structs.push_back(std::move(p));
    }

    bool ParseField(Pending& p)
    {
        StructDef& def = p.def;
        FieldDef f;
        const int line = Peek().line;

        if (IsPunct('['))
        {
            if (!(IsPunct('[', 1) && Peek(2).type == TokType::Ident && IsPunct(']', 3) && IsPunct(']', 4)))
            {
                Err(line, "malformed display attribute; write [[hex]], [[dec]] or [[fixed]]");
                return false;
            }
            const std::string a = Peek(2).text;
            if (a == "hex") f.fmt = FieldFormat::Hex;
            else if (a == "dec") f.fmt = FieldFormat::Dec;
            else if (a == "fixed") f.fmt = FieldFormat::Fixed;
            else
            {
                Err(line, "unknown display attribute '" + a + "'; use hex, dec or fixed");
                return false;
            }
            for (int i = 0; i < 5; ++i) Next();
        }

        const Tok typeTok = Peek();
        if (typeTok.type == TokType::Punct)
        {
            if (typeTok.text[0] == '#') Err(typeTok.line, "the preprocessor is not supported ('#' directives)");
            else if (typeTok.text[0] == '{') Err(typeTok.line, "anonymous nested structs and unions are not supported");
            else if (typeTok.text[0] == ':') Err(typeTok.line, "bitfields are not supported");
            else Err(typeTok.line, "expected a field type but found '" + typeTok.text + "'");
            return false;
        }
        if (typeTok.type != TokType::Ident)
        {
            Err(typeTok.line == 0 ? line : typeTok.line, "expected a field type");
            return false;
        }
        if (const char* what = UnsupportedWord(typeTok.text))
        {
            Err(typeTok.line, what);
            return false;
        }
        Next();

        FieldKind scalar;
        const bool isScalar = LookupScalar(typeTok.text, scalar);
        const bool isVoid = typeTok.text == "void";
        if (!CheckName(typeTok, "type")) return false;

        if (IsPunct('*'))
        {
            Next();
            if (IsPunct('*'))
            {
                Err(Peek().line, "pointers to pointers are not supported; use void*");
                return false;
            }
            f.kind = FieldKind::Pointer;
            if (isVoid) {}
            else if (isScalar) f.pointee = scalar;
            else if (typeTok.text == "struct" || IsReservedName(typeTok.text))
            {
                Err(typeTok.line, "'" + typeTok.text + "' cannot be a pointer target");
                return false;
            }
            else f.structName = typeTok.text;
        }
        else if (isVoid)
        {
            Err(typeTok.line, "'void' is only valid as a pointer (void*)");
            return false;
        }
        else if (isScalar) f.kind = scalar;
        else if (IsReservedName(typeTok.text))
        {
            Err(typeTok.line, "'" + typeTok.text + "' is not a type");
            return false;
        }
        else
        {
            f.kind = FieldKind::Struct;
            f.structName = typeTok.text;
        }

        const Tok nameTok = Peek();
        if (nameTok.type != TokType::Ident)
        {
            Err(nameTok.type == TokType::End ? line : nameTok.line,
                nameTok.type == TokType::End ? "expected a field name"
                                             : "expected a field name but found '" + nameTok.text + "'");
            return false;
        }
        if (UnsupportedWord(nameTok.text) != nullptr)
        {
            Err(nameTok.line, "multi-token C types are not supported ('" + typeTok.text + " " + nameTok.text +
                                  "'); use u8/s8/u16/s16/u32/s32/fixed");
            return false;
        }
        Next();
        if (Peek().type == TokType::Ident)
        {
            Err(nameTok.line, "multi-token C types are not supported ('" + typeTok.text + " " + nameTok.text + " " +
                                  Peek().text + "'); use u8/s8/u16/s16/u32/s32/fixed");
            return false;
        }
        if (!CheckName(nameTok, "field")) return false;
        f.name = nameTok.text;

        if (IsPunct(':'))
        {
            Err(Peek().line, "bitfields are not supported");
            return false;
        }
        if (IsPunct('['))
        {
            Next();
            uint32_t n = 0;
            if (Peek().type != TokType::Number)
            {
                Err(Peek().line == 0 ? line : Peek().line, "expected an array length in '[...]'");
                return false;
            }
            if (!Number32(Next(), n, "array length")) return false;
            if (n == 0 || n > kMaxArrayCount)
            {
                Err(line, "array length of '" + f.name + "' must be 1.." + std::to_string(kMaxArrayCount));
                return false;
            }
            if (!IsPunct(']'))
            {
                Err(line, "expected ']' after the array length");
                return false;
            }
            Next();
            f.count = n;
            f.isArray = true;
            if (IsPunct('['))
            {
                Err(line, "multi-dimensional arrays are not supported");
                return false;
            }
        }
        if (IsPunct('@'))
        {
            Next();
            uint32_t off = 0;
            if (!Number32(Next(), off, "offset")) return false;
            f.hasExplicitOffset = true;
            f.offset = off;
        }
        if (!IsPunct(';'))
        {
            const Tok& t = Peek();
            if (t.type == TokType::End || IsPunct('}'))
                Err(line, "missing ';' after field '" + f.name + "'");
            else if (IsPunct(':'))
                Err(t.line, "bitfields are not supported");
            else
                Err(t.line, "expected ';' after field '" + f.name + "' but found '" + t.text + "'");
            return false;
        }
        Next();

        // Attribute/kind compatibility.
        if (f.fmt != FieldFormat::Default)
        {
            const bool is32 = f.kind == FieldKind::U32 || f.kind == FieldKind::S32 || f.kind == FieldKind::Fixed16_16;
            if (f.kind == FieldKind::Struct)
            {
                Err(line, "a display attribute cannot be applied to struct field '" + f.name + "'");
                return false;
            }
            if (f.fmt == FieldFormat::Fixed && !is32)
            {
                Err(line, "[[fixed]] applies only to 32-bit fields (u32, s32, fixed); '" + f.name + "' is " + FieldTypeText(f));
                return false;
            }
        }
        for (const FieldDef& o : def.fields)
            if (o.name == f.name)
            {
                Err(line, "duplicate field name '" + f.name + "' in struct '" + def.name + "'");
                return false;
            }
        def.fields.push_back(std::move(f));
        p.fieldLines.push_back(line);
        return true;
    }

    void ParseView(std::vector<StructView>& views, std::vector<int>& viewLines)
    {
        const Tok kw = Next();
        const int line = kw.line;
        const size_t eq = mClean.find('=', kw.end);
        const size_t semi = mClean.find(';', kw.end);
        if (eq == std::string::npos || (semi != std::string::npos && semi < eq))
        {
            Err(line, "expected 'view label = Type[count] @ expression;'");
            SkipStatement();
            return;
        }
        if (semi == std::string::npos)
        {
            Err(line, "missing ';' at the end of the view");
            SkipStatement();
            return;
        }
        StructView v;
        v.label = Trim(mClean.substr(kw.end, eq - kw.end));
        std::string why;
        if (!LabelOk(v.label, why))
        {
            Err(line, "view label " + why);
            SkipToOffset(semi);
            return;
        }
        // Resume tokenising after the '='.
        while (Peek().type != TokType::End && Peek().begin < eq + 1) Next();
        const Tok typeTok = Peek();
        if (typeTok.type != TokType::Ident || typeTok.begin > semi)
        {
            Err(line, "expected a struct name after '='");
            SkipToOffset(semi);
            return;
        }
        Next();
        v.structName = typeTok.text;
        v.count = 1;
        if (IsPunct('['))
        {
            Next();
            if (Peek().type != TokType::Number || !Number32(Next(), v.count, "view count"))
            {
                if (mErrs.empty() || mErrs.back().line != line) Err(line, "expected a count in '[...]'");
                SkipToOffset(semi);
                return;
            }
            if (!IsPunct(']'))
            {
                Err(line, "expected ']' after the view count");
                SkipToOffset(semi);
                return;
            }
            Next();
        }
        if (!IsPunct('@') || Peek().begin > semi)
        {
            Err(line, "expected '@ expression' in the view");
            SkipToOffset(semi);
            return;
        }
        const Tok at = Next();
        std::string expr = mClean.substr(at.end, semi - at.end);
        for (char& c : expr)
            if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        v.expression = Trim(expr);
        if (v.expression.empty())
        {
            Err(line, "the view has no address expression after '@'");
            SkipToOffset(semi);
            return;
        }
        if (v.expression.find_first_of("{}") != std::string::npos)
        {
            Err(line, "unexpected '{' or '}' in the view expression");
            SkipToOffset(semi);
            return;
        }
        SkipToOffset(semi);
        if (views.size() >= kMaxViews)
        {
            Err(line, "more than " + std::to_string(kMaxViews) + " views");
            return;
        }
        views.push_back(std::move(v));
        viewLines.push_back(line);
    }

    void SkipToOffset(size_t semi)
    {
        while (Peek().type != TokType::End && Peek().begin <= semi) Next();
    }

public:
    static bool LabelOk(const std::string& l, std::string& why)
    {
        if (l.empty()) { why = "is empty"; return false; }
        if (l.size() > kMaxNameLen) { why = "is longer than 64 characters"; return false; }
        for (unsigned char c : l)
            if (c < 0x20 || c == '=' || c == ';' || c == '{' || c == '}' || c == '/' || c == 0x7F)
            {
                why = "contains a character not allowed in a label (= ; { } / or a control character)";
                return false;
            }
        return true;
    }

private:
    std::vector<ParseError>&  mErrs;
    const std::string&        mText;
    std::string               mClean;
    std::vector<Tok>          mToks;
    size_t                    mPos = 0;
};

// Lay out struct 'idx' (recursing into by-value members first). Returns false after reporting.
struct LayoutPass
{
    std::vector<Pending>&              structs;
    const std::map<std::string, size_t>& index;
    std::vector<StructLayout>&         layouts;
    std::vector<ParseError>&           errs;
    std::vector<int>                   state;   // 0 new, 1 in progress, 2 done, 3 failed
    std::vector<size_t>                stack;

    void Err(int line, const std::string& m) { if (errs.size() < kMaxErrors) errs.push_back({line, m}); }

    bool Do(size_t idx)
    {
        if (state[idx] == 2) return true;
        if (state[idx] == 3) return false;
        state[idx] = 1;
        stack.push_back(idx);
        const bool ok = Compute(idx);
        stack.pop_back();
        state[idx] = ok ? 2 : 3;
        return ok;
    }

    bool Compute(size_t idx)
    {
        Pending& p = structs[idx];
        StructDef& def = p.def;
        StructLayout& L = layouts[idx];
        L.fields.assign(def.fields.size(), FieldLayout());
        uint64_t cursor = 0;
        uint32_t align = 1;
        for (size_t i = 0; i < def.fields.size(); ++i)
        {
            const FieldDef& f = def.fields[i];
            const int line = p.fieldLines[i];
            uint32_t elemSize = 0, elemAlign = 1;
            if (f.kind == FieldKind::Struct)
            {
                auto it = index.find(f.structName);
                if (it == index.end())
                {
                    Err(line, "unknown type '" + f.structName + "' for field '" + f.name + "'");
                    return false;
                }
                if (state[it->second] == 1)
                {
                    std::string path;
                    bool on = false;
                    for (size_t s : stack)
                    {
                        if (s == it->second) on = true;
                        if (on) path += structs[s].def.name + " -> ";
                    }
                    path += f.structName;
                    Err(line, "struct '" + def.name + "' contains itself by value (" + path +
                                  "); use a pointer for recursive types");
                    return false;
                }
                if (!Do(it->second)) return false;
                elemSize = layouts[it->second].size;
                elemAlign = layouts[it->second].align;
            }
            else
            {
                if (f.kind == FieldKind::Pointer && !f.structName.empty() && index.find(f.structName) == index.end())
                {
                    Err(line, "unknown type '" + f.structName + "' for pointer field '" + f.name + "'");
                    return false;
                }
                elemSize = FieldKindSize(f.kind);
                elemAlign = KindAlign(f.kind);
            }
            if (def.packed) elemAlign = 1;

            uint64_t off;
            if (f.hasExplicitOffset)
            {
                off = f.offset;
                if (off < cursor)
                {
                    Err(line, "field '" + f.name + "' at " + Hex(off) + " overlaps or precedes the previous field (which ends at " +
                                  Hex(cursor) + ")");
                    return false;
                }
            }
            else
                off = RoundUp(cursor, elemAlign);
            const uint64_t total = (uint64_t)elemSize * f.count;
            const uint64_t end = off + total;
            if (end > kMaxStructSize)
            {
                Err(line, "struct '" + def.name + "' would exceed the maximum size of " + Hex(kMaxStructSize));
                return false;
            }
            L.fields[i].offset = (uint32_t)off;
            L.fields[i].elemSize = elemSize;
            L.fields[i].size = (uint32_t)total;
            L.fields[i].align = elemAlign;
            cursor = end;
            align = (std::max)(align, elemAlign);
        }
        const uint64_t natural = def.packed ? cursor : RoundUp(cursor, align);
        uint64_t size = natural;
        def.minSize = 0;
        if (p.hasDeclared)
        {
            if (p.declared < natural)
            {
                Err(def.line, "declared size " + Hex(p.declared) + " of struct '" + def.name +
                                  "' is smaller than its layout (" + Hex(natural) + ")");
                return false;
            }
            if (p.declared > kMaxStructSize)
            {
                Err(def.line, "declared size of struct '" + def.name + "' exceeds the maximum of " + Hex(kMaxStructSize));
                return false;
            }
            if (p.declared > natural) { size = p.declared; def.minSize = (uint32_t)p.declared; }
        }
        if (size > kMaxStructSize)
        {
            Err(def.line, "struct '" + def.name + "' would exceed the maximum size of " + Hex(kMaxStructSize));
            return false;
        }
        L.size = (uint32_t)size;
        L.align = align;
        def.size = L.size;
        def.align = align;
        return true;
    }
};

bool ExprOk(const std::string& e, std::string& why)
{
    if (e.empty() || e != Trim(e)) { why = "The address expression must be non-empty and have no surrounding spaces."; return false; }
    if (e.find_first_of(";{}\n\r") != std::string::npos || e.find("//") != std::string::npos ||
        e.find("/*") != std::string::npos)
    {
        why = "The address expression cannot contain ';', braces, comments or line breaks.";
        return false;
    }
    return true;
}

}  // namespace

bool StructLibrary::Parse(const std::string& text, std::vector<ParseError>& errors)
{
    errors.clear();
    // A UTF-8 byte-order mark (Windows editors) is not part of the text.
    const bool bom = text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
                     (unsigned char)text[2] == 0xBF;
    const std::string stripped = bom ? text.substr(3) : std::string();
    std::vector<Pending> pend;
    std::vector<StructView> views;
    std::vector<int> viewLines;
    Parser parser(bom ? stripped : text, errors);
    parser.Run(pend, views, viewLines);

    // Semantic checks only on text that is syntactically sound, so a broken definition does not
    // also report every use of it as an unknown type.
    std::vector<StructLayout> layouts(pend.size());
    std::map<std::string, size_t> index;
    if (errors.empty())
    {
        if (pend.size() > kMaxStructs)
            errors.push_back({0, "more than " + std::to_string(kMaxStructs) + " structs"});
        for (size_t i = 0; i < pend.size(); ++i) index[pend[i].def.name] = i;

        LayoutPass pass{pend, index, layouts, errors, std::vector<int>(pend.size(), 0), {}};
        for (size_t i = 0; i < pend.size(); ++i) pass.Do(i);

        if (errors.empty())
            for (size_t i = 0; i < views.size(); ++i)
            {
                const StructView& v = views[i];
                auto it = index.find(v.structName);
                if (it == index.end())
                {
                    errors.push_back({viewLines[i], "view '" + v.label + "' uses unknown struct '" + v.structName + "'"});
                    continue;
                }
                if (v.count == 0 || v.count > kMaxViewCount ||
                    (uint64_t)v.count * layouts[it->second].size > 0x100000000ull)
                    errors.push_back({viewLines[i], "view '" + v.label + "' has an invalid count (1.." +
                                                        std::to_string(kMaxViewCount) + ", and count x size must fit 32 bits)"});
            }
    }
    if (!errors.empty()) return false;

    // Commit: nothing above touched the live library.
    mStructs.clear();
    mIndex.clear();
    for (size_t i = 0; i < pend.size(); ++i)
    {
        mIndex[pend[i].def.name] = i;
        mStructs.push_back(std::move(pend[i].def));
    }
    mLayouts = std::move(layouts);
    mViews = std::move(views);
    for (StructView& v : mViews) v.id = mNextViewId++;
    ++mGeneration;
    ++mViewsGeneration;
    return true;
}

std::string StructLibrary::Emit() const
{
    std::string out;
    for (size_t i = 0; i < mStructs.size(); ++i)
    {
        const StructDef& d = mStructs[i];
        const StructLayout& L = mLayouts[i];
        if (!out.empty()) out += "\n";
        out += "struct " + d.name + (d.packed ? " packed" : "") + " size(" + Hex(d.size) + ") {\n";
        for (size_t k = 0; k < d.fields.size(); ++k)
        {
            const FieldDef& f = d.fields[k];
            out += "    ";
            if (f.fmt != FieldFormat::Default) out += std::string("[[") + AttrName(f.fmt) + "]] ";
            out += BaseTypeText(f) + " " + f.name;
            if (f.isArray) out += "[" + std::to_string(f.count) + "]";
            out += " @" + Hex(L.fields[k].offset, 2) + ";\n";
        }
        out += "};\n";
    }
    if (!mViews.empty() && !out.empty()) out += "\n";
    for (const StructView& v : mViews)
        out += "view " + v.label + " = " + v.structName + "[" + std::to_string(v.count) + "] @ " + v.expression + ";\n";
    return out;
}

void StructLibrary::Clear()
{
    mStructs.clear();
    mLayouts.clear();
    mIndex.clear();
    mViews.clear();
    ++mGeneration;
    ++mViewsGeneration;
}

const StructDef* StructLibrary::Find(const std::string& name) const
{
    auto it = mIndex.find(name);
    return it == mIndex.end() ? nullptr : &mStructs[it->second];
}

const StructLayout* StructLibrary::Layout(const std::string& name) const
{
    auto it = mIndex.find(name);
    return it == mIndex.end() ? nullptr : &mLayouts[it->second];
}

namespace
{
// Returns false once the leaf cap is hit so every loop above it unwinds immediately.
bool Flatten(const StructLibrary& lib, const StructDef& d, const StructLayout& L, const std::string& prefix,
             uint32_t base, size_t maxLeaves, std::vector<LeafField>& out, bool& truncated)
{
    for (size_t i = 0; i < d.fields.size(); ++i)
    {
        const FieldDef& f = d.fields[i];
        for (uint32_t e = 0; e < f.count; ++e)
        {
            std::string path = prefix + f.name;
            if (f.isArray) path += "[" + std::to_string(e) + "]";
            const uint32_t off = base + L.fields[i].offset + e * L.fields[i].elemSize;
            if (f.kind == FieldKind::Struct)
            {
                const StructDef* sd = lib.Find(f.structName);
                const StructLayout* sl = lib.Layout(f.structName);
                if (!sd || !sl) continue;
                if (!Flatten(lib, *sd, *sl, path + ".", off, maxLeaves, out, truncated)) return false;
            }
            else
            {
                if (out.size() >= maxLeaves) { truncated = true; return false; }
                LeafField leaf;
                leaf.path = path;
                leaf.offset = off;
                leaf.field = f;
                leaf.field.name = path;
                leaf.field.count = 1;
                leaf.field.isArray = false;
                leaf.field.hasExplicitOffset = false;
                out.push_back(std::move(leaf));
            }
        }
    }
    return true;
}
}  // namespace

bool StructLibrary::FlattenLeaves(const std::string& structName, size_t maxLeaves,
                                  std::vector<LeafField>& out, bool& truncated) const
{
    out.clear();
    truncated = false;
    const StructDef* d = Find(structName);
    const StructLayout* L = Layout(structName);
    if (!d || !L) return false;
    Flatten(*this, *d, *L, "", 0, maxLeaves, out, truncated);
    return true;
}

const StructView* StructLibrary::FindView(uint64_t id) const
{
    for (const StructView& v : mViews)
        if (v.id == id) return &v;
    return nullptr;
}

bool StructLibrary::ValidateView(const std::string& label, const std::string& structName, uint32_t count,
                                 const std::string& expression, std::string* err) const
{
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
    std::string why;
    if (!Parser::LabelOk(label, why)) return fail("View label " + why + ".");
    if (label != Trim(label)) return fail("View label has leading or trailing spaces.");
    const StructLayout* L = Layout(structName);
    if (!L) return fail("Unknown struct '" + structName + "'.");
    if (count == 0 || count > kMaxViewCount ||
        (uint64_t)count * L->size > 0x100000000ull)
        return fail("Count must be 1.." + std::to_string(kMaxViewCount) + " and count x size must fit 32 bits.");
    if (!ExprOk(expression, why)) return fail(why);
    return true;
}

uint64_t StructLibrary::AddView(const std::string& label, const std::string& structName, uint32_t count,
                                const std::string& expression, std::string* err)
{
    if (mViews.size() >= kMaxViews)
    {
        if (err) *err = "Too many views.";
        return 0;
    }
    if (!ValidateView(label, structName, count, expression, err)) return 0;
    StructView v;
    v.id = mNextViewId++;
    v.label = label;
    v.structName = structName;
    v.count = count;
    v.expression = expression;
    mViews.push_back(std::move(v));
    ++mViewsGeneration;
    return mViews.back().id;
}

bool StructLibrary::UpdateView(uint64_t id, const std::string& label, const std::string& structName,
                               uint32_t count, const std::string& expression, std::string* err)
{
    for (StructView& v : mViews)
        if (v.id == id)
        {
            if (!ValidateView(label, structName, count, expression, err)) return false;
            v.label = label;
            v.structName = structName;
            v.count = count;
            v.expression = expression;
            ++mViewsGeneration;
            return true;
        }
    if (err) *err = "No such view.";
    return false;
}

bool StructLibrary::RemoveView(uint64_t id)
{
    for (size_t i = 0; i < mViews.size(); ++i)
        if (mViews[i].id == id)
        {
            mViews.erase(mViews.begin() + (std::ptrdiff_t)i);
            ++mViewsGeneration;
            return true;
        }
    return false;
}

bool StructLibrary::Equivalent(const StructLibrary& o) const
{
    if (mStructs.size() != o.mStructs.size() || mViews.size() != o.mViews.size()) return false;
    for (size_t i = 0; i < mStructs.size(); ++i)
    {
        const StructDef& a = mStructs[i];
        const StructDef& b = o.mStructs[i];
        if (a.name != b.name || a.packed != b.packed || a.minSize != b.minSize || a.size != b.size ||
            a.align != b.align || a.fields.size() != b.fields.size())
            return false;
        for (size_t k = 0; k < a.fields.size(); ++k)
        {
            const FieldDef& x = a.fields[k];
            const FieldDef& y = b.fields[k];
            const FieldLayout& lx = mLayouts[i].fields[k];
            const FieldLayout& ly = o.mLayouts[i].fields[k];
            if (x.name != y.name || x.kind != y.kind || x.structName != y.structName || x.pointee != y.pointee ||
                x.count != y.count || x.isArray != y.isArray || x.fmt != y.fmt || lx.offset != ly.offset ||
                lx.elemSize != ly.elemSize)
                return false;
        }
    }
    for (size_t i = 0; i < mViews.size(); ++i)
    {
        const StructView& a = mViews[i];
        const StructView& b = o.mViews[i];
        if (a.label != b.label || a.expression != b.expression || a.structName != b.structName || a.count != b.count)
            return false;
    }
    return true;
}

}  // namespace sfe
