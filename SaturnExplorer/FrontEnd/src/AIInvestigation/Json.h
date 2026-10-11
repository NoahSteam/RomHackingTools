// Json -- the value tree and strict reader/writer behind the AI investigation file formats
// (response, manifest, evidence). No ImGui, no platform headers.
//
// The reader is deliberately stricter than a general JSON library, because it faces text an
// external agent wrote and the rest of the feature trusts what it accepts:
//   * a repeated object key is an error, never "last one wins";
//   * integers are exact: anything from -2^63 to 2^64-1 is kept as such, and an integer outside
//     that range is an error rather than a rounded double;
//   * numbers with a fraction or exponent are rejected (no schema field is non-integral, and a
//     canonical form of a double would be one more thing to get subtly wrong);
//   * bytes, nesting depth, array length, object size and string length are bounded while parsing.
//
// The tokenizer is nlohmann/json (FrontEnd/third_party/nlohmann-json/VENDOR.md); this tree and every
// rule above are SE's own, and only JsonReader.cpp includes that header.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace sfe
{
namespace ai
{

class JsonValue
{
public:
    enum class Type { Null, Bool, Integer, String, Array, Object };

    using Member = std::pair<std::string, JsonValue>;

    JsonValue() = default;   // null

    static JsonValue Null() { return JsonValue(); }
    static JsonValue Bool(bool b) { JsonValue v; v.mType = Type::Bool; v.mBool = b; return v; }
    static JsonValue UInt(uint64_t u) { JsonValue v; v.mType = Type::Integer; v.mMagnitude = u; return v; }
    static JsonValue Int(int64_t i);
    static JsonValue String(std::string s) { JsonValue v; v.mType = Type::String; v.mString = std::move(s); return v; }
    static JsonValue Array() { JsonValue v; v.mType = Type::Array; return v; }
    static JsonValue Object() { JsonValue v; v.mType = Type::Object; return v; }

    Type type() const { return mType; }
    bool IsNull() const { return mType == Type::Null; }
    bool IsBool() const { return mType == Type::Bool; }
    bool IsInteger() const { return mType == Type::Integer; }
    bool IsString() const { return mType == Type::String; }
    bool IsArray() const { return mType == Type::Array; }
    bool IsObject() const { return mType == Type::Object; }

    bool AsBool() const { return mBool; }
    const std::string& AsString() const { return mString; }

    // Integers keep their sign and magnitude, so the whole of int64 and uint64 is representable
    // and neither conversion ever rounds. A conversion that does not fit returns false.
    bool IsNegative() const { return mType == Type::Integer && mNegative; }
    bool ToUint64(uint64_t* out) const;
    bool ToInt64(int64_t* out) const;

    const std::vector<JsonValue>& items() const { return mItems; }
    std::vector<JsonValue>& items() { return mItems; }
    const std::vector<Member>& members() const { return mMembers; }

    // Objects. Find is a linear scan: objects here have a handful of members, and insertion order
    // is kept so a writer's output is stable.
    const JsonValue* Find(const std::string& key) const;
    bool Has(const std::string& key) const { return Find(key) != nullptr; }
    // Adds or replaces. The reader never calls it with a repeated key (that is an error there).
    JsonValue& Set(const std::string& key, JsonValue value);
    void Append(JsonValue value) { mItems.push_back(std::move(value)); }

    // Structural equality: same type and value; objects compare as sets of members, so key order
    // does not matter.
    bool Equals(const JsonValue& other) const;

private:
    Type mType = Type::Null;
    bool mBool = false;
    bool mNegative = false;
    uint64_t mMagnitude = 0;   // |value|; at most 2^63 when mNegative
    std::string mString;
    std::vector<JsonValue> mItems;
    std::vector<Member> mMembers;
};

struct JsonLimits
{
    size_t maxBytes = 8u * 1024u * 1024u;      // whole document, checked before parsing starts
    size_t maxDepth = 32;                      // nested arrays/objects; the root is depth 1
    size_t maxArrayLength = 4096;
    size_t maxObjectMembers = 256;
    size_t maxStringBytes = 64u * 1024u;       // UTF-8 bytes of any one string or key
};

struct JsonError
{
    std::string message;    // what is wrong, with the JSON path where known
    size_t offset = 0;      // byte offset, when the tokenizer reported one
};

// Parses exactly one JSON document and nothing after it (trailing text is an error). On failure
// *out is left null and *error says why. 'error' may be null.
bool ParseJson(const std::string& text, const JsonLimits& limits, JsonValue* out, JsonError* error);

struct JsonWriteOptions
{
    bool sortKeys = false;   // bytewise UTF-8 order of the key
    int indent = 0;          // 0: no insignificant whitespace; >0: that many spaces per level
};

// Strings are escaped the way RFC 8785 specifies (\" \\ \b \f \n \r \t, other control characters as
// lower-case \u00xx, everything else raw UTF-8), integers in plain decimal. With sortKeys and no
// indent the output is the canonical form that payload hashes are taken over.
std::string WriteJson(const JsonValue& value, const JsonWriteOptions& options);
inline std::string ToCanonicalJson(const JsonValue& value)
{
    JsonWriteOptions o;
    o.sortKeys = true;
    return WriteJson(value, o);
}

}  // namespace ai
}  // namespace sfe
