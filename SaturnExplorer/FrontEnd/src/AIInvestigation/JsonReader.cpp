#include "AIInvestigation/Json.h"

#include <algorithm>

// The only translation unit that includes the library: it is large, and nothing else needs it.
#include <nlohmann/json.hpp>

namespace sfe
{
namespace ai
{

JsonValue JsonValue::Int(int64_t i)
{
    JsonValue v;
    v.mType = Type::Integer;
    if (i < 0)
    {
        v.mNegative = true;
        // Negate in unsigned arithmetic: -INT64_MIN does not fit in int64_t.
        v.mMagnitude = ~static_cast<uint64_t>(i) + 1u;
    }
    else
    {
        v.mMagnitude = static_cast<uint64_t>(i);
    }
    return v;
}

bool JsonValue::ToUint64(uint64_t* out) const
{
    if (mType != Type::Integer || mNegative) return false;
    if (out) *out = mMagnitude;
    return true;
}

bool JsonValue::ToInt64(int64_t* out) const
{
    if (mType != Type::Integer) return false;
    if (mNegative)
    {
        if (mMagnitude > (static_cast<uint64_t>(1) << 63)) return false;
        if (out) *out = static_cast<int64_t>(~mMagnitude + 1u);
        return true;
    }
    if (mMagnitude > static_cast<uint64_t>(INT64_MAX)) return false;
    if (out) *out = static_cast<int64_t>(mMagnitude);
    return true;
}

const JsonValue* JsonValue::Find(const std::string& key) const
{
    for (const Member& m : mMembers)
        if (m.first == key) return &m.second;
    return nullptr;
}

JsonValue& JsonValue::Set(const std::string& key, JsonValue value)
{
    for (Member& m : mMembers)
    {
        if (m.first == key)
        {
            m.second = std::move(value);
            return m.second;
        }
    }
    mMembers.emplace_back(key, std::move(value));
    return mMembers.back().second;
}

bool JsonValue::Equals(const JsonValue& o) const
{
    if (mType != o.mType) return false;
    switch (mType)
    {
    case Type::Null:    return true;
    case Type::Bool:    return mBool == o.mBool;
    case Type::Integer: return mMagnitude == o.mMagnitude && mNegative == o.mNegative;
    case Type::String:  return mString == o.mString;
    case Type::Array:
        if (mItems.size() != o.mItems.size()) return false;
        for (size_t i = 0; i < mItems.size(); ++i)
            if (!mItems[i].Equals(o.mItems[i])) return false;
        return true;
    case Type::Object:
        if (mMembers.size() != o.mMembers.size()) return false;
        for (const Member& m : mMembers)
        {
            const JsonValue* other = o.Find(m.first);
            if (!other || !m.second.Equals(*other)) return false;
        }
        return true;
    }
    return false;
}

// ---- Reader --------------------------------------------------------------------------------

namespace
{

using Json = nlohmann::json;

// Builds a JsonValue from the tokenizer's events. Returning false from any event stops the parse,
// which is how every limit and the duplicate-key rule are enforced before the rest of a hostile
// document is read.
class TreeBuilder : public Json::json_sax_t
{
public:
    TreeBuilder(const JsonLimits& limits, JsonValue* root) : mLimits(limits), mRoot(root) {}

    const std::string& Failure() const { return mFailure; }
    size_t FailureOffset() const { return mOffset; }
    bool Complete() const { return mHaveRoot && mFrames.empty(); }

    bool null() override { return Add(JsonValue::Null()); }
    bool boolean(bool v) override { return Add(JsonValue::Bool(v)); }
    bool number_integer(Json::number_integer_t v) override { return Add(JsonValue::Int(static_cast<int64_t>(v))); }
    bool number_unsigned(Json::number_unsigned_t v) override { return Add(JsonValue::UInt(static_cast<uint64_t>(v))); }

    // The tokenizer sends an integer too large for uint64_t (or too negative for int64_t) here, as
    // a double, with its original text. Digits-only text is therefore an out-of-range integer,
    // which must fail rather than be rounded.
    bool number_float(Json::number_float_t, const Json::string_t& text) override
    {
        const bool digitsOnly = !text.empty() &&
            std::all_of(text.begin(), text.end(), [](char c) { return c == '-' || (c >= '0' && c <= '9'); });
        return Fail(digitsOnly
            ? "integer out of range (must be between -9223372036854775808 and 18446744073709551615): " + text
            : "non-integer numbers are not allowed: " + text);
    }

    bool string(Json::string_t& v) override
    {
        if (v.size() > mLimits.maxStringBytes)
            return Fail("string longer than " + std::to_string(mLimits.maxStringBytes) + " bytes");
        return Add(JsonValue::String(std::move(v)));   // the tokenizer clears its buffer on the next token
    }

