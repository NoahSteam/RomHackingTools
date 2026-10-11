#include "AIInvestigation/ActionRegistry.h"

#include <algorithm>

#include "AIInvestigation/Encoding.h"

namespace sfe
{
namespace ai
{
namespace
{

const char* const kDataTypes[] = { "u8", "u16", "u32", "s8", "s16", "s32" };

ParamSpec Make(const char* name, ParamType type, bool required, const char* summary)
{
    ParamSpec p;
    p.name = name;
    p.type = type;
    p.required = required;
    p.summary = summary;
    return p;
}
ParamSpec Choice(const char* name, bool required, const char* summary, std::vector<const char*> values, ParamType type = ParamType::Enum)
{
    ParamSpec p = Make(name, type, required, summary);
    p.values = std::move(values);
    return p;
}
ParamSpec Num(const char* name, bool required, const char* summary, uint64_t lo, uint64_t hi)
{
    ParamSpec p = Make(name, ParamType::Integer, required, summary);
    p.min = lo;
    p.max = hi;
    return p;
}
ParamSpec Hex(const char* name, bool required, const char* summary, uint64_t lo, uint64_t hi)
{
    ParamSpec p = Make(name, ParamType::Hex, required, summary);
    p.min = lo;
    p.max = hi;
    return p;
}
ParamSpec TextParam(const char* name, bool required, const char* summary, size_t maxChars, ParamType type = ParamType::Text)
{
    ParamSpec p = Make(name, type, required, summary);
    p.maxChars = maxChars;
    return p;
}

std::vector<ActionSpec> Build()
{
    std::vector<ActionSpec> t;

    {
        ActionSpec a{};
        a.type = "add_address_label";
        a.summary = "Name a data address, optionally with its size and type.";
        a.target.needsBus = true;
        a.target.caps = { { kCapWatch, nullptr, {} } };
        a.target.summary = "a data location in a bus-addressable region";
        a.params = {
            Make("name", ParamType::Name, true, "the label"),
            Hex("size", false, "bytes covered by the label", 1, 0x100000),
            Choice("data_type", false, "element type", std::vector<const char*>(std::begin(kDataTypes), std::end(kDataTypes))),
            Num("count", false, "element count; more than 1 makes it an array (needs data_type)", 1, 4096),
            TextParam("note", false, "one-line note", kMaxCommentChars),
        };
        a.semanticKey = "canonical address";
        a.adapter = { "AddressSymbols::Add", AdapterStatus::Planned, 2,
                      "new Debug/AddressSymbols model, ImGui-free, with persisted UUIDs and a per-scope file; refuses an address that already has a function name" };
        a.codeSpecific = false;
        a.codeSpecificOnCodeRegion = false;
        a.implementedIn = 3;
        a.notes = "size, when given with data_type, must equal count * the type's size; count needs data_type; the range must fit the region";
        t.push_back(a);
    }
    {
        ActionSpec a{};
        a.type = "rename_function";
        a.summary = "Name a function entry point.";
        a.target.needsBus = true;
        a.target.evenAddress = true;
        a.target.caps = { { kCapSh2Code, nullptr, {} } };
        a.target.summary = "the even address of the function entry, in a region that can hold SH-2 code";
        a.params = { Make("name", ParamType::Name, true, "the function name") };
        a.semanticKey = "canonical entry address";
        a.adapter = { "FunctionNames::Rename", AdapterStatus::Existing, 0,
                      "keyed by the raw address today (std::map<uint32_t, std::string>): M2 re-keys by canonical entry, replaces it with UUID records and moves it to a per-scope file" };
        a.codeSpecific = true;
        a.codeSpecificOnCodeRegion = false;
        a.implementedIn = 3;
        a.notes = "must cite evidence covering the entry (code-specific, PLAN.md §6.4)";
        t.push_back(a);
    }
    {
        ActionSpec a{};
        a.type = "add_comment";
        a.summary = "Attach a one-line comment to an address.";
        a.target.needsBus = true;
        a.target.summary = "any bus-addressable location";
        a.params = { TextParam("text", true, "one line, no control characters", kMaxCommentChars) };
        a.semanticKey = "canonical address";
        a.adapter = { "comment store", AdapterStatus::Planned, 2,
                      "no model class exists: the map is private to AssemblyPanel (mComments) and edited inline, one line per note in assembly_comments.txt; M2 extracts a record store keyed by canonical address with UUIDs" };
        a.codeSpecific = false;
        a.codeSpecificOnCodeRegion = true;
        a.implementedIn = 3;
        a.notes = "code-specific (must cite covering evidence) when the target is in a region with kCapSh2Code";
        t.push_back(a);
    }
    {
        ActionSpec a{};
        a.type = "add_watch";
        a.summary = "Add a Watch window entry for an address (big-endian).";
        a.target.needsBus = true;
        a.target.caps = { { kCapWatch, nullptr, {} } };
        a.target.summary = "a bus-addressable location";
        a.params = {
            Make("name", ParamType::Name, true, "the watch name"),
            Choice("data_type", true, "how to read the value",
                   { "u8", "s8", "u16", "s16", "u32", "s32", "rgb555", "pointer" }),
            Make("enabled", ParamType::Bool, false, "defaults to true"),
        };
        a.rejected = { { "byte_order", "watches are always big-endian; there is no byte-order field" } };
        a.semanticKey = "(canonical address, data_type)";
        a.adapter = { "WatchList::Add", AdapterStatus::Existing, 0,
                      "WatchEntry has a session-local id but no persisted UUID, and the list is a working-directory session file (M2); the expression is 0xADDR [+/- N] until labels exist (M2)" };
        a.codeSpecific = false;
        a.codeSpecificOnCodeRegion = false;
        a.implementedIn = 3;
        a.notes = "";
        t.push_back(a);
    }
    {
        ActionSpec a{};
        a.type = "add_execution_breakpoint";
        a.summary = "Break when an SH-2 executes the instruction at an address (either CPU).";
        a.target.needsBus = true;
        a.target.evenAddress = true;
        a.target.caps = { { kCapSh2Code, nullptr, {} } };
        a.target.summary = "the even address of an instruction, in a region that can hold SH-2 code";
        a.params = {
            TextParam("condition", false, "optional guard, evaluated when the emulator halts here", kMaxConditionChars, ParamType::Condition),
            Make("enabled", ParamType::Bool, false, "defaults to true"),
        };
        a.rejected = { { "cpu", "an execution breakpoint halts whichever SH-2 reaches the address; there is no cpu field" } };
        a.semanticKey = "canonical address";
        a.adapter = { "BreakpointManager::EnableExecution (then SetCondition)", AdapterStatus::Existing, 0,
                      "never ToggleExecution, which removes an existing breakpoint; Breakpoint has process-local ids and no UUID or persistence (M2)" };
        a.codeSpecific = true;
        a.codeSpecificOnCodeRegion = false;
        a.implementedIn = 3;
        a.notes = "must cite evidence covering the address (code-specific); the condition must parse as a ConditionEval expression";
        t.push_back(a);
    }
    {
        ActionSpec a{};
        a.type = "add_memory_watchpoint";
        a.summary = "Break when an SH-2 (or SCU DMA) reads or writes a memory range.";
        a.target.needsBus = true;
        a.target.caps = { { kCapWatch, nullptr, {} }, { kCapBreakWrite, "kind", { "write", "read_write" } } };
        a.target.summary = "a bus-addressable location; for the write kinds, a region the emulator can watch for writes";
        a.params = {
            Choice("kind", true, "which accesses halt", { "read", "write", "read_write" }),
            Choice("size", true, "bytes watched", { "0x1", "0x2", "0x4" }, ParamType::HexChoice),
            Make("enabled", ParamType::Bool, false, "defaults to true"),
        };
        a.rejected = {
            { "cpu", "the emulator ignores cpu for data watchpoints" },
            { "condition", "memory watchpoints take no condition: the halt PC is the accessing instruction, not the watched address" },
        };
        a.semanticKey = "(canonical address, kind, size)";
        a.adapter = { "BreakpointManager::AddMemory", AdapterStatus::Existing, 0,
                      "dedups by (address, kind, size) and returns an existing, possibly user-owned id, so M3 must check ownership first; no UUID or persistence (M2)" };
        a.codeSpecific = false;
        a.codeSpecificOnCodeRegion = false;
        a.implementedIn = 3;
        a.notes = "";
        t.push_back(a);
    }
    {
        ActionSpec a{};
        a.type = "add_log_tracepoint";
        a.summary = "Log registers or memory each time an SH-2 executes an address, without halting.";
        a.target.needsBus = true;
        a.target.evenAddress = true;
        a.target.caps = { { kCapSh2Code, nullptr, {} } };
        a.target.summary = "the even address of an instruction, in a region that can hold SH-2 code";
        a.params = {
            Num("cpu", true, "0 master, 1 slave", 0, 1),
            TextParam("format", true, "output template; {value[:spec]} substitutions", kMaxFormatChars, ParamType::Format),
            TextParam("condition", false, "optional guard; the tracepoint then forwards every execution", kMaxConditionChars, ParamType::Condition),
            Choice("repeat", false, "defaults to every_time", { "every_time", "once", "every_n" }),
            Num("repeat_n", false, "act on every Nth execution (with repeat every_n)", 2, 0xFFFFFF),
            Make("enabled", ParamType::Bool, false, "defaults to true"),
        };
        a.rejected = {
            { "effects", "tracepoints here only write to the log; no other effect (pause, screenshot, script, ...) can be requested" },
            { "pause", "tracepoints here only write to the log" },
            { "script", "scripts are never run" },
        };
        a.semanticKey = "(cpu, canonical address)";
        a.adapter = { "ExecutionActions::Add (after a LogAt key check)", AdapterStatus::Existing, 0,
                      "Add does not dedup, so M3 checks (cpu, address) first; at most 256 are installable; no UUID or persistence (M2); memory in the format reads the next snapshot, not the value at the hit" };
        a.codeSpecific = true;
        a.codeSpecificOnCodeRegion = false;
        a.implementedIn = 3;
        a.notes = "repeat_n is required with, and only with, repeat every_n; format and condition must parse; at most 256 per response; "
                  "must cite evidence covering the address (code-specific)";
        t.push_back(a);
    }
    {
        ActionSpec a{};
        a.type = "request_evidence";
        a.summary = "Ask for more bytes of a source from an earlier package (read-only).";
        a.target.required = false;
        a.target.summary = "none: the ranges are parameters";
        ParamSpec ranges = Make("ranges", ParamType::RangeList, true, "[{region, offset, length}], at most 64");
        a.params = {
            Make("evidence_id", ParamType::Uuid, false, "the package holding the source; defaults to this response's evidence_id"),
            Make("source", ParamType::Handle, true, "the source's id in that package's manifest"),
            ranges,
            Choice("representation", true, "how the bytes should come back", { "binary", "hex_text" }),
            Hex("max_bytes", true, "upper bound on the total returned", 1, 0x1000000),
            TextParam("reason", true, "why this evidence is needed", kMaxCommentChars),
        };
        a.semanticKey = "none (read-only)";
        a.adapter = { "Prompt Builder follow-up", AdapterStatus::Planned, 3,
                      "opens a pre-filled Prompt Builder (M1); fulfilment from the parent package's retained source/ is M3, and never from live RAM" };
        a.codeSpecific = false;
        a.codeSpecificOnCodeRegion = false;
        a.implementedIn = 3;
        a.notes = "each range must fit its region; the source must exist in the package named by evidence_id (checked when that package is the one validated against)";
        t.push_back(a);
    }
    {
        ActionSpec a{};
        a.type = "request_code_evidence";
        a.summary = "Ask for the code around a function entry from an earlier package (read-only).";
        a.target.needsBus = true;
        a.target.evenAddress = true;
        a.target.caps = { { kCapSh2Code, nullptr, {} } };
        a.target.summary = "the even address of the function entry, in a region that can hold SH-2 code";
        a.params = {
            Make("evidence_id", ParamType::Uuid, false, "the package holding the source; defaults to this response's evidence_id"),
            Make("source", ParamType::Handle, true, "the source's id in that package's manifest"),
            Choice("representation", true, "how the code should come back", { "disassembly", "binary" }),
            Hex("max_bytes", true, "upper bound on the total returned", 1, 0x100000),
            TextParam("reason", true, "why this evidence is needed", kMaxCommentChars),
        };
        a.semanticKey = "none (read-only)";
        a.adapter = { "Prompt Builder follow-up", AdapterStatus::Planned, 3,
                      "opens a pre-filled Prompt Builder (M1) using the bounded SH-2 code adapter (M3); never reads live RAM" };
        a.codeSpecific = false;
        a.codeSpecificOnCodeRegion = false;
        a.implementedIn = 3;
        a.notes = "a read-only request, so it is not code-specific in the sense of PLAN.md §6.4";
        t.push_back(a);
    }
    return t;
}

JsonValue Str(const std::string& s) { return JsonValue::String(s); }
JsonValue Num64(uint64_t v) { return JsonValue::UInt(v); }

JsonValue StringArray(const std::vector<const char*>& values)
{
    JsonValue a = JsonValue::Array();
    for (const char* v : values) a.Append(Str(v));
    return a;
}

std::string Join(const std::vector<const char*>& values, const char* sep)
{
    std::string out;
    for (size_t i = 0; i < values.size(); ++i)
    {
        if (i) out += sep;
        out += values[i];
    }
    return out;
}

const char kHexPattern[] = "^0x[0-9A-F]{1,8}$";
const char kNamePattern[] = "^[A-Za-z_][A-Za-z0-9_]{0,63}$";
const char kHandlePattern[] = "^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$";
const char kUuidPattern[] = "^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$";
const char kExtensionKeyPattern[] = "^[a-z0-9][a-z0-9_-]*(\\.[a-z0-9][a-z0-9_-]*)+$";

JsonValue ParamSchema(const ParamSpec& p)
{
    JsonValue s = JsonValue::Object();
    switch (p.type)
    {
    case ParamType::Name:
        s.Set("type", Str("string"));
        s.Set("pattern", Str(kNamePattern));
        break;
    case ParamType::Text:
    case ParamType::Condition:
    case ParamType::Format:
        s.Set("type", Str("string"));
        s.Set("minLength", Num64(p.required && p.type == ParamType::Text ? 1 : 0));
        s.Set("maxLength", Num64(p.maxChars));
        break;
    case ParamType::Hex:
        s.Set("type", Str("string"));
        s.Set("pattern", Str(kHexPattern));
        break;
    case ParamType::HexChoice:
    case ParamType::Enum:
        s.Set("type", Str("string"));
        s.Set("enum", StringArray(p.values));
        break;
    case ParamType::Integer:
        s.Set("type", Str("integer"));
        s.Set("minimum", Num64(p.min));
        s.Set("maximum", Num64(p.max));
        break;
    case ParamType::Bool:
        s.Set("type", Str("boolean"));
        break;
    case ParamType::Uuid:
        s.Set("type", Str("string"));
        s.Set("pattern", Str(kUuidPattern));
        break;
    case ParamType::Handle:
        s.Set("type", Str("string"));
        s.Set("pattern", Str(kHandlePattern));
        break;
    case ParamType::RangeList:
    {
        s.Set("type", Str("array"));
        s.Set("minItems", Num64(1));
        s.Set("maxItems", Num64(kMaxRequestRanges));
        JsonValue item = JsonValue::Object();
        item.Set("type", Str("object"));
        item.Set("additionalProperties", JsonValue::Bool(false));
        JsonValue req = JsonValue::Array();
        req.Append(Str("region"));
        req.Append(Str("offset"));
        req.Append(Str("length"));
        item.Set("required", std::move(req));
        JsonValue props = JsonValue::Object();
        JsonValue regionNames = JsonValue::Array();
        for (size_t i = 0; i < kRegionCount; ++i) regionNames.Append(Str(RegionApiName(static_cast<RegionId>(i))));
        JsonValue region = JsonValue::Object();
        region.Set("enum", std::move(regionNames));
        props.Set("region", std::move(region));
        JsonValue hex = JsonValue::Object();
        hex.Set("type", Str("string"));
        hex.Set("pattern", Str(kHexPattern));
        props.Set("offset", hex);
        props.Set("length", hex);
        item.Set("properties", std::move(props));
        s.Set("items", std::move(item));
        break;
    }
    }
    s.Set("description", Str(p.summary));
    return s;
}

std::string ParamTypeText(const ParamSpec& p)
{
    switch (p.type)
    {
    case ParamType::Name: return "identifier [A-Za-z_][A-Za-z0-9_]{0,63}";
    case ParamType::Text: return "one line of text, at most " + std::to_string(p.maxChars) + " characters";
    case ParamType::Condition: return "condition expression, at most " + std::to_string(p.maxChars) + " characters";
    case ParamType::Format: return "format template, at most " + std::to_string(p.maxChars) + " characters";
    case ParamType::Hex:
        return "hex string \"0x...\" (upper-case digits)" +
               (p.max ? ", " + FormatHex(p.min) + " to " + FormatHex(p.max) : std::string());
    case ParamType::HexChoice: return "one of " + Join(p.values, ", ") + " (hex strings)";
    case ParamType::Enum: return "one of " + Join(p.values, ", ");
    case ParamType::Integer: return "number " + std::to_string(p.min) + ".." + std::to_string(p.max);
    case ParamType::Bool: return "true or false";
    case ParamType::Uuid: return "UUID string";
    case ParamType::Handle: return "id string";
    case ParamType::RangeList: return "array of {region, offset, length} (1 to " + std::to_string(kMaxRequestRanges) + ")";
    }
    return "?";
}

}  // namespace

const std::vector<ActionSpec>& ActionRegistry()
{
    static const std::vector<ActionSpec> table = Build();
    return table;
}

const ActionSpec* FindAction(const std::string& type)
{
    for (const ActionSpec& a : ActionRegistry())
        if (type == a.type) return &a;
    return nullptr;
}

const ParamSpec* FindParam(const ActionSpec& spec, const std::string& name)
{
    for (const ParamSpec& p : spec.params)
        if (name == p.name) return &p;
    return nullptr;
}

bool IsCodeSpecific(const ActionSpec& spec, const CanonicalTarget* target)
{
    if (spec.codeSpecific) return true;
    return spec.codeSpecificOnCodeRegion && target && HasCaps(*target, kCapSh2Code);
}

std::string SemanticKey(const ActionSpec& spec, const CanonicalTarget* target, const JsonValue& parameters)
{
    if (!target) return std::string();
    auto param = [&](const char* name) -> std::string {
        const JsonValue* v = parameters.Find(name);
        if (!v) return std::string();
        if (v->IsString()) return v->AsString();
        uint64_t u = 0;
        return v->ToUint64(&u) ? std::to_string(u) : std::string();
    };
    const std::string where = std::string(RegionApiName(target->region)) + "+" + FormatHex(target->offset);
    const std::string type = spec.type;
    if (type == "add_watch") return type + "|" + where + "|" + param("data_type");
    if (type == "add_memory_watchpoint") return type + "|" + where + "|" + param("kind") + "|" + param("size");
    if (type == "add_log_tracepoint") return type + "|cpu" + param("cpu") + "|" + where;
    if (type == "request_evidence" || type == "request_code_evidence") return std::string();
    return type + "|" + where;
}

JsonValue BuildResponseSchema()
{
    JsonValue root = JsonValue::Object();
    root.Set("$schema", Str("https://json-schema.org/draft/2020-12/schema"));
    root.Set("title", Str("SaturnExplorer AI investigation response, schema v1"));
    root.Set("description", Str("Return exactly one JSON document matching this schema, either bare or in a single fenced code block labelled json. "
                                "Unknown fields are rejected except inside \"extensions\". Addresses, offsets and lengths are \"0x\"-prefixed "
                                "upper-case hex strings; IDs are lower-case UUIDs."));
    root.Set("type", Str("object"));
    root.Set("additionalProperties", JsonValue::Bool(false));
    JsonValue required = JsonValue::Array();
    for (const char* r : { "schema_version", "investigation_id", "evidence_id", "response_id", "hypotheses", "actions" }) required.Append(Str(r));
    root.Set("required", std::move(required));

    JsonValue defs = JsonValue::Object();
    {
        JsonValue uuid = JsonValue::Object();
        uuid.Set("type", Str("string"));
        uuid.Set("pattern", Str(kUuidPattern));
        defs.Set("uuid", std::move(uuid));
        JsonValue handle = JsonValue::Object();
        handle.Set("type", Str("string"));
        handle.Set("pattern", Str(kHandlePattern));
        defs.Set("handle", std::move(handle));
        JsonValue hex = JsonValue::Object();
        hex.Set("type", Str("string"));
        hex.Set("pattern", Str(kHexPattern));
        defs.Set("hex", std::move(hex));
        JsonValue sha = JsonValue::Object();
        sha.Set("type", Str("string"));
        sha.Set("pattern", Str("^[0-9a-f]{64}$"));
        defs.Set("sha256", std::move(sha));

        JsonValue refList = JsonValue::Object();
        refList.Set("type", Str("array"));
        refList.Set("maxItems", Num64(32));
        JsonValue refItem = JsonValue::Object();
        refItem.Set("type", Str("string"));
        refItem.Set("description", Str("an evidence item id from manifest.json, or \"<item id>/<range id>\""));
        refList.Set("items", std::move(refItem));
        defs.Set("evidence_refs", std::move(refList));

        JsonValue ext = JsonValue::Object();
        ext.Set("type", Str("object"));
        ext.Set("description", Str("Namespaced extension data, kept as-is and never acted on. Keys look like \"com.example.tool\"."));
        JsonValue pp = JsonValue::Object();
        pp.Set(kExtensionKeyPattern, JsonValue::Object());
        ext.Set("patternProperties", std::move(pp));
        ext.Set("additionalProperties", JsonValue::Bool(false));
        defs.Set("extensions", std::move(ext));

        JsonValue target = JsonValue::Object();
        target.Set("type", Str("object"));
        target.Set("additionalProperties", JsonValue::Bool(false));
        JsonValue tp = JsonValue::Object();
        JsonValue busRef = JsonValue::Object();
        busRef.Set("$ref", Str("#/$defs/hex"));
        tp.Set("bus_address", busRef);
        JsonValue regionNames = JsonValue::Array();
        for (size_t i = 0; i < kRegionCount; ++i) regionNames.Append(Str(RegionApiName(static_cast<RegionId>(i))));
        JsonValue region = JsonValue::Object();
        region.Set("enum", std::move(regionNames));
        tp.Set("region", std::move(region));
        tp.Set("offset", busRef);
        target.Set("properties", std::move(tp));
        JsonValue anyOf = JsonValue::Array();
        {
            JsonValue a = JsonValue::Object();
            JsonValue r = JsonValue::Array();
            r.Append(Str("bus_address"));
            a.Set("required", std::move(r));
            anyOf.Append(std::move(a));
            JsonValue b = JsonValue::Object();
            JsonValue r2 = JsonValue::Array();
            r2.Append(Str("region"));
            r2.Append(Str("offset"));
            b.Set("required", std::move(r2));
            anyOf.Append(std::move(b));
        }
        target.Set("anyOf", std::move(anyOf));
        target.Set("description", Str("A location: bus_address, or region and offset, or both (they must agree). The VDP1 frame buffer "
                                      "(vdp1fb) has no bus address and is addressed by region and offset only."));
        defs.Set("target", std::move(target));
    }
    root.Set("$defs", std::move(defs));

    JsonValue props = JsonValue::Object();
    {
        JsonValue v = JsonValue::Object();
        v.Set("const", Num64(1));
        props.Set("schema_version", std::move(v));
        for (const char* id : { "investigation_id", "evidence_id", "response_id" })
        {
            JsonValue r = JsonValue::Object();
            r.Set("$ref", Str("#/$defs/uuid"));
            props.Set(id, std::move(r));
        }
        JsonValue ext = JsonValue::Object();
        ext.Set("$ref", Str("#/$defs/extensions"));
        props.Set("extensions", std::move(ext));

        JsonValue hyps = JsonValue::Object();
        hyps.Set("type", Str("array"));
        hyps.Set("maxItems", Num64(64));
        JsonValue h = JsonValue::Object();
        h.Set("type", Str("object"));
        h.Set("additionalProperties", JsonValue::Bool(false));
        JsonValue hreq = JsonValue::Array();
        for (const char* r : { "id", "claim", "confidence", "evidence_refs", "prediction" }) hreq.Append(Str(r));
        h.Set("required", std::move(hreq));
        JsonValue hp = JsonValue::Object();
        JsonValue handleRef = JsonValue::Object();
        handleRef.Set("$ref", Str("#/$defs/handle"));
        hp.Set("id", handleRef);
        JsonValue claim = JsonValue::Object();
        claim.Set("type", Str("string"));
        claim.Set("minLength", Num64(1));
        claim.Set("maxLength", Num64(1000));
        hp.Set("claim", std::move(claim));
        JsonValue conf = JsonValue::Object();
        conf.Set("enum", StringArray({ "low", "medium", "high" }));
        conf.Set("description", Str("your own assessment, not a probability"));
        hp.Set("confidence", std::move(conf));
        JsonValue refs = JsonValue::Object();
        refs.Set("$ref", Str("#/$defs/evidence_refs"));
        hp.Set("evidence_refs", std::move(refs));
        JsonValue pred = JsonValue::Object();
        pred.Set("type", Str("string"));
        pred.Set("minLength", Num64(1));
        pred.Set("maxLength", Num64(1000));
        pred.Set("description", Str("what would be observed if the claim is true"));
        hp.Set("prediction", std::move(pred));
        JsonValue hext = JsonValue::Object();
        hext.Set("$ref", Str("#/$defs/extensions"));
        hp.Set("extensions", std::move(hext));
        h.Set("properties", std::move(hp));
        hyps.Set("items", std::move(h));
        props.Set("hypotheses", std::move(hyps));

        JsonValue actions = JsonValue::Object();
        actions.Set("type", Str("array"));
        actions.Set("maxItems", Num64(256));
        JsonValue oneOf = JsonValue::Array();
        for (const ActionSpec& spec : ActionRegistry())
        {
            JsonValue a = JsonValue::Object();
            a.Set("type", Str("object"));
            a.Set("additionalProperties", JsonValue::Bool(false));
            JsonValue areq = JsonValue::Array();
            for (const char* r : { "id", "type", "hypothesis_id" }) areq.Append(Str(r));
            if (spec.target.required) areq.Append(Str("target"));
            areq.Append(Str("parameters"));
            a.Set("required", std::move(areq));
            JsonValue ap = JsonValue::Object();
            ap.Set("id", handleRef);
            JsonValue type = JsonValue::Object();
            type.Set("const", Str(spec.type));
            ap.Set("type", std::move(type));
            ap.Set("hypothesis_id", handleRef);
            if (spec.target.required)
            {
                JsonValue t = JsonValue::Object();
                t.Set("$ref", Str("#/$defs/target"));
                t.Set("description", Str(spec.target.summary));
                ap.Set("target", std::move(t));
            }
            JsonValue params = JsonValue::Object();
            params.Set("type", Str("object"));
            params.Set("additionalProperties", JsonValue::Bool(false));
            JsonValue preq = JsonValue::Array();
            JsonValue pprops = JsonValue::Object();
            for (const ParamSpec& p : spec.params)
            {
                if (p.required) preq.Append(Str(p.name));
                pprops.Set(p.name, ParamSchema(p));
            }
            if (!preq.items().empty()) params.Set("required", std::move(preq));
            params.Set("properties", std::move(pprops));
            ap.Set("parameters", std::move(params));
            JsonValue dep = JsonValue::Object();
            dep.Set("type", Str("array"));
            dep.Set("maxItems", Num64(16));
            JsonValue depItem = JsonValue::Object();
            depItem.Set("$ref", Str("#/$defs/handle"));
            dep.Set("items", std::move(depItem));
            ap.Set("depends_on", std::move(dep));
            JsonValue arefs = JsonValue::Object();
            arefs.Set("$ref", Str("#/$defs/evidence_refs"));
            ap.Set("evidence_refs", std::move(arefs));
            JsonValue sha = JsonValue::Object();
            sha.Set("$ref", Str("#/$defs/sha256"));
            sha.Set("description", Str("optional; must equal the fingerprint SE derives from the cited evidence"));
            ap.Set("expected_code_sha256", std::move(sha));
            JsonValue aext = JsonValue::Object();
            aext.Set("$ref", Str("#/$defs/extensions"));
            ap.Set("extensions", std::move(aext));
            a.Set("properties", std::move(ap));
            a.Set("description", Str(spec.summary));
            oneOf.Append(std::move(a));
        }
        actions.Set("items", [&] { JsonValue o = JsonValue::Object(); o.Set("oneOf", std::move(oneOf)); return o; }());
        props.Set("actions", std::move(actions));
    }
    root.Set("properties", std::move(props));
    return root;
}

std::string BuildResponseSchemaText()
{
    JsonWriteOptions o;
    o.indent = 2;
    return WriteJson(BuildResponseSchema(), o) + "\n";
}

std::string BuildPromptActionList()
{
    std::string out;
    out += "Allowed action types (schema v1). Any other type is rejected. Each action has: id, type, hypothesis_id,\n"
           "target (where the type takes one), parameters, and optionally depends_on, evidence_refs and expected_code_sha256.\n"
           "A target is a bus_address, or a region and offset, or both (they must name the same place). Regions: ";
    for (size_t i = 0; i < kRegionCount; ++i) out += std::string(i ? ", " : "") + RegionApiName(static_cast<RegionId>(i));
    out += ".\nThe VDP1 frame buffer (vdp1fb) is an image with no bus address: use region and offset, and only where nothing\n"
           "below asks for a bus-addressable location.\n\n";
    for (const ActionSpec& a : ActionRegistry())
    {
        out += std::string("- ") + a.type + ": " + a.summary + "\n";
        if (a.target.required) out += std::string("  target: ") + a.target.summary + "\n";
        else                   out += std::string("  target: none (") + a.target.summary + ")\n";
        for (const ParamSpec& p : a.params)
            out += std::string("  parameter ") + p.name + (p.required ? " (required): " : " (optional): ") + ParamTypeText(p) + " -- " + p.summary + "\n";
        for (const RejectedParam& r : a.rejected)
            out += std::string("  not accepted: ") + r.name + " -- " + r.reason + "\n";
        if (a.codeSpecific || a.codeSpecificOnCodeRegion)
            out += std::string("  code-specific: cite an evidence item or range that covers the target in evidence_refs") +
                   (a.codeSpecific ? "" : " (when the target is in a code region)") + "\n";
        if (a.notes[0]) out += std::string("  rules: ") + a.notes + "\n";
        out += "\n";
    }
    return out;
}

}  // namespace ai
}  // namespace sfe
