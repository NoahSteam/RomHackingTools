#include "AIInvestigation/PackageManifest.h"

#include <algorithm>
#include <cstdio>
#include <set>

#include "AIInvestigation/Encoding.h"
#include "Sha256.h"

namespace sfe
{
namespace ai
{

namespace layout
{
const char kManifestFile[] = "manifest.json";
const char kPromptFile[] = "prompt.md";
const char kEvidenceFile[] = "evidence.json";
const char kResponseSchemaFile[] = "response.schema.json";
const char kAttachmentsDir[] = "attachments";
const char kSourceDir[] = "source";
const char kDefaultInvestigationsDir[] = "investigations";
}  // namespace layout

bool IsPresetId(const std::string& s)
{
    if (s.empty() || s.size() > 48) return false;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const char c = s[i];
        const bool lower = c >= 'a' && c <= 'z';
        const bool digit = c >= '0' && c <= '9';
        if (!(lower || (i > 0 && (digit || c == '_')))) return false;
    }
    return true;
}

std::string ShortEvidenceId(const std::string& evidenceId)
{
    return IsUuid(evidenceId) ? evidenceId.substr(0, 8) : std::string();
}

std::string FormatUtc(const UtcTime& t)
{
    char b[32];
    std::snprintf(b, sizeof b, "%04d-%02d-%02dT%02d:%02d:%02dZ", t.year, t.month, t.day, t.hour, t.minute, t.second);
    return b;
}

bool IsUtcStamp(const std::string& s)
{
    if (s.size() != 20) return false;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const char c = s[i];
        const bool digit = c >= '0' && c <= '9';
        switch (i)
        {
        case 4: case 7: if (c != '-') return false; break;
        case 10: if (c != 'T') return false; break;
        case 13: case 16: if (c != ':') return false; break;
        case 19: if (c != 'Z') return false; break;
        default: if (!digit) return false;
        }
    }
    const int month = (s[5] - '0') * 10 + (s[6] - '0'), day = (s[8] - '0') * 10 + (s[9] - '0');
    const int hour = (s[11] - '0') * 10 + (s[12] - '0'), minute = (s[14] - '0') * 10 + (s[15] - '0');
    const int second = (s[17] - '0') * 10 + (s[18] - '0');
    return month >= 1 && month <= 12 && day >= 1 && day <= 31 && hour <= 23 && minute <= 59 && second <= 60;
}

std::string PackageFolderName(const UtcTime& t, const std::string& preset, const std::string& evidenceId)
{
    if (!IsPresetId(preset) || !IsUuid(evidenceId)) return std::string();
    if (t.year < 1970 || t.year > 9999 || t.month < 1 || t.month > 12 || t.day < 1 || t.day > 31 || t.hour < 0 || t.hour > 23 ||
        t.minute < 0 || t.minute > 59 || t.second < 0 || t.second > 60)
        return std::string();
    char b[24];
    std::snprintf(b, sizeof b, "%04d%02d%02d-%02d%02d%02d-", t.year, t.month, t.day, t.hour, t.minute, t.second);
    return std::string(b) + preset + "-" + ShortEvidenceId(evidenceId);
}

bool IsValidPackagePath(const std::string& path, std::string* why)
{
    auto fail = [&](const char* m) { if (why) *why = m; return false; };
    if (path.empty()) return fail("empty path");
    if (path[0] == '/') return fail("absolute path");
    if (path.size() >= 2 && path[1] == ':') return fail("drive-letter path");
    size_t start = 0;
    std::vector<std::string> segments;
    for (size_t i = 0; i <= path.size(); ++i)
    {
        const char c = i < path.size() ? path[i] : '/';
        if (i < path.size())
        {
            const unsigned char u = static_cast<unsigned char>(c);
            if (c == '\\') return fail("backslash in path (use '/')");
            if (u < 0x20 || u == 0x7F) return fail("control character in path");
            if (c == ':') return fail("':' in path");
        }
        if (c == '/')
        {
            const std::string seg = path.substr(start, i - start);
            if (seg.empty()) return fail("empty path segment");
            if (seg == "." || seg == "..") return fail("'.' or '..' in path");
            // A trailing dot or space is silently dropped by Windows, so "x." and "x" would be one file.
            if (seg.back() == '.' || seg.back() == ' ') return fail("path segment ends in '.' or space");
            segments.push_back(seg);
            start = i + 1;
        }
    }
    const std::string& top = segments[0];
    if (segments.size() == 1)
    {
        if (top == layout::kPromptFile || top == layout::kEvidenceFile || top == layout::kResponseSchemaFile) return true;
        if (top == layout::kManifestFile) return fail("the manifest does not list itself");
        return fail("not one of the package's fixed files");
    }
    if (top != layout::kAttachmentsDir && top != layout::kSourceDir) return fail("outside attachments/ and source/");
    return true;
}

