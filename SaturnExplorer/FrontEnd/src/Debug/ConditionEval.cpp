#include "ConditionEval.h"

#include <cctype>
#include <cstdlib>

#include "FormatString.h"   // IFormatContext

namespace sfe
{
namespace
{

// Recursive-descent evaluator over a char stream. Values are unsigned 32-bit (SH-2
// register width); comparisons are unsigned and yield 0/1. When 'ctx' is null it runs in
// validate mode: symbols/memory resolve to 0 so only syntax is checked.
struct Parser
{
    const std::string& s;
    size_t             i = 0;
    const IFormatContext* ctx;   // null = validate only
    std::string        err;

    explicit Parser(const std::string& str, const IFormatContext* c) : s(str), ctx(c) {}

    void skip() { while (i < s.size() && std::isspace((unsigned char)s[i])) ++i; }
    bool eof() { skip(); return i >= s.size(); }
    char peek() { skip(); return i < s.size() ? s[i] : '\0'; }

    // Consume the operator 'op' if it is next (after whitespace). Longer ops are matched
    // before their prefixes by the callers' ordering.
    bool eat(const char* op)
    {
        skip();
        const size_t n = std::string(op).size();
        if (s.compare(i, n, op) == 0)
        {
            // Don't let "<" match inside "<<", or "&" inside "&&": callers try the long
            // form first, so a bare match here is only reached when the long form failed.
            i += n;
            return true;
        }
        return false;
    }

    void fail(const std::string& m) { if (err.empty()) err = m; }

    uint32_t parse() { uint32_t v = orExpr(); skip(); if (i < s.size()) fail("trailing text"); return v; }

    uint32_t orExpr()
    {
        uint32_t v = andExpr();
        while (eat("||")) { uint32_t r = andExpr(); v = (v || r) ? 1u : 0u; }
        return v;
    }
    uint32_t andExpr()
    {
        uint32_t v = cmpExpr();
        while (eat("&&")) { uint32_t r = cmpExpr(); v = (v && r) ? 1u : 0u; }
        return v;
    }
    // Comparison sits BELOW the bitwise ops on purpose (unlike C): "r0 & 0x80 == 0x80"
    // reads as "(r0 & 0x80) == 0x80", which is what a debugger user means. The longer
    // operators are tried before their prefixes; "<<"/">>" are already consumed by the
    // higher-precedence shift() inside each operand, so a bare "<"/">" here is relational.
    uint32_t cmpExpr()
    {
        uint32_t v = bitOr();
        if      (eat("==")) return v == bitOr() ? 1u : 0u;
        else if (eat("!=")) return v != bitOr() ? 1u : 0u;
        else if (eat("<=")) return v <= bitOr() ? 1u : 0u;
        else if (eat(">=")) return v >= bitOr() ? 1u : 0u;
        else if (eat("<"))  return v <  bitOr() ? 1u : 0u;
        else if (eat(">"))  return v >  bitOr() ? 1u : 0u;
        return v;
    }
    // '|' and '^' (but not '||').
    uint32_t bitOr()
    {
        uint32_t v = bitAnd();
        for (;;)
        {
            skip();
            if (i + 1 < s.size() && s[i] == '|' && s[i+1] == '|') break;   // leave for orExpr
            if (eat("|"))      v |= bitAnd();
            else if (eat("^")) v ^= bitAnd();
            else break;
        }
        return v;
    }
    // '&' (but not '&&').
    uint32_t bitAnd()
    {
        uint32_t v = shift();
        for (;;)
        {
            skip();
            if (i + 1 < s.size() && s[i] == '&' && s[i+1] == '&') break;   // leave for andExpr
            if (eat("&")) v &= shift();
            else break;
        }
        return v;
    }
    uint32_t shift()
    {
        uint32_t v = addSub();
        for (;;)
        {
            if (eat("<<"))      v <<= (addSub() & 31u);
            else if (eat(">>")) v >>= (addSub() & 31u);
            else break;
        }
        return v;
    }
    uint32_t addSub()
    {
        uint32_t v = unary();
        for (;;)
        {
            skip();
            if (i < s.size() && s[i] == '+') { ++i; v += unary(); }
            else if (i < s.size() && s[i] == '-') { ++i; v -= unary(); }
            else break;
        }
        return v;
    }
    uint32_t unary()
    {
        skip();
        if (i < s.size() && s[i] == '!') { ++i; return unary() == 0 ? 1u : 0u; }
        if (i < s.size() && s[i] == '~') { ++i; return ~unary(); }
        if (i < s.size() && s[i] == '-') { ++i; return 0u - unary(); }
        return primary();
    }
    uint32_t primary()
    {
        skip();
        if (i >= s.size()) { fail("unexpected end of expression"); return 0; }
        const char c = s[i];
        if (c == '(')
        {
            ++i;
            uint32_t v = orExpr();
            if (!eat(")")) fail("missing ')'");
            return v;
        }
        if (c == '*') { ++i; return deref(); }
        if (std::isdigit((unsigned char)c)) return number();
        if (std::isalpha((unsigned char)c) || c == '_') return name();
        fail(std::string("unexpected '") + c + "'");
        ++i;
        return 0;
    }
    uint32_t number()
    {
        char* end = nullptr;
        unsigned long v = std::strtoul(s.c_str() + i, &end, 0);
        if (end == s.c_str() + i) { fail("bad number"); return 0; }
        i = static_cast<size_t>(end - s.c_str());
        return static_cast<uint32_t>(v);
    }
    std::string ident()
    {
        size_t j = i;
        while (j < s.size() && (std::isalnum((unsigned char)s[j]) || s[j] == '_')) ++j;
        std::string t = s.substr(i, j - i);
        i = j;
        for (char& ch : t) ch = static_cast<char>(std::tolower((unsigned char)ch));
        return t;
    }
    uint32_t name()
    {
        const std::string t = ident();
        uint32_t v = 0;
        if (ctx && !ctx->GetValue(t, v)) fail("unknown value '" + t + "'");
        return v;
    }
    // '*' already consumed. Read the address primary, then an optional ':b|:w|:l' size.
    uint32_t deref()
    {
        const uint32_t addr = unary();   // allows *r4, *0x..., *(r4+8), *-…
        uint32_t size = 4;
        skip();
        if (i < s.size() && s[i] == ':')
        {
            ++i;
            skip();
            const char sc = i < s.size() ? (char)std::tolower((unsigned char)s[i]) : '\0';
            if (sc == 'b') { size = 1; ++i; }
            else if (sc == 'w') { size = 2; ++i; }
            else if (sc == 'l') { size = 4; ++i; }
            else fail("size after ':' must be b, w, or l");
        }
        uint32_t v = 0;
        if (ctx && !ctx->ReadMem(addr, size, v)) fail("unreadable memory");
        return v;
    }
};

}  // namespace

bool ConditionEval(const std::string& expr, const IFormatContext& ctx, std::string* err)
{
    // Empty guard => always fires.
    bool blank = true;
    for (char c : expr) if (!std::isspace((unsigned char)c)) { blank = false; break; }
    if (blank) return true;

    Parser p(expr, &ctx);
    const uint32_t v = p.parse();
    if (!p.err.empty())
    {
        if (err) *err = p.err;
        return true;   // fail-open: a broken guard must not swallow the stop
    }
    return v != 0;
}

std::string ConditionValidate(const std::string& expr)
{
    bool blank = true;
    for (char c : expr) if (!std::isspace((unsigned char)c)) { blank = false; break; }
    if (blank) return std::string();

    Parser p(expr, nullptr);
    p.parse();
    return p.err;
}

}  // namespace sfe
