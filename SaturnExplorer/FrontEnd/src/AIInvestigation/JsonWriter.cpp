#include "AIInvestigation/Json.h"

#include <algorithm>

namespace sfe
{
namespace ai
{
namespace
{

void AppendString(std::string& out, const std::string& s)
{
    static const char kHex[] = "0123456789abcdef";
    out += '"';
    for (const char ch : s)
    {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (c)
        {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20)
            {
                out += "\\u00";
                out += kHex[c >> 4];
                out += kHex[c & 0xF];
            }
            else
            {
                out += ch;   // UTF-8 passes through untouched
            }
        }
    }
    out += '"';
}

void AppendInteger(std::string& out, const JsonValue& v)
{
    uint64_t u = 0;
    if (v.ToUint64(&u))
    {
        out += std::to_string(u);
        return;
    }
    int64_t i = 0;
    v.ToInt64(&i);   // negative by construction, and within int64
    // std::to_string(int64_t) is exact for every value, INT64_MIN included.
    out += std::to_string(static_cast<long long>(i));
}

void Newline(std::string& out, int indent, int depth)
{
    if (indent <= 0) return;
    out += '\n';
    out.append(static_cast<size_t>(indent) * static_cast<size_t>(depth), ' ');
}

void Write(std::string& out, const JsonValue& v, const JsonWriteOptions& o, int depth)
{
    switch (v.type())
    {
    case JsonValue::Type::Null:    out += "null"; return;
    case JsonValue::Type::Bool:    out += v.AsBool() ? "true" : "false"; return;
    case JsonValue::Type::Integer: AppendInteger(out, v); return;
    case JsonValue::Type::String:  AppendString(out, v.AsString()); return;
    case JsonValue::Type::Array:
        if (v.items().empty()) { out += "[]"; return; }
        out += '[';
        for (size_t i = 0; i < v.items().size(); ++i)
        {
            if (i) out += ',';
            Newline(out, o.indent, depth + 1);
            Write(out, v.items()[i], o, depth + 1);
        }
        Newline(out, o.indent, depth);
        out += ']';
        return;
    case JsonValue::Type::Object:
    {
        if (v.members().empty()) { out += "{}"; return; }
        std::vector<const JsonValue::Member*> order;
        for (const JsonValue::Member& m : v.members()) order.push_back(&m);
        // std::string compares as unsigned bytes, so this is UTF-8 byte order.
        if (o.sortKeys)
            std::sort(order.begin(), order.end(),
                      [](const JsonValue::Member* a, const JsonValue::Member* b) { return a->first < b->first; });
        out += '{';
        for (size_t i = 0; i < order.size(); ++i)
        {
            if (i) out += ',';
            Newline(out, o.indent, depth + 1);
            AppendString(out, order[i]->first);
            out += o.indent > 0 ? ": " : ":";
            Write(out, order[i]->second, o, depth + 1);
        }
        Newline(out, o.indent, depth);
        out += '}';
        return;
    }
    }
}

}  // namespace

std::string WriteJson(const JsonValue& value, const JsonWriteOptions& options)
{
    std::string out;
    Write(out, value, options, 0);
    return out;
}

}  // namespace ai
}  // namespace sfe