const ManifestSource* Manifest::FindSource(const std::string& id) const
{
    for (const ManifestSource& s : sources) if (s.id == id) return &s;
    return nullptr;
}
const ManifestItem* Manifest::FindItem(const std::string& id) const
{
    for (const ManifestItem& i : items) if (i.id == id) return &i;
    return nullptr;
}
const ManifestFile* Manifest::FindFile(const std::string& path) const
{
    for (const ManifestFile& f : files) if (f.path == path) return &f;
    return nullptr;
}

std::vector<std::string> ValidateManifest(const Manifest& m)
{
    std::vector<std::string> problems;
    auto bad = [&](const std::string& p) { problems.push_back(p); };

    if (m.packageVersion != layout::kPackageVersion) bad("package_version must be " + std::to_string(layout::kPackageVersion));
    if (m.responseSchemaVersion != 0 && m.responseSchemaVersion != layout::kResponseSchemaVersion)
        bad("response_schema_version must be absent or " + std::to_string(layout::kResponseSchemaVersion));
    if (!IsUuid(m.investigationId)) bad("investigation_id is not a UUID");
    if (!IsUuid(m.evidenceId)) bad("evidence_id is not a UUID");
    if (!IsPresetId(m.preset)) bad("preset is not a preset id");
    if (!IsUtcStamp(m.createdUtc)) bad("created_utc is not YYYY-MM-DDTHH:MM:SSZ");
    if (m.folderName.empty()) bad("folder_name is empty");
    else if (IsUuid(m.evidenceId) && IsPresetId(m.preset) &&
             (m.folderName.size() < 9 || m.folderName.compare(m.folderName.size() - 9, 9, "-" + ShortEvidenceId(m.evidenceId)) != 0 ||
              m.folderName.find("-" + m.preset + "-") == std::string::npos))
        bad("folder_name does not end in -<preset>-<short evidence id>");
    if (m.hasParent && (!IsUuid(m.parent.evidenceId) || !IsUuid(m.parent.responseId))) bad("parent ids are not UUIDs");

    std::set<std::string> filePaths;
    for (const ManifestFile& f : m.files)
    {
        std::string why;
        if (!IsValidPackagePath(f.path, &why)) bad("file \"" + f.path + "\": " + why);
        if (!filePaths.insert(f.path).second) bad("file \"" + f.path + "\" is listed twice");
        if (!IsSha256Hex(f.sha256)) bad("file \"" + f.path + "\": sha256 is not 64 lower-case hex digits");
    }

    std::set<std::string> sourceIds;
    for (const ManifestSource& s : m.sources)
    {
        if (!IsHandle(s.id)) bad("source id \"" + s.id + "\" is not a handle");
        if (!sourceIds.insert(s.id).second) bad("source \"" + s.id + "\" is listed twice");
        const std::string why = ValidateSourceKey(s.key);
        if (!why.empty()) bad("source \"" + s.id + "\": " + why);
    }

    std::set<std::string> itemIds;
    for (const ManifestItem& item : m.items)
    {
        const std::string at = "item \"" + item.id + "\": ";
        if (!IsHandle(item.id)) bad("item id \"" + item.id + "\" is not a handle");
        if (!itemIds.insert(item.id).second) bad(at + "listed twice");
        if (!IsHandle(item.kind)) bad(at + "kind is not a handle");
        if (item.title.size() > 256) bad(at + "title is longer than 256 bytes");

        if (item.configuration)
        {
            if (!item.sources.empty()) bad(at + "a configuration item names no source (it is not read from a capture)");
            if (!IsUtcStamp(item.generatedUtc)) bad(at + "a configuration item needs its own generated_utc stamp");
            if (!item.ranges.empty()) bad(at + "a configuration item has no memory ranges");
        }
        else
        {
            if (item.sources.empty()) bad(at + "names no source: every item records the capture it was read from");
            if (!item.generatedUtc.empty()) bad(at + "only a configuration item carries generated_utc");
        }
        std::set<std::string> itemSources;
        for (const std::string& sid : item.sources)
        {
            if (!sourceIds.count(sid)) bad(at + "names unknown source \"" + sid + "\"");
            if (!itemSources.insert(sid).second) bad(at + "names source \"" + sid + "\" twice");
        }
        for (const std::string& f : item.files)
            if (!filePaths.count(f)) bad(at + "file \"" + f + "\" is not in the package's file list");

        std::set<std::string> rangeIds;
        for (const ManifestRange& r : item.ranges)
        {
            const std::string rat = at + "range \"" + r.id + "\": ";
            if (!IsHandle(r.id)) bad(rat + "id is not a handle");
            if (!rangeIds.insert(r.id).second) bad(rat + "listed twice");
            if (r.sources.empty() || r.sources.size() > 2) bad(rat + "names one source, or two for a diff range");
            std::set<std::string> seen;
            for (const std::string& sid : r.sources)
            {
                if (!itemSources.count(sid)) bad(rat + "source \"" + sid + "\" is not one of the item's sources");
                if (!seen.insert(sid).second) bad(rat + "names source \"" + sid + "\" twice");
                if (const ManifestSource* src = m.FindSource(sid))
                {
                    if (!IsMemorySource(src->key.kind)) bad(rat + "source \"" + sid + "\" is a file; memory ranges come from emulator captures");
                    else if (!src->key.contentSha256.count(RegionApiName(r.region)))
                        bad(rat + "source \"" + sid + "\" holds no " + RegionApiName(r.region));
                }
            }
            CanonicalTarget start;
            start.region = r.region;
            start.offset = r.offset;
            if (r.length == 0 || !RangeFits(start, r.length)) bad(rat + "does not fit inside " + RegionApiName(r.region));
            if (!r.file.empty() || !r.sha256.empty())
            {
                if (r.sources.size() != 1) bad(rat + "an attached range comes from exactly one source");
                if (!IsSha256Hex(r.sha256)) bad(rat + "sha256 is not 64 lower-case hex digits");
            }
            if (!r.file.empty())
            {
                const ManifestFile* f = m.FindFile(r.file);
                if (!f) bad(rat + "file \"" + r.file + "\" is not in the package's file list");
                else
                {
                    if (f->size != r.length) bad(rat + "its file's size is not the range's length");
                    if (f->sha256 != r.sha256) bad(rat + "its file's sha256 is not the range's");
                    if (r.file.compare(0, 12, "attachments/") != 0 && r.file.compare(0, 7, "source/") != 0)
                        bad(rat + "a range file lives under attachments/ or source/");
                }
            }
            if (r.kind == RangeKind::Code)
            {
                if (r.sources.size() != 1 || !IsSha256Hex(r.sha256)) bad(rat + "a code range comes from one source and carries the hash of its bytes");
                if (!(Traits(r.region).caps & kCapSh2Code)) bad(rat + RegionApiName(r.region) + " cannot hold SH-2 code");
            }
        }
        for (const auto& o : item.coverage.omitted)
            if (o.second == 0) bad(at + "an omitted range has zero length");
    }
    return problems;
}

