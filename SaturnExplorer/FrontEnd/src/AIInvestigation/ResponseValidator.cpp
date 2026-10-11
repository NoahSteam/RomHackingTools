#include "AIInvestigation/ResponseValidator.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

#include "AIInvestigation/Encoding.h"
#include "Debug/ConditionEval.h"
#include "Debug/FormatString.h"
#include "Sha256.h"

namespace sfe
{
namespace ai
{

// ---- Step 1: extraction ---------------------------------------------------------------------------

namespace
{

std::string TrimmedCopy(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

std::string Lower(std::string s)
{
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// A fence line is up to three spaces of indent, then at least three backticks. Returns the run length
// (0 when the line is not a fence) and the text after the run.
size_t FenceRun(const std::string& line, std::string* rest)
{
    size_t i = 0;
    while (i < line.size() && i < 4 && line[i] == ' ') ++i;
    if (i > 3) return 0;
    size_t n = 0;
    while (i + n < line.size() && line[i + n] == '`') ++n;
    if (n < 3) return 0;
    if (rest) *rest = line.substr(i + n);
    return n;
}

}  // namespace

Extraction ExtractResponseJson(const std::string& input)
{
    Extraction r;
    std::string text = input;
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);   // a copied BOM is invisible; do not make the user find it
    const std::string trimmed = TrimmedCopy(text);
    if (trimmed.empty())
    {
        r.error = "the response is empty";
        return r;
    }
    if (trimmed[0] == '{')
    {
        r.ok = true;
        r.json = trimmed;
        return r;
    }

    std::vector<std::string> lines;
    {
        std::string cur;
        for (char c : text)
        {
            if (c == '\n') { if (!cur.empty() && cur.back() == '\r') cur.pop_back(); lines.push_back(cur); cur.clear(); }
            else cur += c;
        }
        if (!cur.empty() && cur.back() == '\r') cur.pop_back();
        lines.push_back(cur);
    }

    std::vector<std::string> blocks;
    bool inFence = false, jsonBlock = false;
    size_t fenceLen = 0;
    std::string body;
    for (const std::string& line : lines)
    {
        std::string rest;
        const size_t run = FenceRun(line, &rest);
        if (!inFence)
        {
            if (run == 0) continue;
            if (rest.find('`') != std::string::npos) continue;   // `` ```x``` `` on one line is inline code, not a fence
            inFence = true;
            fenceLen = run;
            const std::string info = TrimmedCopy(rest);
            const std::string word = info.substr(0, info.find_first_of(" \t"));
            jsonBlock = Lower(word) == "json";
            body.clear();
        }
        else if (run >= fenceLen && TrimmedCopy(rest).empty())
        {
            if (jsonBlock) blocks.push_back(body);
            inFence = false;
            jsonBlock = false;
        }
        else if (jsonBlock)
        {
            body += line;
            body += '\n';
        }
    }
    if (inFence)
    {
        r.error = "a fenced code block is never closed";
        return r;
    }
    if (blocks.empty())
    {
        r.error = "no JSON found: reply with one JSON document, either bare or in a single ```json fenced block";
        return r;
    }
    if (blocks.size() > 1)
    {
        r.error = "found " + std::to_string(blocks.size()) + " ```json fenced blocks; exactly one is allowed";
        return r;
    }
    r.ok = true;
    r.fenced = true;
    r.json = blocks[0];
    return r;
}

const char* IssueCodeName(IssueCode c)
{
    switch (c)
    {
    case IssueCode::NotJson: return "not_json";
    case IssueCode::UnsupportedInSchemaV1: return "unsupported_in_schema_v1";
    case IssueCode::UnknownField: return "unknown_field";
    case IssueCode::MissingField: return "missing_field";
    case IssueCode::WrongType: return "wrong_type";
    case IssueCode::BadValue: return "bad_value";
    case IssueCode::BadSchemaVersion: return "bad_schema_version";
    case IssueCode::InvestigationIdMismatch: return "investigation_id_mismatch";
    case IssueCode::EvidenceIdMismatch: return "evidence_id_mismatch";
    case IssueCode::UnknownEvidenceRef: return "unknown_evidence_ref";
    case IssueCode::UnknownActionType: return "unknown_action_type";
    case IssueCode::DuplicateId: return "duplicate_id";
    case IssueCode::UnknownHypothesis: return "unknown_hypothesis";
    case IssueCode::UnknownDependency: return "unknown_dependency";
    case IssueCode::DependencyCycle: return "dependency_cycle";
    case IssueCode::BadTarget: return "bad_target";
    case IssueCode::MissingCapability: return "missing_capability";
    case IssueCode::Misaligned: return "misaligned";
    case IssueCode::Grammar: return "grammar";
    case IssueCode::Unparseable: return "unparseable";
    case IssueCode::RejectedParameter: return "rejected_parameter";
    case IssueCode::LimitExceeded: return "limit_exceeded";
    case IssueCode::RangeOutOfRegion: return "range_out_of_region";
    case IssueCode::UnknownSource: return "unknown_source";
    case IssueCode::DuplicateSemanticKey: return "duplicate_semantic_key";
    case IssueCode::DuplicateLabelName: return "duplicate_label_name";
    case IssueCode::TooManyTracepoints: return "too_many_tracepoints";
    case IssueCode::CodeEvidenceMissing: return "code_evidence_missing";
    case IssueCode::FingerprintMismatch: return "fingerprint_mismatch";
    }
    return "?";
}

// ---- Steps 3-5 ------------------------------------------------------------------------------------

namespace
{

// The bytes a typed value reads (WatchTypeSize in Debug/WatchList.h; a test pins the two together).
uint32_t DataTypeSize(const std::string& t)
{
    if (t == "u8" || t == "s8") return 1;
    if (t == "u16" || t == "s16" || t == "rgb555") return 2;
    if (t == "u32" || t == "s32" || t == "pointer") return 4;
    return 0;
}

// A string parameter's text, or "" when it is absent or not a string.
std::string StringParam(const JsonValue& params, const char* key)
{
    const JsonValue* v = params.Find(key);
    return v && v->IsString() ? v->AsString() : std::string();
}

bool IsNamespacedKey(const std::string& k)
{
    // [a-z0-9][a-z0-9_-]*(\.[a-z0-9][a-z0-9_-]*)+
    size_t segments = 0;
    size_t i = 0;
    while (i <= k.size())
    {
        size_t j = k.find('.', i);
        if (j == std::string::npos) j = k.size();
        if (j == i) return false;
        for (size_t p = i; p < j; ++p)
        {
            const char c = k[p];
            const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
            if (!(alnum || (p > i && (c == '_' || c == '-')))) return false;
        }
        ++segments;
        if (j == k.size()) break;
        i = j + 1;
    }
    return segments >= 2;
}

class Checker
{
public:
    Checker(const Manifest& manifest, const ValidateOptions& options, ValidationResult* result)
        : mManifest(manifest), mOptions(options), mResult(result) {}

