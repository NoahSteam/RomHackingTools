// Json tests: the three properties the AI response contract depends on (PLAN.md §6.2, M0) -- a
// repeated key is an error, integers are exact up to 2^64-1 and anything larger is refused, and the
// byte / depth / length limits hold -- plus the canonical writer the payload hashes are taken over.
#include "AIInvestigation/Json.h"

#include <cstdio>
#include <string>

using namespace sfe::ai;

namespace
{
int gFail = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; }
}

bool Parses(const std::string& text, const JsonLimits& limits = JsonLimits(), JsonValue* out = nullptr,
            std::string* why = nullptr)
{
    JsonValue v;
    JsonError e;
    const bool ok = ParseJson(text, limits, out ? out : &v, &e);
    if (why) *why = e.message;
    return ok;
}

bool Contains(const std::string& s, const char* needle) { return s.find(needle) != std::string::npos; }

// ---- (a) duplicate keys ----------------------------------------------------------------------

void TestDuplicateKeys()
{
    std::string why;
    Check(!Parses("{\"a\":1,\"a\":2}", JsonLimits(), nullptr, &why), "a repeated key is rejected");
    Check(Contains(why, "duplicate object key \"a\""), "the message names the key");
    Check(Contains(why, "$.a"), "the message carries the JSON path");

    // The repeat can be far from the first, nested, or inside an array's object.
    Check(!Parses("{\"a\":1,\"b\":2,\"c\":3,\"a\":4}"), "a non-adjacent repeat is rejected");
    Check(!Parses("{\"x\":{\"y\":{\"z\":1,\"z\":2}}}", JsonLimits(), nullptr, &why) && Contains(why, "$.x.y.z"),
          "a nested repeat is rejected, with its path");
    Check(!Parses("{\"list\":[{\"k\":1},{\"k\":1,\"k\":2}]}", JsonLimits(), nullptr, &why) && Contains(why, "$.list[1].k"),
          "a repeat inside an array element is rejected, with its index");

    // Keys are compared after unescaping: this is the same name spelled two ways.
    Check(!Parses("{\"a\":1,\"\\u0061\":2}"), "an escaped spelling of the same key is a repeat");
    Check(!Parses("{\"\":1,\"\":2}"), "empty keys repeat too");

    // Same name at different levels, or in sibling objects, is fine.
    Check(Parses("{\"a\":{\"a\":1},\"b\":{\"a\":2},\"c\":[{\"a\":3},{\"a\":4}]}"),
          "the same name in different objects is allowed");
    // A case difference is a different key (JSON is case-sensitive).
    Check(Parses("{\"a\":1,\"A\":2}"), "keys differing in case are distinct");

    JsonValue v;
    Check(!Parses("{\"a\":1,\"a\":2}", JsonLimits(), &v) && v.IsNull(), "a failed parse leaves the output null");
}

// ---- (b) integers ----------------------------------------------------------------------------