// ---- JSON ---------------------------------------------------------------------------------------

JsonValue ManifestToJson(const Manifest& m)
{
    JsonValue o = JsonValue::Object();
    o.Set("package_version", JsonValue::UInt(static_cast<uint64_t>(m.packageVersion)));
    if (m.responseSchemaVersion) o.Set("response_schema_version", JsonValue::UInt(static_cast<uint64_t>(m.responseSchemaVersion)));
    o.Set("investigation_id", JsonValue::String(m.investigationId));
    o.Set("evidence_id", JsonValue::String(m.evidenceId));
    o.Set("folder_name", JsonValue::String(m.folderName));
    o.Set("preset", JsonValue::String(m.preset));
    o.Set("created_utc", JsonValue::String(m.createdUtc));
    if (m.hasParent)
    {
        JsonValue p = JsonValue::Object();
        p.Set("evidence_id", JsonValue::String(m.parent.evidenceId));
        p.Set("response_id", JsonValue::String(m.parent.responseId));
        o.Set("parent", std::move(p));
    }
    JsonValue files = JsonValue::Array();
    for (const ManifestFile& f : m.files)
    {
        JsonValue j = JsonValue::Object();
        j.Set("path", JsonValue::String(f.path));
        j.Set("size", JsonValue::String(FormatHex(f.size)));
        j.Set("sha256", JsonValue::String(f.sha256));
        files.Append(std::move(j));
    }
    o.Set("files", std::move(files));
    JsonValue sources = JsonValue::Array();
    for (const ManifestSource& s : m.sources)
    {
        JsonValue j = JsonValue::Object();
        j.Set("id", JsonValue::String(s.id));
        j.Set("key", SourceKeyToJson(s.key));
        sources.Append(std::move(j));
    }
    o.Set("sources", std::move(sources));
    JsonValue items = JsonValue::Array();
    for (const ManifestItem& it : m.items)
    {
        JsonValue j = JsonValue::Object();
        j.Set("id", JsonValue::String(it.id));
        j.Set("kind", JsonValue::String(it.kind));
        j.Set("title", JsonValue::String(it.title));
        JsonValue srcs = JsonValue::Array();
        for (const std::string& s : it.sources) srcs.Append(JsonValue::String(s));
        j.Set("sources", std::move(srcs));
        if (it.configuration)
        {
            j.Set("configuration", JsonValue::Bool(true));
            j.Set("generated_utc", JsonValue::String(it.generatedUtc));
        }
        JsonValue ranges = JsonValue::Array();
        for (const ManifestRange& r : it.ranges)
        {
            JsonValue rj = JsonValue::Object();
            rj.Set("id", JsonValue::String(r.id));
            JsonValue rs = JsonValue::Array();
            for (const std::string& s : r.sources) rs.Append(JsonValue::String(s));
            rj.Set("sources", std::move(rs));
            rj.Set("region", JsonValue::String(RegionApiName(r.region)));
            rj.Set("offset", JsonValue::String(FormatHex(r.offset)));
            rj.Set("length", JsonValue::String(FormatHex(r.length)));
            rj.Set("kind", JsonValue::String(r.kind == RangeKind::Code ? "code" : "data"));
            if (!r.file.empty()) rj.Set("file", JsonValue::String(r.file));
            if (!r.sha256.empty()) rj.Set("sha256", JsonValue::String(r.sha256));
            ranges.Append(std::move(rj));
        }
        j.Set("ranges", std::move(ranges));
        JsonValue files2 = JsonValue::Array();
        for (const std::string& f : it.files) files2.Append(JsonValue::String(f));
        j.Set("files", std::move(files2));
        JsonValue cov = JsonValue::Object();
        cov.Set("complete", JsonValue::Bool(it.coverage.complete));
        JsonValue om = JsonValue::Array();
        for (const auto& p : it.coverage.omitted)
        {
            JsonValue oj = JsonValue::Object();
            oj.Set("offset", JsonValue::String(FormatHex(p.first)));
            oj.Set("length", JsonValue::String(FormatHex(p.second)));
            om.Append(std::move(oj));
        }
        cov.Set("omitted", std::move(om));
        j.Set("coverage", std::move(cov));
        items.Append(std::move(j));
    }
    o.Set("items", std::move(items));
    return o;
}