    void Run(const JsonValue& doc);

private:
    // Records a problem. While an action is being checked it is tagged with that action and marks it
    // invalid; whole-response checks use IssueOn() with the action's index.
    void Issue(IssueCode code, const std::string& where, const std::string& message)
    {
        ValidationIssue i;
        i.code = code;
        i.where = where;
        i.message = message;
        if (mInAction)
        {
            i.actionId = mActionId;
            mActionInvalid = true;
        }
        mResult->issues.push_back(std::move(i));
    }

    void IssueOn(size_t actionIndex, IssueCode code, const std::string& where, const std::string& message)
    {
        ValidationIssue i;
        i.code = code;
        i.where = where;
        i.actionId = mResult->actions[actionIndex].id;
        i.message = message;
        mResult->issues.push_back(std::move(i));
        mResult->actions[actionIndex].valid = false;
    }

    bool Object(const JsonValue& v, const std::string& where, const char* what)
    {
        if (v.IsObject()) return true;
        Issue(IssueCode::WrongType, where, std::string(what) + " must be an object");
        return false;
    }

    // Reports every member of 'o' not in 'allowed'. "experiment" has its own message.
    void CheckFields(const JsonValue& o, const std::string& where, std::initializer_list<const char*> allowed)
    {
        for (const JsonValue::Member& m : o.members())
        {
            bool ok = false;
            for (const char* a : allowed) ok = ok || m.first == a;
            if (ok) continue;
            if (m.first == "experiment")
                Issue(IssueCode::UnsupportedInSchemaV1, where + ".experiment", "\"experiment\" is unsupported in schema v1");
            else
                Issue(IssueCode::UnknownField, where + "." + m.first, "unknown field \"" + m.first + "\"");
        }
    }

    const JsonValue* Require(const JsonValue& o, const std::string& where, const char* key)
    {
        const JsonValue* v = o.Find(key);
        if (!v) Issue(IssueCode::MissingField, where + "." + key, std::string("missing \"") + key + "\"");
        return v;
    }

    bool StringField(const JsonValue* v, const std::string& where)
    {
        if (!v) return false;
        if (!v->IsString()) { Issue(IssueCode::WrongType, where, "must be a string"); return false; }
        return true;
    }

    void CheckExtensions(const JsonValue& o, const std::string& where)
    {
        const JsonValue* e = o.Find("extensions");
        if (!e) return;
        if (!e->IsObject()) { Issue(IssueCode::WrongType, where + ".extensions", "extensions must be an object"); return; }
        for (const JsonValue::Member& m : e->members())
            if (!IsNamespacedKey(m.first))
                Issue(IssueCode::Grammar, where + ".extensions." + m.first,
                      "extension keys are namespaced, like \"com.example.tool\"");
    }

    void CheckRefs(const JsonValue& o, const std::string& where, std::vector<std::string>* out);
    void CheckHypotheses(const JsonValue& list);
    void CheckAction(const JsonValue& a, size_t index);
    void CheckParameters(const ActionSpec& spec, const JsonValue& params, const std::string& where);
    void CheckParam(const ParamSpec& p, const JsonValue& v, const std::string& where);
    void CheckCrossRules(const ActionSpec& spec, ValidatedAction& act, const std::string& where);
    void CheckRequestSource(const ActionSpec& spec, const ValidatedAction& act, const std::string& where);
    void CheckTarget(const ActionSpec& spec, const JsonValue& target, const std::string& where, ValidatedAction& act);
    void DeriveFingerprint(const JsonValue& actionObject, ValidatedAction& act, const std::string& where);
    void CheckDependencies();

    const ValidatedHypothesis* FindHypothesis(const std::string& id) const
    {
        for (const ValidatedHypothesis& h : mResult->hypotheses) if (h.id == id) return &h;
        return nullptr;
    }

