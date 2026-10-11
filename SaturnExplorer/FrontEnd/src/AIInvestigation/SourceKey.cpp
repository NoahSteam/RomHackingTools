#include "AIInvestigation/SourceKey.h"

#include "AIInvestigation/CanonicalTarget.h"   // RegionApiName
#include "AIInvestigation/Encoding.h"
#include "Sha256.h"

namespace sfe
{
namespace ai
{

const char kFileContentName[] = "file";

const char* SourceKindName(SourceKind kind)
{
    switch (kind)
    {
    case SourceKind::CompareSnapshot: return "compare_snapshot";
    case SourceKind::PausedLiveHead:  return "paused_live_head";
    case SourceKind::RecorderFrame:   return "recorder_frame";
    case SourceKind::File:            return "file";
    case SourceKind::DiscFile:        return "disc_file";
    }
    return "?";
}

bool SourceKindFromName(const std::string& name, SourceKind* out)
{
    for (SourceKind k : { SourceKind::CompareSnapshot, SourceKind::PausedLiveHead, SourceKind::RecorderFrame,
                          SourceKind::File, SourceKind::DiscFile })
    {
        if (name == SourceKindName(k))
        {
            if (out) *out = k;
            return true;
        }
    }
    return false;
}

bool IsMemorySource(SourceKind kind)
{
    return kind == SourceKind::CompareSnapshot || kind == SourceKind::PausedLiveHead || kind == SourceKind::RecorderFrame;
}

bool SourceKey::operator==(const SourceKey& o) const
{
    return kind == o.kind && sourceId == o.sourceId && deriveSerial == o.deriveSerial && sessionId == o.sessionId &&
           timelineEpoch == o.timelineEpoch && hasConnectionGeneration == o.hasConnectionGeneration &&
           (!hasConnectionGeneration || connectionGeneration == o.connectionGeneration) && frame == o.frame &&
           contentSha256 == o.contentSha256;
}

std::string ValidateSourceKey(const SourceKey& k)
{
    for (const auto& c : k.contentSha256)
        if (!IsSha256Hex(c.second)) return "content_sha256[\"" + c.first + "\"] is not 64 lower-case hex digits";

    if (IsMemorySource(k.kind))
    {
        uint64_t frame = 0;
        if (!ParseDecimal64(k.frame, &frame)) return "frame must be a decimal string";
        if (frame == 0) return "frame is 0: the capture could not say which frame it is from";
        if (k.kind == SourceKind::PausedLiveHead && !k.hasConnectionGeneration)
            return "a paused live head needs its connection_generation";
        if (k.contentSha256.empty()) return "a memory capture needs at least one region hash";
        for (const auto& c : k.contentSha256)
        {
            RegionId id;
            if (!RegionFromApiName(c.first, &id)) return "content_sha256 names an unknown region \"" + c.first + "\"";
        }
        if (k.kind == SourceKind::CompareSnapshot)
        {
            // A compare snapshot holds every region Memory Compare compares, and no others: its
            // identity must account for exactly those bytes.
            for (size_t i = 0; i < kRegionCount; ++i)
            {
                const RegionId id = static_cast<RegionId>(i);
                const bool held = k.contentSha256.count(RegionApiName(id)) != 0;
                if (InCompare(id) != held)
                    return std::string("a compare snapshot ") + (InCompare(id) ? "must hold " : "holds no ") + RegionApiName(id);
            }
        }
        return std::string();
    }

    if (!k.frame.empty()) return "a file has no frame";
    if (k.hasConnectionGeneration) return "a file has no connection_generation";
    if (k.contentSha256.size() != 1 || !k.contentSha256.count(kFileContentName))
        return "a file source has exactly one hash, named \"file\"";
    return std::string();
}

bool SameContent(const SourceKey& a, const SourceKey& b)
{
    return a.kind == b.kind && a.contentSha256 == b.contentSha256;
}

JsonValue SourceKeyToJson(const SourceKey& k)
{
    JsonValue o = JsonValue::Object();
    o.Set("kind", JsonValue::String(SourceKindName(k.kind)));
    o.Set("source_id", JsonValue::String(FormatDecimal(k.sourceId)));
    o.Set("derive_serial", JsonValue::String(FormatDecimal(k.deriveSerial)));
    o.Set("session_id", JsonValue::String(FormatDecimal(k.sessionId)));
    o.Set("timeline_epoch", JsonValue::String(FormatDecimal(k.timelineEpoch)));
    if (IsMemorySource(k.kind)) o.Set("frame", JsonValue::String(k.frame));
    if (k.hasConnectionGeneration) o.Set("connection_generation", JsonValue::String(FormatDecimal(k.connectionGeneration)));
    JsonValue h = JsonValue::Object();
    for (const auto& c : k.contentSha256) h.Set(c.first, JsonValue::String(c.second));
    o.Set("content_sha256", std::move(h));
    return o;
}

bool SourceKeyFromJson(const JsonValue& v, SourceKey* out, std::string* error)
{
    auto fail = [&](const std::string& m) { if (error) *error = "source key: " + m; return false; };
    if (!v.IsObject()) return fail("must be an object");
    SourceKey k;
    for (const JsonValue::Member& m : v.members())
    {
        const std::string& n = m.first;
        if (n != "kind" && n != "source_id" && n != "derive_serial" && n != "session_id" && n != "timeline_epoch" &&
            n != "frame" && n != "connection_generation" && n != "content_sha256")
            return fail("unknown field \"" + n + "\"");
    }
    const JsonValue* kind = v.Find("kind");
    if (!kind || !kind->IsString() || !SourceKindFromName(kind->AsString(), &k.kind)) return fail("\"kind\" is missing or unknown");

    // A counter that must be present and be a decimal string.
    auto counter = [&](const char* name, uint64_t* dst) {
        const JsonValue* c = v.Find(name);
        return c && c->IsString() && ParseDecimal64(c->AsString(), dst);
    };
    if (!counter("source_id", &k.sourceId) || !counter("derive_serial", &k.deriveSerial) ||
        !counter("session_id", &k.sessionId) || !counter("timeline_epoch", &k.timelineEpoch))
        return fail("a counter is missing or is not a decimal string");
    if (v.Has("connection_generation"))
    {
        if (!counter("connection_generation", &k.connectionGeneration)) return fail("connection_generation is not a decimal string");
        k.hasConnectionGeneration = true;
    }
    if (const JsonValue* f = v.Find("frame"))
    {
        if (!f->IsString()) return fail("frame must be a decimal string");
        k.frame = f->AsString();
    }
    const JsonValue* h = v.Find("content_sha256");
    if (!h || !h->IsObject()) return fail("\"content_sha256\" must be an object");
    for (const JsonValue::Member& m : h->members())
    {
        if (!m.second.IsString()) return fail("a content hash must be a string");
        k.contentSha256[m.first] = m.second.AsString();
    }
    const std::string problem = ValidateSourceKey(k);
    if (!problem.empty()) return fail(problem);
    if (out) *out = std::move(k);
    return true;
}

}  // namespace ai
}  // namespace sfe