namespace
{

struct Reader
{
    std::string error;
    bool Fail(const std::string& m) { if (error.empty()) error = m; return false; }

    // Rejects any member not in 'allowed'.
    bool Known(const JsonValue& o, const char* where, std::initializer_list<const char*> allowed)
    {
        for (const JsonValue::Member& m : o.members())
        {
            bool ok = false;
            for (const char* a : allowed) ok = ok || m.first == a;
            if (!ok) return Fail(std::string(where) + ": unknown member \"" + m.first + "\"");
        }
        return true;
    }
    bool Str(const JsonValue& o, const char* where, const char* key, std::string* out, bool required = true)
    {
        const JsonValue* v = o.Find(key);
        if (!v) return required ? Fail(std::string(where) + ": missing \"" + key + "\"") : true;
        if (!v->IsString()) return Fail(std::string(where) + ": \"" + key + "\" must be a string");
        *out = v->AsString();
        return true;
    }
    bool Hex(const JsonValue& o, const char* where, const char* key, uint64_t* out)
    {
        std::string s, why;
        if (!Str(o, where, key, &s)) return false;
        if (!ParseHex64(s, out, &why)) return Fail(std::string(where) + ": \"" + key + "\": " + why);
        return true;
    }
    bool Hex32(const JsonValue& o, const char* where, const char* key, uint32_t* out)
    {
        std::string s, why;
        if (!Str(o, where, key, &s)) return false;
        if (!ParseHex32(s, out, &why)) return Fail(std::string(where) + ": \"" + key + "\": " + why);
        return true;
    }
    bool StrList(const JsonValue& o, const char* where, const char* key, std::vector<std::string>* out)
    {
        const JsonValue* v = o.Find(key);
        if (!v) return Fail(std::string(where) + ": missing \"" + key + "\"");
        if (!v->IsArray()) return Fail(std::string(where) + ": \"" + key + "\" must be an array");
        for (const JsonValue& e : v->items())
        {
            if (!e.IsString()) return Fail(std::string(where) + ": \"" + key + "\" holds a non-string");
            out->push_back(e.AsString());
        }
        return true;
    }
};

}  // namespace

