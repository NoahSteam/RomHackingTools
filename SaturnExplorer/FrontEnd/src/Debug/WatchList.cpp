#include "Debug/WatchList.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#include "SaturnRegions.h"

namespace sfe
{

const WatchType kAllWatchTypes[8] = {
    WatchType::U8, WatchType::S8, WatchType::U16, WatchType::S16,
    WatchType::U32, WatchType::S32, WatchType::RGB555, WatchType::Pointer };

const char* WatchTypeName(WatchType t)
{
    switch (t)
    {
    case WatchType::U8:      return "U8";
    case WatchType::S8:      return "S8";
    case WatchType::U16:     return "U16";
    case WatchType::S16:     return "S16";
    case WatchType::U32:     return "U32";
    case WatchType::S32:     return "S32";
    case WatchType::RGB555:  return "RGB555";
    case WatchType::Pointer: return "Pointer";
    }
    return "U16";
}

uint32_t WatchTypeSize(WatchType t)
{
    switch (t)
    {
    case WatchType::U8:  case WatchType::S8:                      return 1;
    case WatchType::U16: case WatchType::S16: case WatchType::RGB555: return 2;
    default:                                                     return 4;
    }
}

bool WatchTypeFromName(const char* s, WatchType& out)
{
    for (WatchType t : kAllWatchTypes)
    {
        if (std::strcmp(s, WatchTypeName(t)) == 0) { out = t; return true; }
    }
    return false;
}

namespace
{
// Trim ASCII whitespace from both ends.
std::string Trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

// Parse a hex ("0x..." or bare hex) or decimal token. Returns false if not a
// clean integer. 'allowBareHex' treats a bare token as hex (addresses) vs decimal.
bool ParseInt(const std::string& tokIn, bool allowBareHex, uint32_t& out)
{
    std::string tok = Trim(tokIn);
    if (tok.empty()) return false;
    int base = 10;
    size_t i = 0;
    if (tok.size() > 2 && tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X'))
    {
        base = 16; i = 2;
    }
    else if (allowBareHex)
    {
        base = 16;
    }
    if (i >= tok.size()) return false;
    uint32_t v = 0;
    for (; i < tok.size(); ++i)
    {
        const char c = tok[i];
        int d;
        if (c >= '0' && c <= '9')      d = c - '0';
        else if (c >= 'a' && c <= 'f') d = 10 + c - 'a';
        else if (c >= 'A' && c <= 'F') d = 10 + c - 'A';
        else return false;
        if (d >= base) return false;
        v = v * base + static_cast<uint32_t>(d);
    }
    out = v;
    return true;
}
}  // namespace

bool SimpleExpressionResolver::Resolve(const std::string& exprIn, uint32_t& outAddr,
                                       std::string& outError)
{
    const std::string expr = Trim(exprIn);
    if (expr.empty()) { outError = "Empty expression"; return false; }

    // Split on the first top-level + or - (after the base token).
    size_t op = std::string::npos;
    for (size_t i = 1; i < expr.size(); ++i)
    {
        if (expr[i] == '+' || expr[i] == '-') { op = i; break; }
    }
    const std::string baseTok = (op == std::string::npos) ? expr : expr.substr(0, op);
    uint32_t base = 0;
    if (!ParseInt(baseTok, /*allowBareHex=*/true, base))
    {
        outError = "Invalid address";
        return false;
    }
    if (op == std::string::npos)
    {
        outAddr = base;
        return true;
    }
    const char sign = expr[op];
    uint32_t off = 0;
    if (!ParseInt(expr.substr(op + 1), /*allowBareHex=*/false, off))
    {
        outError = "Invalid offset";
        return false;
    }
    outAddr = (sign == '+') ? (base + off) : (base - off);
    return true;
}

std::string NormalizeExpression(const std::string& exprIn)
{
    const std::string expr = Trim(exprIn);
    // Only normalize a pure address (no +/-); leave arithmetic expressions as typed.
    for (size_t i = 1; i < expr.size(); ++i)
    {
        if (expr[i] == '+' || expr[i] == '-') return expr;
    }
    uint32_t v = 0;
    if (ParseInt(expr, true, v))
    {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%08X", v);
        return buf;
    }
    return expr;
}

bool IsPlausibleSaturnAddress(uint32_t addr)
{
    const uint32_t a = addr & 0x07FFFFFFu;
    struct R { uint32_t lo, hi; };
    static const R kOk[] = {
        { 0x00000000u, 0x0007FFFFu },  // BIOS
        { 0x00200000u, 0x002FFFFFu },  // Low work RAM
        { 0x05C00000u, 0x05C7FFFFu },  // VDP1 VRAM
        { 0x05D00000u, 0x05D0FFFFu },  // VDP1 regs
        { 0x05E00000u, 0x05E7FFFFu },  // VDP2 VRAM
        { 0x05F00000u, 0x05F00FFFu },  // CRAM
        { 0x05F80000u, 0x05F8FFFFu },  // VDP2 regs
        { 0x06000000u, 0x060FFFFFu },  // High work RAM
    };
    for (const R& r : kOk)
    {
        if (a >= r.lo && a <= r.hi) return true;
    }
    return false;
}

WatchValue FormatWatchValue(WatchType type, const MemoryReadResult& mem)
{
    WatchValue v;
    if (!mem.success)
    {
        v.text = mem.error.empty() ? "Unavailable" : mem.error;
        return v;
    }
    const uint32_t size = WatchTypeSize(type);
    if (mem.bytes.size() < size)
    {
        v.text = "Unavailable";
        return v;
    }
    // Big-endian assembly.
    uint32_t raw = 0;
    for (uint32_t i = 0; i < size; ++i) raw = (raw << 8) | mem.bytes[i];

    char buf[48];
    v.valid = true;
    switch (type)
    {
    case WatchType::U8:
        std::snprintf(buf, sizeof(buf), "%02X", raw & 0xFF);
        v.text = buf; v.numeric = raw & 0xFF; v.numericMeaningful = true; break;
    case WatchType::S8:
        std::snprintf(buf, sizeof(buf), "%d", (int)(int8_t)raw);
        v.text = buf; v.numeric = (int8_t)raw; v.numericMeaningful = true; break;
    case WatchType::U16:
        std::snprintf(buf, sizeof(buf), "%04X", raw & 0xFFFF);
        v.text = buf; v.numeric = raw & 0xFFFF; v.numericMeaningful = true; break;
    case WatchType::S16:
        std::snprintf(buf, sizeof(buf), "%d", (int)(int16_t)raw);
        v.text = buf; v.numeric = (int16_t)raw; v.numericMeaningful = true; break;
    case WatchType::U32:
        std::snprintf(buf, sizeof(buf), "%08X", raw);
        v.text = buf; v.numeric = (long long)(uint32_t)raw; v.numericMeaningful = true; break;
    case WatchType::S32:
        std::snprintf(buf, sizeof(buf), "%d", (int32_t)raw);
        v.text = buf; v.numeric = (int32_t)raw; v.numericMeaningful = true; break;
    case WatchType::RGB555:
    {
        const uint16_t w = (uint16_t)(raw & 0xFFFF);
        const int r5 = (w >> 10) & 0x1F, g5 = (w >> 5) & 0x1F, b5 = w & 0x1F;
        std::snprintf(buf, sizeof(buf), "R%d G%d B%d", r5, g5, b5);
        v.text = buf; v.hasSwatch = true; DecodeRgb555(w, v.r, v.g, v.b);
        v.numeric = w; v.numericMeaningful = false;   // changed = neutral tint
        break;
    }
    case WatchType::Pointer:
        std::snprintf(buf, sizeof(buf), "-> 0x%08X", raw);
        v.text = buf; v.isPointer = true; v.pointerTarget = raw;
        v.pointerSuspicious = !IsPlausibleSaturnAddress(raw);
        v.numeric = (long long)(uint32_t)raw; v.numericMeaningful = false;
        break;
    }
    return v;
}

WatchEntry& WatchList::Add(const std::string& name, const std::string& expr,
                           WatchType type, bool enabled)
{
    WatchEntry e;
    e.id = mNextId++;
    e.name = name;
    e.expression = expr.empty() ? "0x06000000" : expr;
    e.type = type;
    e.enabled = enabled;
    mEntries.push_back(std::move(e));
    return mEntries.back();
}

void WatchList::RemoveAt(size_t i)
{
    if (i < mEntries.size()) mEntries.erase(mEntries.begin() + i);
}

// --- JSON --------------------------------------------------------------------

namespace
{
void JsonEscape(const std::string& s, std::string& out)
{
    out.push_back('"');
    for (char c : s)
    {
        switch (c)
        {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if ((unsigned char)c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); out += b; }
            else out.push_back(c);
        }
    }
    out.push_back('"');
}

// A tiny JSON parser (object/array/string/number/bool/null) — enough for the watch
// schema, robust to whitespace, key order, and escaped strings.
struct JVal
{
    enum T { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool               b = false;
    double             num = 0;
    std::string        str;
    std::vector<JVal>  arr;
    std::vector<std::pair<std::string, JVal>> obj;