void TestIntegers()
{
    JsonValue v;
    uint64_t u = 0;
    int64_t i = 0;

    Check(Parses("18446744073709551615", JsonLimits(), &v) && v.ToUint64(&u) && u == 18446744073709551615ull,
          "2^64-1 is read exactly");
    Check(WriteJson(v, JsonWriteOptions()) == "18446744073709551615", "2^64-1 is written back exactly");
    Check(!v.ToInt64(&i), "2^64-1 does not fit int64");

    // 2^53+1 is the first integer a double cannot hold; a parser that goes through double returns 2^53.
    Check(Parses("9007199254740993", JsonLimits(), &v) && v.ToUint64(&u) && u == 9007199254740993ull,
          "2^53+1 survives (no double round trip)");
    Check(Parses("[9223372036854775807,9223372036854775808]", JsonLimits(), &v) &&
              v.items()[0].ToInt64(&i) && i == INT64_MAX &&
              v.items()[1].ToUint64(&u) && u == 9223372036854775808ull && !v.items()[1].ToInt64(&i),
          "2^63-1 and 2^63 land on the right side of the int64 boundary");

    Check(Parses("-9223372036854775808", JsonLimits(), &v) && v.ToInt64(&i) && i == INT64_MIN && !v.ToUint64(&u),
          "INT64_MIN is read exactly");
    Check(WriteJson(v, JsonWriteOptions()) == "-9223372036854775808", "INT64_MIN is written back exactly");
    Check(Parses("0", JsonLimits(), &v) && v.ToUint64(&u) && u == 0 && v.ToInt64(&i) && i == 0, "zero");
    Check(Parses("-0", JsonLimits(), &v) && v.ToUint64(&u) && u == 0 && WriteJson(v, JsonWriteOptions()) == "0",
          "negative zero is the integer zero");

    std::string why;
    Check(!Parses("18446744073709551616", JsonLimits(), nullptr, &why), "2^64 is rejected");
    Check(Contains(why, "integer out of range"), "and the reason says so, not 'not a number'");
    Check(!Parses("-9223372036854775809"), "below INT64_MIN is rejected");
    Check(!Parses("[1,99999999999999999999999999999999999999]"), "a huge integer inside an array is rejected");
    Check(!Parses("{\"n\":340282366920938463463374607431768211456}"), "2^128 inside an object is rejected");

    // Rejected rather than rounded: no field in any schema is non-integral.
    Check(!Parses("1.5", JsonLimits(), nullptr, &why) && Contains(why, "non-integer"), "a fraction is rejected");
    Check(!Parses("1e3"), "an exponent is rejected");
    Check(!Parses("1.0"), "1.0 is rejected, not folded to 1");

    // Malformed numbers are the tokenizer's job; pin that they stay refused.
    Check(!Parses("01"), "a leading zero is rejected");
    Check(!Parses("+1"), "a leading plus is rejected");
    Check(!Parses("0x10"), "hex is rejected");
    Check(!Parses("NaN") && !Parses("Infinity"), "NaN and Infinity are rejected");

    // Construction round trips too.
    Check(JsonValue::Int(INT64_MIN).ToInt64(&i) && i == INT64_MIN, "Int(INT64_MIN)");
    Check(JsonValue::Int(-1).ToInt64(&i) && i == -1 && !JsonValue::Int(-1).ToUint64(&u), "Int(-1)");
    Check(JsonValue::UInt(UINT64_MAX).ToUint64(&u) && u == UINT64_MAX, "UInt(max)");
}

// ---- (c) limits ------------------------------------------------------------------------------

std::string Nest(size_t depth, bool arrays)
{
    std::string s;
    for (size_t i = 0; i < depth; ++i) s += arrays ? "[" : "{\"k\":";
    s += "1";
    for (size_t i = 0; i < depth; ++i) s += arrays ? "]" : "}";
    return s;
}