    const Manifest& mManifest;
    const ValidateOptions& mOptions;
    ValidationResult* mResult;
    bool mInAction = false;        // CheckAction is running: issues belong to mActionId
    bool mActionInvalid = false;
    std::string mActionId;
};

void Checker::CheckRefs(const JsonValue& o, const std::string& where, std::vector<std::string>* out)
{
    const JsonValue* refs = o.Find("evidence_refs");
    if (!refs) return;
    if (!refs->IsArray()) { Issue(IssueCode::WrongType, where + ".evidence_refs", "evidence_refs must be an array"); return; }
    if (refs->items().size() > mOptions.limits.maxEvidenceRefs)
        Issue(IssueCode::LimitExceeded, where + ".evidence_refs", "more than " + std::to_string(mOptions.limits.maxEvidenceRefs) + " evidence references");
    for (size_t i = 0; i < refs->items().size(); ++i)
    {
        const JsonValue& r = refs->items()[i];
        const std::string at = where + ".evidence_refs[" + std::to_string(i) + "]";
        if (!r.IsString()) { Issue(IssueCode::WrongType, at, "an evidence reference is a string"); continue; }
        std::string why;
        if (!ResolveEvidenceRef(mManifest, r.AsString(), nullptr, &why))
            Issue(IssueCode::UnknownEvidenceRef, at, why);
        else if (out)
            out->push_back(r.AsString());
    }
}

void Checker::CheckHypotheses(const JsonValue& list)
{
    if (!list.IsArray()) { Issue(IssueCode::WrongType, "$.hypotheses", "hypotheses must be an array"); return; }
    if (list.items().size() > mOptions.limits.maxHypotheses)
        Issue(IssueCode::LimitExceeded, "$.hypotheses", "more than " + std::to_string(mOptions.limits.maxHypotheses) + " hypotheses");
    std::set<std::string> ids;
    for (size_t i = 0; i < list.items().size(); ++i)
    {
        const JsonValue& h = list.items()[i];
        const std::string where = "$.hypotheses[" + std::to_string(i) + "]";
        if (!Object(h, where, "a hypothesis")) continue;
        CheckFields(h, where, { "id", "claim", "confidence", "evidence_refs", "prediction", "extensions" });
        ValidatedHypothesis v;
        if (const JsonValue* id = Require(h, where, "id"))
        {
            if (StringField(id, where + ".id"))
            {
                v.id = id->AsString();
                if (!IsHandle(v.id)) Issue(IssueCode::Grammar, where + ".id", "an id is 1-64 characters of letters, digits, '_', '.' and '-'");
                else if (!ids.insert(v.id).second) Issue(IssueCode::DuplicateId, where + ".id", "hypothesis id \"" + v.id + "\" is used twice");
            }
        }
        for (const char* key : { "claim", "prediction" })
        {
            const JsonValue* t = Require(h, where, key);
            if (!StringField(t, where + "." + key)) continue;
            std::string why;
            if (t->AsString().empty()) Issue(IssueCode::BadValue, where + "." + key, std::string(key) + " must not be empty");
            else if (!IsSingleLineText(t->AsString(), mOptions.limits.maxClaimChars, &why))
                Issue(IssueCode::Grammar, where + "." + key, why);
            (std::string(key) == "claim" ? v.claim : v.prediction) = t->AsString();
        }
        if (const JsonValue* c = Require(h, where, "confidence"))
        {
            if (StringField(c, where + ".confidence"))
            {
                v.confidence = c->AsString();
                if (v.confidence != "low" && v.confidence != "medium" && v.confidence != "high")
                    Issue(IssueCode::BadValue, where + ".confidence", "confidence is low, medium or high");
            }
        }
        Require(h, where, "evidence_refs");
        CheckRefs(h, where, &v.evidenceRefs);
        CheckExtensions(h, where);
        mResult->hypotheses.push_back(std::move(v));
    }
}

void Checker::CheckTarget(const ActionSpec& spec, const JsonValue& target, const std::string& where, ValidatedAction& act)
{
    const TargetResult t = ResolveTargetJson(target);
    if (!t.ok)
    {
        Issue(IssueCode::BadTarget, where, t.message);
        return;
    }
    act.hasTarget = true;
    act.target = t.target;
    if (spec.target.needsBus && !t.target.hasBus)
    {
        Issue(IssueCode::BadTarget, where, std::string(RegionApiName(t.target.region)) + " has no bus address; " + spec.type + " needs a bus-addressable location");
        return;
    }
    if (spec.target.evenAddress && (t.target.offset & 1u))
        Issue(IssueCode::Misaligned, where, std::string(spec.type) + " needs an even (instruction-aligned) address, not " + TargetToString(t.target));
}

void Checker::CheckParam(const ParamSpec& p, const JsonValue& v, const std::string& where)
{
    auto wrong = [&](const char* what) { Issue(IssueCode::WrongType, where, std::string("must be ") + what); };
    switch (p.type)
    {
    case ParamType::Name:
        if (!v.IsString()) return wrong("a string");
        if (!IsIdentifierName(v.AsString()))
            Issue(IssueCode::Grammar, where, "a name matches [A-Za-z_][A-Za-z0-9_]{0,63}");
        return;
    case ParamType::Text:
    case ParamType::Condition:
    case ParamType::Format:
    {
        if (!v.IsString()) return wrong("a string");
        const std::string& s = v.AsString();
        std::string why;
        if (s.empty()) { Issue(IssueCode::BadValue, where, "must not be empty"); return; }
        if (!IsSingleLineText(s, p.maxChars, &why)) { Issue(IssueCode::Grammar, where, why); return; }
        // ConditionValidate / FormatValidate are pure parsers; the length bound above is the complexity
        // bound, so a hostile 256-character input cannot make them run long.
        if (p.type == ParamType::Condition)
        {
            const std::string e = ConditionValidate(s);
            if (!e.empty()) Issue(IssueCode::Unparseable, where, "condition does not parse: " + e);
        }
        else if (p.type == ParamType::Format)
        {
            const std::string e = FormatValidate(s);
            if (!e.empty()) Issue(IssueCode::Unparseable, where, "format does not parse: " + e);
        }
        return;
    }
    case ParamType::Hex:
    {
        if (!v.IsString()) return wrong("a hex string");
        uint64_t x = 0;
        std::string why;
        if (!ParseHex64(v.AsString(), &x, &why)) { Issue(IssueCode::BadValue, where, why); return; }
        if (p.max && (x < p.min || x > p.max))
            Issue(IssueCode::BadValue, where, "must be between " + FormatHex(p.min) + " and " + FormatHex(p.max));
        return;
    }
    case ParamType::HexChoice:
    case ParamType::Enum:
    {
        if (!v.IsString()) return wrong("a string");
        for (const char* c : p.values)
            if (v.AsString() == c) return;
        std::string list;
        for (const char* c : p.values) list += (list.empty() ? "" : ", ") + std::string(c);
        Issue(IssueCode::BadValue, where, "must be one of " + list);
        return;
    }
    case ParamType::Integer:
    {
        uint64_t x = 0;
        if (!v.IsInteger() || !v.ToUint64(&x)) return wrong("a non-negative integer");
        if (x < p.min || x > p.max)
            Issue(IssueCode::BadValue, where, "must be between " + std::to_string(p.min) + " and " + std::to_string(p.max));
        return;
    }
    case ParamType::Bool:
        if (!v.IsBool()) wrong("true or false");
        return;
    case ParamType::Uuid:
        if (!v.IsString()) return wrong("a string");
        if (!IsUuid(v.AsString())) Issue(IssueCode::BadValue, where, "must be a lower-case UUID");
        return;
    case ParamType::Handle:
        if (!v.IsString()) return wrong("a string");
        if (!IsHandle(v.AsString())) Issue(IssueCode::Grammar, where, "an id is 1-64 characters of letters, digits, '_', '.' and '-'");
        return;
    case ParamType::RangeList:
    {
        if (!v.IsArray()) return wrong("an array");
        if (v.items().empty() || v.items().size() > kMaxRequestRanges)
        {
            Issue(IssueCode::LimitExceeded, where, "between 1 and " + std::to_string(kMaxRequestRanges) + " ranges");
            return;
        }
        for (size_t i = 0; i < v.items().size(); ++i)
        {
            const JsonValue& r = v.items()[i];
            const std::string at = where + "[" + std::to_string(i) + "]";
            if (!r.IsObject()) { Issue(IssueCode::WrongType, at, "a range is an object"); continue; }
            CheckFields(r, at, { "region", "offset", "length" });
            JsonValue loc = JsonValue::Object();
            if (const JsonValue* x = r.Find("region")) loc.Set("region", *x);
            if (const JsonValue* x = r.Find("offset")) loc.Set("offset", *x);
            const JsonValue* len = r.Find("length");
            if (!r.Find("region") || !r.Find("offset") || !len)
            {
                Issue(IssueCode::MissingField, at, "a range needs region, offset and length");
                continue;
            }
            const TargetResult t = ResolveTargetJson(loc);
            if (!t.ok) { Issue(IssueCode::BadTarget, at, t.message); continue; }
            uint32_t length = 0;
            std::string why;
            if (!len->IsString() || !ParseHex32(len->AsString(), &length, &why))
            {
                Issue(IssueCode::BadValue, at + ".length", len->IsString() ? why : "length must be a hex string");
                continue;
            }
            if (!RangeFits(t.target, length))
                Issue(IssueCode::RangeOutOfRegion, at, "[" + FormatHex(t.target.offset) + ", +" + FormatHex(length) + ") does not fit inside " +
                      RegionApiName(t.target.region));
        }
        return;
    }
    }
}

void Checker::CheckParameters(const ActionSpec& spec, const JsonValue& params, const std::string& where)
{
    for (const JsonValue::Member& m : params.members())
    {
        const ParamSpec* p = FindParam(spec, m.first);
        if (p)
        {
            CheckParam(*p, m.second, where + "." + m.first);
            continue;
        }
        const RejectedParam* rej = nullptr;
        for (const RejectedParam& r : spec.rejected) if (m.first == r.name) rej = &r;
        if (rej) Issue(IssueCode::RejectedParameter, where + "." + m.first, std::string("\"") + m.first + "\" is not accepted: " + rej->reason);
        else Issue(IssueCode::UnknownField, where + "." + m.first, "unknown parameter \"" + m.first + "\" for " + spec.type);
    }
    for (const ParamSpec& p : spec.params)
        if (p.required && !params.Has(p.name))
            Issue(IssueCode::MissingField, where + "." + p.name, std::string("missing required parameter \"") + p.name + "\"");
}

void Checker::CheckRequestSource(const ActionSpec& spec, const ValidatedAction& act, const std::string& where)
{
    const JsonValue* ev = act.parameters.Find("evidence_id");
    const JsonValue* src = act.parameters.Find("source");
    if (!src || !src->IsString()) return;
    if (ev && ev->IsString() && ev->AsString() != mManifest.evidenceId)
        return;   // names another package; its manifest is not the one validated against, so the source is resolved when it is loaded
    const ManifestSource* s = mManifest.FindSource(src->AsString());
    if (!s)
    {
        Issue(IssueCode::UnknownSource, where + ".source", "this package has no source \"" + src->AsString() + "\"");
        return;
    }
    if (!IsMemorySource(s->key.kind))
    {
        Issue(IssueCode::UnknownSource, where + ".source", "source \"" + s->id + "\" is a file; this request is for emulator memory");
        return;
    }
    auto holds = [&](RegionId r) { return s->key.contentSha256.count(RegionApiName(r)) != 0; };
    if (std::string(spec.type) == "request_code_evidence")
    {
        if (act.hasTarget && !holds(act.target.region))
            Issue(IssueCode::UnknownSource, where + ".source", "source \"" + s->id + "\" holds no " + RegionApiName(act.target.region));
        return;
    }
    const JsonValue* ranges = act.parameters.Find("ranges");
    if (!ranges || !ranges->IsArray()) return;
    for (size_t i = 0; i < ranges->items().size(); ++i)
    {
        const JsonValue* region = ranges->items()[i].IsObject() ? ranges->items()[i].Find("region") : nullptr;
        RegionId id;
        if (region && region->IsString() && RegionFromApiName(region->AsString(), &id) && !holds(id))
            Issue(IssueCode::UnknownSource, where + ".ranges[" + std::to_string(i) + "]",
                  "source \"" + s->id + "\" holds no " + region->AsString());
    }
}

void Checker::CheckCrossRules(const ActionSpec& spec, ValidatedAction& act, const std::string& where)
{
    const std::string type = spec.type;
    const JsonValue& p = act.parameters;
    auto str = [&](const char* k) { return StringParam(p, k); };
    auto num = [&](const char* k, uint64_t* out) { const JsonValue* v = p.Find(k); return v && v->ToUint64(out); };

    if (type == "add_address_label" && act.hasTarget)
    {
        const std::string dt = str("data_type");
        uint64_t count = 1, size = 0;
        const bool hasCount = num("count", &count);
        const bool hasSize = p.Has("size") && ParseHex64(str("size"), &size);
        if (hasCount && dt.empty())
            Issue(IssueCode::BadValue, where + ".count", "count needs data_type");
        uint64_t covers = 1;
        if (!dt.empty())
        {
            covers = DataTypeSize(dt) * count;
            if (hasSize && size != covers)
                Issue(IssueCode::BadValue, where + ".size", "size " + FormatHex(size) + " is not count * the size of " + dt + " (" + FormatHex(covers) + ")");
        }
        else if (hasSize)
        {
            covers = size;
        }
        if (covers > 0xFFFFFFFFull || !RangeFits(act.target, static_cast<uint32_t>(covers)))
            Issue(IssueCode::RangeOutOfRegion, where, "the label's " + FormatHex(covers) + " bytes do not fit inside " + RegionApiName(act.target.region) + " from " + FormatHex(act.target.offset));
    }
    else if (type == "add_watch" && act.hasTarget)
    {
        const uint32_t size = DataTypeSize(str("data_type"));
        if (size && !RangeFits(act.target, size))
            Issue(IssueCode::RangeOutOfRegion, where, "a " + str("data_type") + " watch reads past the end of " + RegionApiName(act.target.region));
    }
    else if (type == "add_memory_watchpoint" && act.hasTarget)
    {
        uint32_t size = 0;
        ParseHex32(str("size"), &size);
        if (size && !RangeFits(act.target, size))
            Issue(IssueCode::RangeOutOfRegion, where, "the watched range runs past the end of " + std::string(RegionApiName(act.target.region)));
    }
    else if (type == "add_log_tracepoint")
    {
        const std::string repeat = str("repeat").empty() ? "every_time" : str("repeat");
        const bool hasN = p.Has("repeat_n");
        if (repeat == "every_n" && !hasN)
            Issue(IssueCode::MissingField, where + ".repeat_n", "repeat every_n needs repeat_n");
        if (repeat != "every_n" && hasN)
            Issue(IssueCode::BadValue, where + ".repeat_n", "repeat_n is only meaningful with repeat every_n");
        if (str("format").find("{*") != std::string::npos)
            act.warnings.push_back("memory in the format is read at the next snapshot, not at the moment of the hit");
        if (p.Has("condition"))
            act.warnings.push_back("a condition makes the emulator forward every execution; SE evaluates it client-side");
    }
    else if (type == "request_evidence")
    {
        uint64_t total = 0, cap = 0;
        const JsonValue* ranges = p.Find("ranges");
        if (ranges && ranges->IsArray())
            for (const JsonValue& r : ranges->items())
            {
                uint32_t len = 0;
                const JsonValue* l = r.IsObject() ? r.Find("length") : nullptr;
                if (l && l->IsString() && ParseHex32(l->AsString(), &len)) total += len;
            }
        if (p.Has("max_bytes") && ParseHex64(str("max_bytes"), &cap) && total > cap)
            Issue(IssueCode::BadValue, where + ".max_bytes", "the ranges total " + FormatHex(total) + " bytes, more than max_bytes " + FormatHex(cap));
        CheckRequestSource(spec, act, where);
    }
    else if (type == "request_code_evidence")
    {
        CheckRequestSource(spec, act, where);
    }
}

// §6.4: SE derives the fingerprint and covered range from the evidence the action cites; it never
// depends on the agent supplying a hash.
void Checker::DeriveFingerprint(const JsonValue& obj, ValidatedAction& act, const std::string& where)
{
    const bool wantsCode = act.codeSpecific;
    const JsonValue* expected = obj.Find("expected_code_sha256");
    if (expected)
    {
        if (!expected->IsString() || !IsSha256Hex(expected->AsString()))
            Issue(IssueCode::BadValue, where + ".expected_code_sha256", "expected_code_sha256 is 64 lower-case hex digits");
        else if (!wantsCode)
            Issue(IssueCode::BadValue, where + ".expected_code_sha256", std::string("expected_code_sha256 only applies to code-specific actions, and ") + act.type + " here is not one");
    }
    if (!wantsCode || !act.hasTarget) return;

    // The action's own references first, then its hypothesis's. The first code range that covers the
    // target is the evidence.
    std::vector<std::string> refs = act.evidenceRefs;
    if (const ValidatedHypothesis* h = FindHypothesis(act.hypothesisId))
        refs.insert(refs.end(), h->evidenceRefs.begin(), h->evidenceRefs.end());
    const uint32_t span = act.type == "add_comment" ? 1u : 2u;
    for (const std::string& ref : refs)
    {
        ResolvedRef r;
        if (!ResolveEvidenceRef(mManifest, ref, &r)) continue;
        std::vector<const ManifestRange*> candidates;
        if (r.range) candidates.push_back(r.range);
        else for (const ManifestRange& m : r.item->ranges) candidates.push_back(&m);
        for (const ManifestRange* c : candidates)
        {
            if (c->kind != RangeKind::Code || c->region != act.target.region || c->sha256.empty()) continue;
            if (act.target.offset < c->offset || span > c->length || act.target.offset - c->offset > c->length - span) continue;
            act.hasFingerprint = true;
            act.fingerprint.sha256 = c->sha256;
            act.fingerprint.evidenceRef = r.item->id + "/" + c->id;
            act.fingerprint.region = c->region;
            act.fingerprint.offset = c->offset;
            act.fingerprint.length = c->length;
            break;
        }
        if (act.hasFingerprint) break;
    }
    if (!act.hasFingerprint)
    {
        Issue(IssueCode::CodeEvidenceMissing, where,
              std::string(act.type) + " is code-specific: cite (in evidence_refs on the action or its hypothesis) a code range that covers " +
              TargetToString(act.target));
        return;
    }
    if (expected && expected->IsString() && IsSha256Hex(expected->AsString()) && expected->AsString() != act.fingerprint.sha256)
        Issue(IssueCode::FingerprintMismatch, where + ".expected_code_sha256",
              "expected_code_sha256 does not match the code in " + act.fingerprint.evidenceRef + " (SE derived " + act.fingerprint.sha256 + ")");
}

void Checker::CheckAction(const JsonValue& a, size_t index)
{
    const std::string where = "$.actions[" + std::to_string(index) + "]";
    if (!a.IsObject())
    {
        Issue(IssueCode::WrongType, where, "an action must be an object");
        return;
    }
    ValidatedAction act;
    act.index = index;
    if (const JsonValue* id = a.Find("id"))
        if (id->IsString()) act.id = id->AsString();
    // From here every issue belongs to this action and marks it invalid.
    mInAction = true;
    mActionInvalid = false;
    mActionId = act.id;

    CheckFields(a, where, { "id", "type", "hypothesis_id", "target", "parameters", "depends_on", "evidence_refs", "expected_code_sha256", "extensions" });
    const JsonValue* id = Require(a, where, "id");
    if (id && StringField(id, where + ".id"))
    {
        if (!IsHandle(id->AsString())) Issue(IssueCode::Grammar, where + ".id", "an id is 1-64 characters of letters, digits, '_', '.' and '-'");
        else
        {
            for (const ValidatedAction& o : mResult->actions)
                if (o.id == id->AsString())
                {
                    Issue(IssueCode::DuplicateId, where + ".id", "action id \"" + id->AsString() + "\" is used twice");
                    break;
                }
        }
    }

    const JsonValue* type = Require(a, where, "type");
    const ActionSpec* spec = nullptr;
    if (type && StringField(type, where + ".type"))
    {
        act.type = type->AsString();
        spec = FindAction(act.type);
        if (!spec)
        {
            std::string list;
            for (const ActionSpec& s : ActionRegistry()) list += (list.empty() ? "" : ", ") + std::string(s.type);
            Issue(IssueCode::UnknownActionType, where + ".type", "\"" + act.type + "\" is not an action type SE accepts (allowed: " + list + ")");
        }
    }
    act.spec = spec;

    if (const JsonValue* h = Require(a, where, "hypothesis_id"))
        if (StringField(h, where + ".hypothesis_id"))
        {
            act.hypothesisId = h->AsString();
            if (!FindHypothesis(act.hypothesisId))
                Issue(IssueCode::UnknownHypothesis, where + ".hypothesis_id", "no hypothesis \"" + act.hypothesisId + "\" in this response");
        }

    if (const JsonValue* d = a.Find("depends_on"))
    {
        if (!d->IsArray()) Issue(IssueCode::WrongType, where + ".depends_on", "depends_on must be an array");
        else
        {
            if (d->items().size() > mOptions.limits.maxDependsOn)
                Issue(IssueCode::LimitExceeded, where + ".depends_on", "more than " + std::to_string(mOptions.limits.maxDependsOn) + " dependencies");
            for (const JsonValue& e : d->items())
            {
                if (!e.IsString()) { Issue(IssueCode::WrongType, where + ".depends_on", "a dependency is an action id"); continue; }
                if (std::find(act.dependsOn.begin(), act.dependsOn.end(), e.AsString()) != act.dependsOn.end())
                    Issue(IssueCode::DuplicateId, where + ".depends_on", "dependency \"" + e.AsString() + "\" is listed twice");
                act.dependsOn.push_back(e.AsString());
            }
        }
    }
    CheckRefs(a, where, &act.evidenceRefs);
    CheckExtensions(a, where);

    const JsonValue* params = Require(a, where, "parameters");
    if (params && !params->IsObject()) { Issue(IssueCode::WrongType, where + ".parameters", "parameters must be an object"); params = nullptr; }
    if (params) act.parameters = *params;

    if (spec)
    {
        const JsonValue* target = a.Find("target");
        if (spec->target.required)
        {
            if (!target) Issue(IssueCode::MissingField, where + ".target", std::string(spec->type) + " needs a target");
            else CheckTarget(*spec, *target, where + ".target", act);
        }
        else if (target)
        {
            Issue(IssueCode::UnknownField, where + ".target", std::string(spec->type) + " takes no target (its ranges are parameters)");
        }
        if (params) CheckParameters(*spec, *params, where + ".parameters");

        // Capabilities, now that the parameters that can condition them are known.
        if (act.hasTarget)
        {
            for (const CapRule& rule : spec->target.caps)
            {
                if (rule.param)
                {
                    const JsonValue* v = act.parameters.Find(rule.param);
                    bool applies = false;
                    if (v && v->IsString())
                        for (const char* want : rule.values) applies = applies || v->AsString() == want;
                    if (!applies) continue;
                }
                if (!HasCaps(act.target, rule.caps))
                {
                    std::string need;
                    if (rule.caps & kCapWatch) need += "readable memory (kCapWatch) ";
                    if (rule.caps & kCapBreakWrite) need += "write-watchable memory (kCapBreakWrite) ";
                    if (rule.caps & kCapSh2Code) need += "a region that can hold SH-2 code (kCapSh2Code) ";
                    Issue(IssueCode::MissingCapability, where + ".target",
                          std::string(spec->type) + " needs " + need + "but " + TargetToString(act.target) + " is not");
                }
            }
        }
        if (params) CheckCrossRules(*spec, act, where + ".parameters");
        act.codeSpecific = IsCodeSpecific(*spec, act.hasTarget ? &act.target : nullptr);
        DeriveFingerprint(a, act, where);
        if (act.hasTarget) act.semanticKey = SemanticKey(*spec, &act.target, act.parameters);
    }

    const std::string canonical = ToCanonicalJson(a);
    act.payloadSha256 = Sha256Hex(canonical.data(), canonical.size());

    mInAction = false;
    act.valid = !mActionInvalid;
    mResult->actions.push_back(std::move(act));
}

void Checker::CheckDependencies()
{
    const size_t n = mResult->actions.size();
    std::set<std::string> index;
    for (size_t i = 0; i < n; ++i) index.insert(mResult->actions[i].id);
    auto at = [&](size_t i) { return "$.actions[" + std::to_string(mResult->actions[i].index) + "].depends_on"; };
    for (size_t i = 0; i < n; ++i)
        for (const std::string& d : mResult->actions[i].dependsOn)
        {
            if (d == mResult->actions[i].id) IssueOn(i, IssueCode::DependencyCycle, at(i), "action \"" + d + "\" depends on itself");
            else if (!index.count(d)) IssueOn(i, IssueCode::UnknownDependency, at(i), "depends on unknown action \"" + d + "\"");
        }

    // Kahn's algorithm, picking the earliest-written ready action each round so the order is stable.
    std::vector<int> indegree(n, 0);
    for (size_t i = 0; i < n; ++i)
        for (const std::string& d : mResult->actions[i].dependsOn)
            if (index.count(d) && d != mResult->actions[i].id) ++indegree[i];
    std::vector<bool> done(n, false);
    for (size_t round = 0; round < n; ++round)
    {
        size_t pick = n;
        for (size_t i = 0; i < n; ++i)
            if (!done[i] && indegree[i] == 0) { pick = i; break; }
        if (pick == n) break;
        done[pick] = true;
        mResult->applyOrder.push_back(mResult->actions[pick].id);
        for (size_t i = 0; i < n; ++i)
            for (const std::string& d : mResult->actions[i].dependsOn)
                if (d == mResult->actions[pick].id && !done[i]) --indegree[i];
    }
    if (mResult->applyOrder.size() == n) return;

    std::string list;
    for (size_t i = 0; i < n; ++i)
        if (!done[i]) list += (list.empty() ? "" : ", ") + mResult->actions[i].id;
    for (size_t i = 0; i < n; ++i)
        if (!done[i]) IssueOn(i, IssueCode::DependencyCycle, at(i), "actions depend on each other in a cycle (" + list + ")");
    mResult->applyOrder.clear();
}

void Checker::Run(const JsonValue& doc)
{
    if (!Object(doc, "$", "the response")) return;
    CheckFields(doc, "$", { "schema_version", "investigation_id", "evidence_id", "response_id", "hypotheses", "actions", "extensions" });

    if (const JsonValue* v = Require(doc, "$", "schema_version"))
    {
        uint64_t n = 0;
        if (!v->IsInteger() || !v->ToUint64(&n)) Issue(IssueCode::BadSchemaVersion, "$.schema_version", "schema_version must be the integer 1");
        else if (n != 1) Issue(IssueCode::BadSchemaVersion, "$.schema_version", "schema_version " + std::to_string(n) + " is not supported; this SE reads schema 1");
    }
    struct IdField { const char* key; std::string* dst; };
    for (const IdField& f : { IdField{ "investigation_id", &mResult->investigationId }, IdField{ "evidence_id", &mResult->evidenceId },
                              IdField{ "response_id", &mResult->responseId } })
    {
        const JsonValue* v = Require(doc, "$", f.key);
        if (!StringField(v, std::string("$.") + f.key)) continue;
        *f.dst = v->AsString();
        if (!IsUuid(*f.dst)) Issue(IssueCode::BadValue, std::string("$.") + f.key, std::string(f.key) + " must be a lower-case UUID");
    }

    // The package is the one the response names, or the user has said to use this one regardless.
    if (IsUuid(mResult->investigationId) && IsUuid(mResult->evidenceId))
    {
        const bool sameInvestigation = mResult->investigationId == mManifest.investigationId;
        const bool sameEvidence = mResult->evidenceId == mManifest.evidenceId;
        if (!sameInvestigation || !sameEvidence)
        {
            if (!mOptions.rebound)
            {
                mResult->needsRebind = true;
                if (!sameEvidence)
                    Issue(IssueCode::EvidenceIdMismatch, "$.evidence_id",
                          "evidence_id " + mResult->evidenceId + " is not the package " + mManifest.evidenceId +
                          "; the import stops here. If this response is for this package, rebind it explicitly");
                if (!sameInvestigation)
                    Issue(IssueCode::InvestigationIdMismatch, "$.investigation_id",
                          "investigation_id " + mResult->investigationId + " is not " + mManifest.investigationId);
                return;
            }
            mResult->rebound = true;
        }
    }
    mResult->boundEvidenceId = mManifest.evidenceId;

    if (const JsonValue* h = Require(doc, "$", "hypotheses")) CheckHypotheses(*h);
    CheckExtensions(doc, "$");
    if (const JsonValue* e = doc.Find("extensions"))
        if (e->IsObject()) mResult->extensions = *e;

    const JsonValue* actions = Require(doc, "$", "actions");
    if (actions)
    {
        if (!actions->IsArray()) Issue(IssueCode::WrongType, "$.actions", "actions must be an array");
        else
        {
            if (actions->items().size() > mOptions.limits.maxActions)
                Issue(IssueCode::LimitExceeded, "$.actions", "more than " + std::to_string(mOptions.limits.maxActions) + " actions");
            for (size_t i = 0; i < actions->items().size(); ++i) CheckAction(actions->items()[i], i);
        }
    }

    // Whole-response rules, which need every action parsed.
    CheckDependencies();
    size_t tracepoints = 0;
    std::map<std::string, std::string> keys, labels;
    for (size_t i = 0; i < mResult->actions.size(); ++i)
    {
        ValidatedAction& a = mResult->actions[i];
        if (!a.spec) continue;
        const std::string where = "$.actions[" + std::to_string(a.index) + "]";
        if (a.type == "add_log_tracepoint" && ++tracepoints == kMaxLogTracepoints + 1)
            IssueOn(i, IssueCode::TooManyTracepoints, where, "more than " + std::to_string(kMaxLogTracepoints) + " tracepoints in one response (the emulator installs at most that many)");
        if (!a.semanticKey.empty())
        {
            const auto it = keys.find(a.semanticKey);
            if (it == keys.end()) keys[a.semanticKey] = a.id;
            else IssueOn(i, IssueCode::DuplicateSemanticKey, where, "action \"" + a.id + "\" and \"" + it->second + "\" are the same item (" + a.semanticKey + ")");
        }
        if (a.type == "add_address_label")
        {
            const JsonValue* n = a.parameters.Find("name");
            if (n && n->IsString())
            {
                const auto it = labels.find(n->AsString());
                if (it == labels.end()) labels[n->AsString()] = a.id;
                else IssueOn(i, IssueCode::DuplicateLabelName, where + ".parameters.name", "label name \"" + n->AsString() + "\" is also used by action \"" + it->second + "\"; a label name is unique");
            }
        }
    }
}

}  // namespace

ValidationResult ValidateResponseJson(const JsonValue& document, const Manifest& manifest, const ValidateOptions& options)
{
    ValidationResult result;
    Checker(manifest, options, &result).Run(document);
    result.ok = result.issues.empty();
    return result;
}

ValidationResult ValidateResponse(const std::string& text, const Manifest& manifest, const ValidateOptions& options)
{
    ValidationResult result;
    auto notJson = [&](const std::string& why) {
        ValidationIssue i;
        i.code = IssueCode::NotJson;
        i.where = "$";
        i.message = why;
        result.issues.push_back(std::move(i));
        return result;
    };
    const Extraction ex = ExtractResponseJson(text);
    if (!ex.ok) return notJson(ex.error);
    JsonLimits limits;
    limits.maxBytes = options.limits.maxBytes;
    limits.maxDepth = options.limits.maxDepth;
    limits.maxArrayLength = options.limits.maxArrayLength;
    limits.maxObjectMembers = options.limits.maxObjectMembers;
    limits.maxStringBytes = options.limits.maxStringBytes;
    JsonValue doc;
    JsonError err;
    if (!ParseJson(ex.json, limits, &doc, &err))
    {
        // The hint is for the common mistake -- prose around an unfenced document -- which the tokenizer
        // reports as a syntax error; a limit or a duplicate key has its own, complete message.
        const bool syntax = err.message.compare(0, 15, "[json.exception") == 0;
        return notJson(err.message + (ex.fenced || !syntax ? std::string()
                                                           : " (if the reply contains prose, put the JSON in a single ```json fenced block)"));
    }
    result = ValidateResponseJson(doc, manifest, options);
    result.fenced = ex.fenced;
    return result;
}

// ---- Identity -------------------------------------------------------------------------------------

ActionIdentity IdentityOf(const ValidationResult& r, const ValidatedAction& a)
{
    ActionIdentity i;
    i.investigationId = r.investigationId;
    i.responseId = r.responseId;
    i.actionId = a.id;
    i.payloadSha256 = a.payloadSha256;
    return i;
}

IdentityRelation CompareIdentity(const ActionIdentity& a, const ActionIdentity& b)
{
    if (a.investigationId != b.investigationId || a.responseId != b.responseId || a.actionId != b.actionId)
        return IdentityRelation::Different;
    return a.payloadSha256 == b.payloadSha256 ? IdentityRelation::SamePayload : IdentityRelation::ConflictingPayload;
}

// ---- Preview --------------------------------------------------------------------------------------

namespace
{

std::string Describe(const ValidatedAction& a)
{
    const std::string where = a.hasTarget ? TargetToString(a.target) : std::string();
    auto str = [&](const char* k) { return StringParam(a.parameters, k); };
    const std::string t = a.type;
    if (t == "add_address_label") return "Label " + where + " as " + str("name") + (str("data_type").empty() ? "" : " (" + str("data_type") + ")");
    if (t == "rename_function") return "Name the function at " + where + " " + str("name");
    if (t == "add_comment") return "Comment at " + where + ": " + str("text");
    if (t == "add_watch") return "Watch " + str("name") + " (" + str("data_type") + ", big-endian) at " + where;
    if (t == "add_execution_breakpoint") return "Break when either SH-2 executes " + where + (str("condition").empty() ? "" : " if " + str("condition"));
    if (t == "add_memory_watchpoint") return "Break on " + str("kind") + " of " + str("size") + " bytes at " + where;
    if (t == "add_log_tracepoint")
    {
        uint64_t cpu = 0;
        if (const JsonValue* c = a.parameters.Find("cpu")) c->ToUint64(&cpu);
        return std::string("Log \"") + str("format") + "\" when the " + (cpu ? "slave" : "master") + " SH-2 executes " + where;
    }
    if (t == "request_evidence") return "Ask for more evidence from source " + str("source") + ": " + str("reason");
    if (t == "request_code_evidence") return "Ask for the code at " + where + " from source " + str("source") + ": " + str("reason");
    return t;
}

}  // namespace

ResponsePreview BuildPreview(const ValidationResult& r)
{
    ResponsePreview p;
    p.ok = r.ok;
    p.rebound = r.rebound;
    p.investigationId = r.investigationId;
    p.evidenceId = r.evidenceId;
    p.responseId = r.responseId;
    p.hypotheses = r.hypotheses;
    for (const ValidationIssue& i : r.issues)
        if (i.actionId.empty()) p.topLevelProblems.push_back(i.where + ": " + i.message);

    std::vector<std::string> order = r.applyOrder;
    for (const ValidatedAction& a : r.actions)
        if (std::find(order.begin(), order.end(), a.id) == order.end()) order.push_back(a.id);

    for (const std::string& id : order)
    {
        const ValidatedAction* a = nullptr;
        for (const ValidatedAction& c : r.actions) if (c.id == id) a = &c;
        if (!a) continue;
        PreviewRow row;
        row.actionId = a->id;
        row.type = a->type;
        row.hypothesisId = a->hypothesisId;
        row.valid = a->valid;
        row.summary = Describe(*a);
        row.target = a->hasTarget ? TargetToString(a->target) : std::string();
        row.semanticKey = a->semanticKey;
        if (a->spec)
        {
            row.adapter = a->spec->adapter.call;
            row.adapterExists = a->spec->adapter.status == AdapterStatus::Existing;
            row.adapterMilestone = a->spec->adapter.milestone;
            row.implementedIn = a->spec->implementedIn;
        }
        row.codeSpecific = a->codeSpecific;
        if (a->hasFingerprint)
        {
            row.fingerprint = a->fingerprint.sha256;
            row.fingerprintCovers = std::string(RegionApiName(a->fingerprint.region)) + "+" + FormatHex(a->fingerprint.offset) +
                                    " length " + FormatHex(a->fingerprint.length) + " from " + a->fingerprint.evidenceRef;
        }
        row.payloadSha256 = a->payloadSha256;
        row.dependsOn = a->dependsOn;
        row.warnings = a->warnings;
        for (const ValidationIssue& i : r.issues)
            if (i.actionId == a->id) row.problems.push_back(i.where + ": " + i.message);
        p.rows.push_back(std::move(row));
    }
    return p;
}

}  // namespace ai
}  // namespace sfe