bool ManifestFromJson(const JsonValue& j, Manifest* out, std::string* error)
{
    Reader r;
    Manifest m;
    auto done = [&](bool ok) {
        if (!ok && error) *error = r.error;
        return ok;
    };
    if (!j.IsObject()) return done(r.Fail("manifest must be an object"));
    if (!r.Known(j, "manifest", { "package_version", "response_schema_version", "investigation_id", "evidence_id", "folder_name",
                                 "preset", "created_utc", "parent", "files", "sources", "items" }))
        return done(false);

    uint64_t n = 0;
    const JsonValue* pv = j.Find("package_version");
    if (!pv || !pv->ToUint64(&n) || n > 1000) return done(r.Fail("manifest: package_version must be a small integer"));
    m.packageVersion = static_cast<int>(n);
    if (const JsonValue* sv = j.Find("response_schema_version"))
    {
        if (!sv->ToUint64(&n) || n == 0 || n > 1000) return done(r.Fail("manifest: response_schema_version must be a positive integer"));
        m.responseSchemaVersion = static_cast<int>(n);
    }
    if (!r.Str(j, "manifest", "investigation_id", &m.investigationId) || !r.Str(j, "manifest", "evidence_id", &m.evidenceId) ||
        !r.Str(j, "manifest", "folder_name", &m.folderName) || !r.Str(j, "manifest", "preset", &m.preset) ||
        !r.Str(j, "manifest", "created_utc", &m.createdUtc))
        return done(false);
    if (const JsonValue* p = j.Find("parent"))
    {
        if (!p->IsObject() || !r.Known(*p, "parent", { "evidence_id", "response_id" }) ||
            !r.Str(*p, "parent", "evidence_id", &m.parent.evidenceId) || !r.Str(*p, "parent", "response_id", &m.parent.responseId))
            return done(r.Fail("manifest: parent is malformed"));
        m.hasParent = true;
    }

    const JsonValue* files = j.Find("files");
    if (!files || !files->IsArray()) return done(r.Fail("manifest: \"files\" must be an array"));
    for (const JsonValue& f : files->items())
    {
        ManifestFile mf;
        if (!f.IsObject() || !r.Known(f, "file", { "path", "size", "sha256" }) || !r.Str(f, "file", "path", &mf.path) ||
            !r.Hex(f, "file", "size", &mf.size) || !r.Str(f, "file", "sha256", &mf.sha256))
            return done(r.Fail("manifest: a file entry is malformed"));
        m.files.push_back(std::move(mf));
    }
    const JsonValue* sources = j.Find("sources");
    if (!sources || !sources->IsArray()) return done(r.Fail("manifest: \"sources\" must be an array"));
    for (const JsonValue& s : sources->items())
    {
        ManifestSource ms;
        if (!s.IsObject() || !r.Known(s, "source", { "id", "key" }) || !r.Str(s, "source", "id", &ms.id) || !s.Find("key"))
            return done(r.Fail("manifest: a source entry is malformed"));
        std::string why;
        if (!SourceKeyFromJson(*s.Find("key"), &ms.key, &why)) return done(r.Fail("manifest: " + why));
        m.sources.push_back(std::move(ms));
    }
    const JsonValue* items = j.Find("items");
    if (!items || !items->IsArray()) return done(r.Fail("manifest: \"items\" must be an array"));
    for (const JsonValue& it : items->items())
    {
        ManifestItem mi;
        if (!it.IsObject() || !r.Known(it, "item", { "id", "kind", "title", "sources", "configuration", "generated_utc", "ranges", "files", "coverage" }) ||
            !r.Str(it, "item", "id", &mi.id) || !r.Str(it, "item", "kind", &mi.kind) || !r.Str(it, "item", "title", &mi.title) ||
            !r.StrList(it, "item", "sources", &mi.sources) || !r.StrList(it, "item", "files", &mi.files) ||
            !r.Str(it, "item", "generated_utc", &mi.generatedUtc, false))
            return done(r.Fail("manifest: an item is malformed"));
        if (const JsonValue* c = it.Find("configuration"))
        {
            if (!c->IsBool()) return done(r.Fail("manifest: item \"configuration\" must be a boolean"));
            mi.configuration = c->AsBool();
        }
        const JsonValue* ranges = it.Find("ranges");
        if (!ranges || !ranges->IsArray()) return done(r.Fail("manifest: item \"ranges\" must be an array"));
        for (const JsonValue& rj : ranges->items())
        {
            ManifestRange mr;
            std::string region, kind;
            if (!rj.IsObject() || !r.Known(rj, "range", { "id", "sources", "region", "offset", "length", "kind", "file", "sha256" }) ||
                !r.Str(rj, "range", "id", &mr.id) || !r.StrList(rj, "range", "sources", &mr.sources) ||
                !r.Str(rj, "range", "region", &region) || !r.Hex32(rj, "range", "offset", &mr.offset) ||
                !r.Hex32(rj, "range", "length", &mr.length) || !r.Str(rj, "range", "kind", &kind) ||
                !r.Str(rj, "range", "file", &mr.file, false) || !r.Str(rj, "range", "sha256", &mr.sha256, false))
                return done(r.Fail("manifest: a range is malformed"));
            if (!RegionFromApiName(region, &mr.region)) return done(r.Fail("manifest: unknown region \"" + region + "\""));
            if (kind == "code") mr.kind = RangeKind::Code;
            else if (kind == "data") mr.kind = RangeKind::Data;
            else return done(r.Fail("manifest: range kind must be \"data\" or \"code\""));
            mi.ranges.push_back(std::move(mr));
        }
        const JsonValue* cov = it.Find("coverage");
        if (!cov || !cov->IsObject() || !r.Known(*cov, "coverage", { "complete", "omitted" }) || !cov->Find("complete") ||
            !cov->Find("complete")->IsBool() || !cov->Find("omitted") || !cov->Find("omitted")->IsArray())
            return done(r.Fail("manifest: item coverage is malformed"));
        mi.coverage.complete = cov->Find("complete")->AsBool();
        for (const JsonValue& oj : cov->Find("omitted")->items())
        {
            uint32_t off = 0, len = 0;
            if (!oj.IsObject() || !r.Known(oj, "omitted range", { "offset", "length" }) || !r.Hex32(oj, "omitted range", "offset", &off) ||
                !r.Hex32(oj, "omitted range", "length", &len))
                return done(r.Fail("manifest: an omitted range is malformed"));
            mi.coverage.omitted.emplace_back(off, len);
        }
        m.items.push_back(std::move(mi));
    }

    const std::vector<std::string> problems = ValidateManifest(m);
    if (!problems.empty()) return done(r.Fail("manifest: " + problems.front()));
    if (out) *out = std::move(m);
    return true;
}

bool ResolveEvidenceRef(const Manifest& m, const std::string& ref, ResolvedRef* out, std::string* why)
{
    auto fail = [&](const std::string& msg) { if (why) *why = msg; return false; };
    const size_t slash = ref.find('/');
    const std::string itemId = ref.substr(0, slash);
    const std::string rangeId = slash == std::string::npos ? std::string() : ref.substr(slash + 1);
    if (ref.empty() || !IsHandle(itemId) || (slash != std::string::npos && !IsHandle(rangeId)))
        return fail("\"" + ref + "\" is not an evidence reference (use \"<item id>\" or \"<item id>/<range id>\")");
    const ManifestItem* item = m.FindItem(itemId);
    if (!item) return fail("evidence item \"" + itemId + "\" is not in this package");
    ResolvedRef r;
    r.item = item;
    if (slash != std::string::npos)
    {
        for (const ManifestRange& range : item->ranges)
            if (range.id == rangeId) r.range = &range;
        if (!r.range) return fail("evidence item \"" + itemId + "\" has no range \"" + rangeId + "\"");
    }
    if (out) *out = r;
    return true;
}

}  // namespace ai
}  // namespace sfe
