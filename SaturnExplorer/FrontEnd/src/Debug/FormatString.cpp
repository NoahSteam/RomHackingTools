#include "FormatString.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace sfe
{
namespace
{

std::string Lower(std::string s)
{
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string Trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// A register/pseudo name the format syntax accepts (lowercase). Returns false for
// unknown names so both eval and validate agree on the vocabulary.
bool IsKnownName(const std::string& n)
{
    if (n == "pc" || n == "pr" || n == "sr" || n == "gbr" || n == "vbr" ||
        n == "mach" || n == "macl" || n == "frame" || n == "cycle")
        return true;
    if (n.size() >= 2 && n[0] == 'r')
    {
        char* end = nullptr;
        long i = std::strtol(n.c_str() + 1, &end, 10);
        return end && *end == '\0' && i >= 0 && i <= 15;
    }
    return false;
}

bool IsAddressReg(const std::string& n)
{
    return n == "pc" || n == "pr" || n == "gbr" || n == "vbr";
}

struct Spec
{
    enum class Fmt { Default, Hex, UDec, SDec, String } fmt = Fmt::Default;
    int  size = 4;        // bytes: 1/2/4 (memory read size + value width)
    int  hexWidth = 0;    // zero-pad width for hex
    bool upper = true;
    bool given = false;
};

// Parse a :spec suffix. Returns false (with 'err' set) on an unrecognized spec.
bool ParseSpec(const std::string& raw, Spec& out, std::string& err)
{
    const std::string s = Lower(Trim(raw));
    if (s.empty()) { out.given = false; return true; }
    out.given = true;
    if (s == "string") { out.fmt = Spec::Fmt::String; return true; }
    const char c = s[0];
    const std::string num = s.substr(1);
    auto widthToSize = [](int w) { return w <= 2 ? 1 : (w <= 4 ? 2 : 4); };
    if (c == 'x')
    {
        out.fmt = Spec::Fmt::Hex;
        out.upper = (raw.size() && (raw[0] == 'X'));
        out.hexWidth = num.empty() ? 0 : std::atoi(num.c_str());
        out.size = out.hexWidth ? widthToSize(out.hexWidth) : 4;
        return true;
    }
    if (c == 'u' || c == 's')
    {
        const int bits = std::atoi(num.c_str());
        if (bits != 8 && bits != 16 && bits != 32) { err = "bad width in ':" + raw + "'"; return false; }
        out.fmt = (c == 'u') ? Spec::Fmt::UDec : Spec::Fmt::SDec;
        out.size = bits / 8;
        return true;
    }
    err = "unknown spec ':" + raw + "'";
    return false;
}

std::string RenderValue(uint32_t v, const Spec& spec, bool addressReg)
{
    Spec s = spec;
    if (s.fmt == Spec::Fmt::Default)
    {
        // Default: address-like registers as X8, everything else decimal.
        if (addressReg) { s.fmt = Spec::Fmt::Hex; s.upper = true; s.hexWidth = 8; s.size = 4; }
        else            { s.fmt = Spec::Fmt::UDec; s.size = 4; }
    }
    const uint32_t mask = (s.size >= 4) ? 0xFFFFFFFFu : ((1u << (s.size * 8)) - 1u);
    char buf[32];
    if (s.fmt == Spec::Fmt::Hex)
    {
        std::snprintf(buf, sizeof(buf), s.upper ? "%0*X" : "%0*x", s.hexWidth, v & mask);
    }
    else if (s.fmt == Spec::Fmt::SDec)
    {
        int32_t sv = static_cast<int32_t>(v & mask);
        const uint32_t sign = 1u << (s.size * 8 - 1);
        if (s.size < 4 && (static_cast<uint32_t>(sv) & sign)) sv -= static_cast<int32_t>(mask + 1);
        std::snprintf(buf, sizeof(buf), "%d", sv);
    }
    else  // UDec
    {
        std::snprintf(buf, sizeof(buf), "%u", v & mask);
    }
    return buf;
}

// Parse a memory address expression: <base>[+<offset>], base = register or constant.
bool ResolveAddr(const std::string& expr, const IFormatContext& ctx, uint32_t& out, std::string& err)
{
    std::string e = Trim(expr);
    uint32_t off = 0;
    const size_t plus = e.find('+');
    if (plus != std::string::npos)
    {
        off = static_cast<uint32_t>(std::strtoul(Trim(e.substr(plus + 1)).c_str(), nullptr, 0));
        e = Trim(e.substr(0, plus));
    }
    uint32_t base = 0;
    const std::string low = Lower(e);
    if (IsKnownName(low))
    {
        if (!ctx.GetValue(low, base)) { err = "no value for '" + e + "'"; return false; }
    }
    else if (e.size() && (std::isdigit(static_cast<unsigned char>(e[0])) || e[0] == '-'))
    {
        base = static_cast<uint32_t>(std::strtoul(e.c_str(), nullptr, 0));
    }
    else
    {
        err = "bad address '" + e + "'";
        return false;
    }
    out = base + off;
    return true;
}

// Validate a memory address expression's syntax without a context.
bool ValidateAddr(const std::string& expr, std::string& err)
{
    std::string e = Trim(expr);
    const size_t plus = e.find('+');
    if (plus != std::string::npos) e = Trim(e.substr(0, plus));
    if (e.empty()) { err = "empty address"; return false; }
    if (IsKnownName(Lower(e))) return true;
    if (std::isdigit(static_cast<unsigned char>(e[0]))) return true;
    err = "bad address '" + e + "'";
    return false;
}

// Evaluate one token body (text between the braces). On error, sets 'err' and
// returns "?" so a live preview still renders.
std::string EvalToken(const std::string& body, const IFormatContext* ctx, std::string& err)
{
    const size_t colon = body.find(':');
    const std::string valuePart = Trim(colon == std::string::npos ? body : body.substr(0, colon));
    const std::string specPart  = colon == std::string::npos ? std::string() : body.substr(colon + 1);
    Spec spec;
    if (!ParseSpec(specPart, spec, err)) return "?";

    if (!valuePart.empty() && valuePart[0] == '*')       // memory deref
    {
        const std::string addrExpr = valuePart.substr(1);
        if (!ctx)  // validation only
        {
            if (!ValidateAddr(addrExpr, err)) return "?";
            return "";
        }
        uint32_t addr = 0;
        if (!ResolveAddr(addrExpr, *ctx, addr, err)) return "?";
        if (spec.fmt == Spec::Fmt::String)
        {
            std::string s;
            if (!ctx->ReadString(addr, s, 64)) { err = "unreadable string"; return "?"; }
            return s;
        }
        uint32_t v = 0;
        if (!ctx->ReadMem(addr, static_cast<uint32_t>(spec.size), v)) { err = "unreadable memory"; return "?"; }
        return RenderValue(v, spec, false);
    }

    // Register / pseudo
    const std::string name = Lower(valuePart);
    if (!IsKnownName(name)) { err = "unknown value '" + valuePart + "'"; return "?"; }
    if (spec.fmt == Spec::Fmt::String) { err = "':string' needs a *memory value"; return "?"; }
    if (!ctx) return "";   // validation only
    uint32_t v = 0;
    if (!ctx->GetValue(name, v)) { err = "no value for '" + valuePart + "'"; return "?"; }
    return RenderValue(v, spec, IsAddressReg(name));
}

// Walk 'tmpl', calling EvalToken for each {token}. ctx==nullptr → validate mode
// (accumulate the first error into 'firstErr'). Returns the rendered string.
std::string Walk(const std::string& tmpl, const IFormatContext* ctx, std::string& firstErr)
{
    std::string out;
    for (size_t i = 0; i < tmpl.size(); ++i)
    {
        const char c = tmpl[i];
        if (c == '{' && i + 1 < tmpl.size() && tmpl[i + 1] == '{') { out += '{'; ++i; continue; }
        if (c == '}' && i + 1 < tmpl.size() && tmpl[i + 1] == '}') { out += '}'; ++i; continue; }
        if (c == '{')
        {
            const size_t end = tmpl.find('}', i + 1);
            if (end == std::string::npos)
            {
                if (firstErr.empty()) firstErr = "unmatched '{'";
                out += "?";
                break;
            }
            std::string err;
            out += EvalToken(tmpl.substr(i + 1, end - i - 1), ctx, err);
            if (!err.empty() && firstErr.empty()) firstErr = err;
            i = end;
        }
        else
        {
            out += c;
        }
    }
    return out;
}

}  // namespace

std::string FormatEvaluate(const std::string& tmpl, const IFormatContext& ctx)
{
    std::string err;
    return Walk(tmpl, &ctx, err);
}

std::string FormatValidate(const std::string& tmpl)
{
    std::string err;
    Walk(tmpl, nullptr, err);
    return err;
}

}  // namespace sfe