    bool binary(Json::binary_t&) override { return Fail("binary values are not JSON"); }

    bool start_object(std::size_t) override { return Open(JsonValue::Object()); }
    bool end_object() override { return Close(); }
    bool start_array(std::size_t) override { return Open(JsonValue::Array()); }
    bool end_array() override { return Close(); }

    bool key(Json::string_t& k) override
    {
        Frame& f = mFrames.back();
        f.pendingKey = k;   // first, so a failure below names this key in its path
        if (k.size() > mLimits.maxStringBytes)
            return Fail("object key longer than " + std::to_string(mLimits.maxStringBytes) + " bytes");
        if (f.value->members().size() >= mLimits.maxObjectMembers)
            return Fail("object with more than " + std::to_string(mLimits.maxObjectMembers) + " members");
        if (f.value->Has(k))   // objects here are small (maxObjectMembers), so a scan is cheaper than a set
            return Fail("duplicate object key \"" + k + "\"");
        return true;
    }

    bool parse_error(std::size_t position, const std::string&, const nlohmann::detail::exception& ex) override
    {
        if (mFailure.empty()) mFailure = ex.what();
        mOffset = position;
        return false;
    }

private:
    struct Frame
    {
        JsonValue* value;
        std::string pendingKey;       // objects: the member being read
    };

    bool Fail(const std::string& what)
    {
        if (mFailure.empty()) mFailure = what + " at " + Path();
        return false;
    }

    std::string Path() const
    {
        std::string p = "$";
        for (size_t i = 0; i < mFrames.size(); ++i)
        {
            const Frame& f = mFrames[i];
            if (f.value->IsArray())
            {
                // An enclosing array is positioned on the element that holds the next frame; the
                // innermost one on the element about to be added.
                const size_t n = f.value->items().size();
                p += "[" + std::to_string(i + 1 < mFrames.size() && n > 0 ? n - 1 : n) + "]";
            }
            else if (!f.pendingKey.empty())
            {
                p += "." + f.pendingKey;
            }
        }
        return p;
    }

    // Stores 'v' where the current container expects its next value and returns where it now
    // lives, or null when a limit refuses it.
    JsonValue* Place(JsonValue v)
    {
        if (mFrames.empty())
        {
            *mRoot = std::move(v);
            mHaveRoot = true;
            return mRoot;
        }
        Frame& f = mFrames.back();
        if (f.value->IsArray())
        {
            if (f.value->items().size() >= mLimits.maxArrayLength)
            {
                Fail("array with more than " + std::to_string(mLimits.maxArrayLength) + " elements");
                return nullptr;
            }
            f.value->Append(std::move(v));
            return &f.value->items().back();
        }
        return &f.value->Set(f.pendingKey, std::move(v));
    }

    bool Add(JsonValue v) { return Place(std::move(v)) != nullptr; }

    bool Open(JsonValue container)
    {
        if (mFrames.size() + 1 > mLimits.maxDepth)
            return Fail("nesting deeper than " + std::to_string(mLimits.maxDepth) + " levels");
        // The parent does not grow again until this container is closed, so the pointer stays
        // valid for as long as the frame does.
        JsonValue* where = Place(std::move(container));
        if (!where) return false;
        Frame f;
        f.value = where;
        mFrames.push_back(std::move(f));
        return true;
    }

    bool Close()
    {
        mFrames.pop_back();
        return true;
    }

    JsonLimits mLimits;
    JsonValue* mRoot;
    std::vector<Frame> mFrames;
    bool mHaveRoot = false;
    std::string mFailure;
    size_t mOffset = 0;
};

}  // namespace

bool ParseJson(const std::string& text, const JsonLimits& limits, JsonValue* out, JsonError* error)
{
    auto fail = [&](const std::string& message, size_t offset) {
        if (out) *out = JsonValue();
        if (error)
        {
            error->message = message;
            error->offset = offset;
        }
        return false;
    };
    if (text.size() > limits.maxBytes)
        return fail("document is " + std::to_string(text.size()) + " bytes; the limit is " +
                    std::to_string(limits.maxBytes), 0);

    JsonValue root;
    TreeBuilder builder(limits, &root);
    bool ok = false;
    try
    {
        ok = Json::sax_parse(text, &builder, nlohmann::detail::input_format_t::json,
                             /*strict=*/true, /*ignore_comments=*/false);
    }
    catch (const std::exception& e)
    {
        // sax_parse reports syntax errors through parse_error(), but allocation failure and the like
        // still throw; none of them may escape into the UI.
        return fail(std::string("JSON could not be read: ") + e.what(), 0);
    }
    if (!ok || !builder.Complete())
        return fail(builder.Failure().empty() ? "not a valid JSON document" : builder.Failure(), builder.FailureOffset());
    if (out) *out = std::move(root);
    return true;
}

}  // namespace ai
}  // namespace sfe