void TestLimits()
{
    std::string why;

    // Bytes: checked against the document before the tokenizer sees any of it.
    JsonLimits tiny;
    tiny.maxBytes = 16;
    Check(Parses(std::string(14, ' ') + "[]", tiny), "a document of exactly maxBytes parses");
    Check(!Parses(std::string(15, ' ') + "[]", tiny, nullptr, &why) && Contains(why, "bytes"),
          "one byte over maxBytes is rejected");
    JsonLimits big;
    big.maxBytes = 1024;
    Check(!Parses(std::string(1025, ' ') + "1", big), "whitespace padding counts toward the byte limit");

    // Depth: arrays and objects both, at the boundary, and a bomb that would overflow a recursive parser.
    JsonLimits d;
    d.maxDepth = 32;
    Check(Parses(Nest(32, true), d), "depth 32 (arrays) parses");
    Check(!Parses(Nest(33, true), d, nullptr, &why) && Contains(why, "nesting deeper than 32"), "depth 33 (arrays) is rejected");
    Check(Parses(Nest(32, false), d), "depth 32 (objects) parses");
    Check(!Parses(Nest(33, false), d), "depth 33 (objects) is rejected");
    JsonLimits wide;
    wide.maxBytes = 4u * 1024u * 1024u;
    Check(!Parses(std::string(2000000, '['), wide), "two million open brackets are refused without recursing");

    // Array length.
    JsonLimits a;
    a.maxArrayLength = 4096;
    auto arrayOf = [](size_t n) {
        std::string s = "[";
        for (size_t i = 0; i < n; ++i) s += i ? ",0" : "0";
        return s + "]";
    };
    Check(Parses(arrayOf(4096), a), "an array of maxArrayLength elements parses");
    Check(!Parses(arrayOf(4097), a, nullptr, &why) && Contains(why, "more than 4096 elements"), "one element over is rejected");
    Check(Parses(arrayOf(0), a), "an empty array parses");
    Check(!Parses("{\"x\":" + arrayOf(4097) + "}", a, nullptr, &why) && Contains(why, "$.x"), "a long nested array is rejected, with its path");

    // Object size.
    JsonLimits o;
    o.maxObjectMembers = 3;
    Check(Parses("{\"a\":0,\"b\":0,\"c\":0}", o), "an object of maxObjectMembers members parses");
    Check(!Parses("{\"a\":0,\"b\":0,\"c\":0,\"d\":0}", o, nullptr, &why) && Contains(why, "more than 3 members"),
          "one member over is rejected");

    // String length, in bytes of UTF-8 (so a multi-byte character counts as several).
    JsonLimits s;
    s.maxStringBytes = 8;
    Check(Parses("\"12345678\"", s), "a string of maxStringBytes parses");
    Check(!Parses("\"123456789\"", s, nullptr, &why) && Contains(why, "string longer than 8"), "one byte over is rejected");
    Check(Parses("\"\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\"", s), "four two-byte characters are eight bytes");
    Check(!Parses("\"\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\"", s), "five two-byte characters are ten bytes");
    Check(!Parses("{\"123456789\":1}", s, nullptr, &why) && Contains(why, "key longer than 8"), "a long key is rejected");
    Check(!Parses("[\"ok\",\"123456789\"]", s), "a long string inside an array is rejected");
    // The limit applies to the decoded string: a \u escape is six bytes of text but one or three of content.
    Check(Parses("\"\\u00e9\\u00e9\\u00e9\\u00e9\"", s), "escapes are measured decoded (4 x 2 bytes)");
}

// ---- Strictness the tokenizer provides, pinned so a library update cannot loosen it ------------

void TestSyntaxStrictness()
{
    Check(!Parses(""), "empty input");
    Check(!Parses("   "), "whitespace only");
    Check(!Parses("{} {}"), "two documents");
    Check(!Parses("{} x"), "trailing text");
    Check(!Parses("[1,]") && !Parses("{\"a\":1,}"), "trailing commas");
    Check(!Parses("{a:1}") && !Parses("{'a':1}"), "unquoted and single-quoted keys");
    Check(!Parses("// c\n{}") && !Parses("/* c */ {}"), "comments");
    Check(!Parses("\"\\ud800\""), "a lone surrogate escape");
    Check(!Parses("\"\xff\""), "ill-formed UTF-8");
    Check(!Parses("\"a\nb\""), "a raw control character inside a string");
    Check(!Parses("[1 2]") && !Parses("{\"a\" 1}"), "missing separators");
    Check(!Parses("tru") && !Parses("nul"), "truncated literals");
    Check(Parses("  {\"a\" : [ true , false , null ] }  "), "insignificant whitespace is fine");
    Check(Parses("\"a\\u0000b\""), "an escaped NUL is a legal string (the validator limits where)");
    // A leading byte-order mark is skipped by the tokenizer; the response importer strips nothing itself.
    Check(Parses("\xEF\xBB\xBF{}"), "a UTF-8 BOM is tolerated");
}

// ---- Tree and writer -------------------------------------------------------------------------