    const JVal* Find(const char* key) const
    {
        for (auto& kv : obj) if (kv.first == key) return &kv.second;
        return nullptr;
    }
};

struct JParser
{
    const char* p;
    const char* end;
    bool ok = true;

    void skip() { while (p < end && std::isspace((unsigned char)*p)) ++p; }
    bool parse(JVal& v)
    {
        skip();
        if (p >= end) return fail();
        switch (*p)
        {
        case '{': return object(v);
        case '[': return array(v);
        case '"': return string(v);
        case 't': case 'f': return boolean(v);
        case 'n': return null(v);
        default:  return number(v);
        }
    }
    bool fail() { ok = false; return false; }
    bool object(JVal& v)
    {
        v.type = JVal::Obj; ++p; skip();
        if (p < end && *p == '}') { ++p; return true; }
        while (p < end)
        {
            skip();
            JVal key; if (!string(key)) return fail();
            skip(); if (p >= end || *p != ':') return fail(); ++p;
            JVal val; if (!parse(val)) return fail();
            v.obj.emplace_back(key.str, std::move(val));
            skip();
            if (p < end && *p == ',') { ++p; continue; }
            if (p < end && *p == '}') { ++p; return true; }
            return fail();
        }
        return fail();
    }
    bool array(JVal& v)
    {
        v.type = JVal::Arr; ++p; skip();
        if (p < end && *p == ']') { ++p; return true; }
        while (p < end)
        {
            JVal e; if (!parse(e)) return fail();
            v.arr.push_back(std::move(e));
            skip();
            if (p < end && *p == ',') { ++p; continue; }
            if (p < end && *p == ']') { ++p; return true; }
            return fail();
        }
        return fail();
    }
    bool string(JVal& v)
    {
        v.type = JVal::Str;
        skip();
        if (p >= end || *p != '"') return fail();
        ++p;
        while (p < end && *p != '"')
        {
            char c = *p++;
            if (c == '\\' && p < end)
            {
                char e = *p++;
                switch (e)
                {
                case 'n': v.str.push_back('\n'); break;
                case 'r': v.str.push_back('\r'); break;
                case 't': v.str.push_back('\t'); break;
                case 'u': if (p + 4 <= end) p += 4; v.str.push_back('?'); break;
                default:  v.str.push_back(e); break;
                }
            }
            else v.str.push_back(c);
        }
        if (p >= end) return fail();
        ++p;   // closing quote
        return true;
    }
    bool boolean(JVal& v)
    {
        v.type = JVal::Bool;
        if (end - p >= 4 && std::strncmp(p, "true", 4) == 0)  { v.b = true;  p += 4; return true; }
        if (end - p >= 5 && std::strncmp(p, "false", 5) == 0) { v.b = false; p += 5; return true; }
        return fail();
    }
    bool null(JVal& v)
    {
        v.type = JVal::Null;
        if (end - p >= 4 && std::strncmp(p, "null", 4) == 0) { p += 4; return true; }
        return fail();
    }
    bool number(JVal& v)
    {
        v.type = JVal::Num;
        char* e = nullptr;
        v.num = std::strtod(p, &e);
        if (e == p) return fail();
        p = e;
        return true;
    }
};
}  // namespace

std::string WatchList::ToJson() const
{
    std::string out = "{\n  \"version\": 1,\n  \"watches\": [";
    for (size_t i = 0; i < mEntries.size(); ++i)
    {
        const WatchEntry& e = mEntries[i];
        out += (i == 0) ? "\n" : ",\n";
        out += "    { \"name\": ";
        JsonEscape(e.name, out);
        out += ", \"expression\": ";
        JsonEscape(e.expression, out);
        out += ", \"type\": ";
        JsonEscape(WatchTypeName(e.type), out);
        out += ", \"enabled\": ";
        out += e.enabled ? "true" : "false";
        out += " }";
    }
    out += mEntries.empty() ? "]\n}\n" : "\n  ]\n}\n";
    return out;
}

int WatchList::FromJson(const std::string& json, std::vector<std::string>& errors)
{
    JParser jp{ json.c_str(), json.c_str() + json.size() };
    JVal doc;
    if (!jp.parse(doc) || doc.type != JVal::Obj)
    {
        errors.push_back("File is not valid JSON.");
        return -1;
    }
    const JVal* ver = doc.Find("version");
    if (!ver || ver->type != JVal::Num || (int)ver->num != 1)
    {
        errors.push_back("Unsupported or missing \"version\" (expected 1).");
        return -1;
    }
    const JVal* watches = doc.Find("watches");
    if (!watches || watches->type != JVal::Arr)
    {
        errors.push_back("Missing \"watches\" array.");
        return -1;
    }

    std::vector<WatchEntry> imported;
    int index = 0;
    for (const JVal& w : watches->arr)
    {
        ++index;
        if (w.type != JVal::Obj) { errors.push_back("Entry " + std::to_string(index) + ": not an object."); continue; }
        const JVal* name = w.Find("name");
        const JVal* expr = w.Find("expression");
        const JVal* type = w.Find("type");
        const JVal* en   = w.Find("enabled");
        if (!expr || expr->type != JVal::Str)
        { errors.push_back("Entry " + std::to_string(index) + ": missing \"expression\"."); continue; }
        WatchType t = WatchType::U16;
        if (type && type->type == JVal::Str && !WatchTypeFromName(type->str.c_str(), t))
        { errors.push_back("Entry " + std::to_string(index) + ": unknown type \"" + type->str + "\", using U16."); }
        WatchEntry e;
        e.id = mNextId++;
        e.name = (name && name->type == JVal::Str) ? name->str : "";
        e.expression = expr->str;
        e.type = t;
        e.enabled = en ? (en->type == JVal::Bool ? en->b : true) : true;
        imported.push_back(std::move(e));
    }
    mEntries = std::move(imported);
    return (int)mEntries.size();
}

}  // namespace sfe