void TestWriter()
{
    JsonValue v;
    Check(Parses("{\"b\":[1,2,{\"z\":null,\"y\":true}],\"a\":\"x\"}", JsonLimits(), &v), "fixture parses");
    Check(WriteJson(v, JsonWriteOptions()) == "{\"b\":[1,2,{\"z\":null,\"y\":true}],\"a\":\"x\"}",
          "insertion order is kept when not sorting");
    Check(ToCanonicalJson(v) == "{\"a\":\"x\",\"b\":[1,2,{\"y\":true,\"z\":null}]}",
          "canonical output sorts keys at every level and has no whitespace");

    // Two documents that differ only in key order and spacing have one canonical form.
    JsonValue w;
    Check(Parses(" { \"a\" : \"x\" , \"b\" : [ 1 , 2 , { \"y\" : true , \"z\" : null } ] } ", JsonLimits(), &w), "reordered fixture parses");
    Check(ToCanonicalJson(v) == ToCanonicalJson(w), "reordering keys does not change the canonical form");
    Check(v.Equals(w), "Equals ignores key order");

    // Key order is bytewise: upper case before lower, shorter prefix first.
    Check(Parses("{\"b\":1,\"B\":2,\"ab\":3,\"a\":4,\"\":5}", JsonLimits(), &v) &&
              ToCanonicalJson(v) == "{\"\":5,\"B\":2,\"a\":4,\"ab\":3,\"b\":1}", "keys sort by byte value");

    // Escapes: the short forms, the \u00xx range in lower case, and everything else raw.
    Check(Parses("[\"\\\"\\\\\\b\\f\\n\\r\\t\\u0001\\u001f\\u007f\\u00e9\"]", JsonLimits(), &v) &&
              WriteJson(v, JsonWriteOptions()) == "[\"\\\"\\\\\\b\\f\\n\\r\\t\\u0001\\u001f\x7f\xC3\xA9\"]",
          "string escaping follows RFC 8785");
    Check(Parses("\"\\u0000\"", JsonLimits(), &v) && WriteJson(v, JsonWriteOptions()) == "\"\\u0000\"", "NUL is escaped");
    Check(Parses("\"\\/\"", JsonLimits(), &v) && WriteJson(v, JsonWriteOptions()) == "\"/\"", "an escaped slash is written plain");

    // Round trip of what we write.
    Check(Parses(ToCanonicalJson(v), JsonLimits(), &w) && w.Equals(v), "canonical output parses back to the same value");

    // Indented output is for humans and parses back the same.
    Check(Parses("{\"a\":[1,{\"b\":2}],\"c\":{}}", JsonLimits(), &v), "indent fixture parses");
    JsonWriteOptions pretty;
    pretty.indent = 2;
    const std::string text = WriteJson(v, pretty);
    Check(text == "{\n  \"a\": [\n    1,\n    {\n      \"b\": 2\n    }\n  ],\n  \"c\": {}\n}", "indented layout");
    Check(Parses(text, JsonLimits(), &w) && w.Equals(v), "indented output parses back");

    Check(WriteJson(JsonValue::Array(), JsonWriteOptions()) == "[]" && WriteJson(JsonValue::Object(), JsonWriteOptions()) == "{}",
          "empty containers");
}

void TestTree()
{
    JsonValue o = JsonValue::Object();
    o.Set("a", JsonValue::UInt(1));
    o.Set("b", JsonValue::String("x"));
    o.Set("a", JsonValue::UInt(2));   // replaces, never duplicates
    Check(o.members().size() == 2, "Set replaces an existing key");
    uint64_t u = 0;
    Check(o.Find("a") && o.Find("a")->ToUint64(&u) && u == 2, "the replacement value is the one kept");
    Check(!o.Find("missing") && !o.Has("missing") && o.Has("b"), "Find and Has");
    Check(!JsonValue::UInt(1).Equals(JsonValue::Int(-1)), "1 != -1");
    Check(!JsonValue::UInt(1).Equals(JsonValue::String("1")), "different types are unequal");
}
}  // namespace

int main()
{
    TestDuplicateKeys();
    TestIntegers();
    TestLimits();
    TestSyntaxStrictness();
    TestWriter();
    TestTree();
    if (gFail == 0) std::printf("JsonTests: all passed\n");
    return gFail == 0 ? 0 : 1;
}
