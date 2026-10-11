// ActionRegistry tests (PLAN.md §6.3, §7.1-7.2, M0 "Done when": every v1 action has a specified adapter,
// required capability and implementation milestone; adapters onto existing models are verified against
// the code).
//
// This target DOES link the existing models -- it is the one that checks the registry's claims about
// them: that each cited model call exists with the cited signature (compile time) and behaves as the
// registry says it does (run time). The validator's own tests link none of them.
//
// Usage: ActionRegistryTests [<generated docs dir> [--write]]
//   With a directory it also checks that the checked-in response.schema.json and prompt-actions.txt are
//   exactly what the registry generates; --write regenerates them instead.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>

#include "AIFixtures.h"
#include "AIInvestigation/Encoding.h"
#include "Debug/BreakpointManager.h"
#include "Debug/CallStack.h"
#include "Debug/ExecutionActions.h"
#include "Debug/WatchList.h"
#include "SeLiveProtocol.h"

using namespace aifix;

// ---- Adapter signatures, at compile time --------------------------------------------------------
// Each line is a model call the registry names for an Existing adapter. If one is renamed or reshaped,
// this stops compiling, and the registry's row for it has to be revisited.
static_assert(std::is_same<decltype(&sfe::FunctionNames::Rename), void (sfe::FunctionNames::*)(uint32_t, const std::string&)>::value,
              "rename_function -> FunctionNames::Rename");
static_assert(std::is_same<decltype(&sfe::WatchList::Add),
                           sfe::WatchEntry& (sfe::WatchList::*)(const std::string&, const std::string&, sfe::WatchType, bool)>::value,
              "add_watch -> WatchList::Add");
static_assert(std::is_same<decltype(&sfe::BreakpointManager::EnableExecution), void (sfe::BreakpointManager::*)(uint32_t)>::value,
              "add_execution_breakpoint -> BreakpointManager::EnableExecution");
static_assert(std::is_same<decltype(&sfe::BreakpointManager::SetCondition), void (sfe::BreakpointManager::*)(uint64_t, const std::string&)>::value,
              "add_execution_breakpoint's condition -> BreakpointManager::SetCondition");
static_assert(std::is_same<decltype(&sfe::BreakpointManager::AddMemory), uint64_t (sfe::BreakpointManager::*)(uint32_t, uint32_t, sfe::BpKind)>::value,
              "add_memory_watchpoint -> BreakpointManager::AddMemory");
static_assert(std::is_same<decltype(&sfe::ExecutionActions::Add), uint64_t (sfe::ExecutionActions::*)(const sfe::ExecutionAction&)>::value,
              "add_log_tracepoint -> ExecutionActions::Add");
static_assert(std::is_same<decltype(&sfe::ExecutionActions::LogAt), const sfe::ExecutionAction* (sfe::ExecutionActions::*)(int, uint32_t) const>::value,
              "add_log_tracepoint's key check -> ExecutionActions::LogAt");

// A planned model must not already exist under the name the registry gives it; when M2 adds it, the
// registry row has to flip to Existing and this check goes with it.
#if defined(__has_include)
#  if __has_include("Debug/AddressSymbols.h")
#    error "Debug/AddressSymbols.h now exists: mark add_address_label's adapter Existing in ActionRegistry.cpp and drop this check"
#  endif
#endif

namespace
{
int gFail = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; }
}
bool Has(const std::string& s, const std::string& needle) { return s.find(needle) != std::string::npos; }

// ---- The table ----------------------------------------------------------------------------------

void TestTableShape()
{
    const std::vector<ActionSpec>& t = ActionRegistry();
    const char* expected[] = { "add_address_label", "rename_function", "add_comment", "add_watch", "add_execution_breakpoint",
                               "add_memory_watchpoint", "add_log_tracepoint", "request_evidence", "request_code_evidence" };
    Check(t.size() == 9, "nine action types in schema v1 (§7.1)");
    for (size_t i = 0; i < t.size() && i < 9; ++i)
        Check(std::string(t[i].type) == expected[i], "the types, in §7.1's order");

    std::set<std::string> types;
    for (const ActionSpec& a : t)
    {
        Check(types.insert(a.type).second, "types are unique");
        Check(FindAction(a.type) == &a, "FindAction finds each");
        Check(a.summary[0] != 0, "a summary");
        Check(a.semanticKey[0] != 0, "a semantic key rule");
        Check(a.adapter.call[0] != 0, "an adapter");
        Check(a.adapter.status == AdapterStatus::Existing ? a.adapter.milestone == 0
                                                          : (a.adapter.milestone == 2 || a.adapter.milestone == 3),
              "an existing adapter has no milestone; a planned one names M2 or M3");
        Check(a.adapter.prerequisites[0] != 0, "prerequisites are written down");
        Check(a.implementedIn == 3, "every v1 action is applied by M3");
        Check(a.notes != nullptr, "notes");
        Check(std::string(a.adapter.call).find("ToggleExecution") == std::string::npos, "ToggleExecution is never an adapter (§7.2)");

        std::set<std::string> names;
        for (const ParamSpec& p : a.params)
        {
            Check(names.insert(p.name).second, "parameter names are unique");
            Check(p.summary[0] != 0, "a parameter summary");
            Check(FindParam(a, p.name) == &p, "FindParam finds each");
            if (p.type == ParamType::Enum || p.type == ParamType::HexChoice) Check(!p.values.empty(), "a choice has values");
            if (p.type == ParamType::Integer) Check(p.min <= p.max && p.max > 0, "an integer has a range");
            if (p.type == ParamType::Text || p.type == ParamType::Condition || p.type == ParamType::Format) Check(p.maxChars > 0, "text has a length bound");
            for (const char* c = p.name; *c; ++c) Check((*c >= 'a' && *c <= 'z') || *c == '_' || (*c >= '0' && *c <= '9'), "parameter names are snake_case");
        }
        for (const RejectedParam& r : a.rejected)
        {
            Check(!names.count(r.name), "a rejected field is not also a parameter");
            Check(r.reason[0] != 0, "a rejected field says why");
        }
        const uint8_t known = kCapWatch | kCapBreakWrite | kCapSh2Code;
        for (const CapRule& c : a.target.caps)
        {
            Check(c.caps != 0 && (c.caps & ~known) == 0, "required capabilities are the three §5.3 names");
            if (c.param) Check(FindParam(a, c.param) != nullptr, "a conditional capability names a real parameter");
        }
        if (!a.target.required) Check(a.target.caps.empty() && !a.target.needsBus && !a.target.evenAddress, "an action with no target has no target rules");
    }
    Check(FindAction("run_shell") == nullptr && FindAction("") == nullptr && FindAction("ADD_WATCH") == nullptr, "anything else is not an action");
}

void TestPinnedToThePlan()
{
    // §7.2, row by row.
    const ActionSpec* label = FindAction("add_address_label");
    Check(label && label->target.needsBus && !label->codeSpecific && label->adapter.status == AdapterStatus::Planned && label->adapter.milestone == 2 &&
              std::string(label->adapter.call) == "AddressSymbols::Add",
          "labels: canonical address, AddressSymbols::Add, planned in M2");
    const ActionSpec* rename = FindAction("rename_function");
    Check(rename && rename->target.evenAddress && rename->target.caps.size() == 1 && rename->target.caps[0].caps == kCapSh2Code &&
              std::string(rename->adapter.call) == "FunctionNames::Rename" && rename->adapter.status == AdapterStatus::Existing,
          "rename: kCapSh2Code, FunctionNames::Rename (exists)");
    const ActionSpec* comment = FindAction("add_comment");
    Check(comment && comment->adapter.status == AdapterStatus::Planned && comment->adapter.milestone == 2,
          "comments: there is no comment store class yet, so the adapter is planned in M2");
    const ActionSpec* watch = FindAction("add_watch");
    Check(watch && watch->target.caps.size() == 1 && watch->target.caps[0].caps == kCapWatch && std::string(watch->adapter.call) == "WatchList::Add", "watch: kCapWatch");
    {
        const ParamSpec* t = FindParam(*watch, "data_type");
        std::vector<std::string> want = { "u8", "s8", "u16", "s16", "u32", "s32", "rgb555", "pointer" };
        std::vector<std::string> got;
        if (t) for (const char* v : t->values) got.push_back(v);
        Check(got == want, "watch types are U8/S8/U16/S16/U32/S32/RGB555/Pointer (§7.2), spelled lower-case");
        bool rejectsByteOrder = false;
        for (const RejectedParam& r : watch->rejected) rejectsByteOrder = rejectsByteOrder || (std::string(r.name) == "byte_order" && Has(r.reason, "big-endian"));
        Check(rejectsByteOrder, "no byte-order field: watches are always big-endian");
    }
    const ActionSpec* bp = FindAction("add_execution_breakpoint");
    Check(bp && bp->codeSpecific && !FindParam(*bp, "cpu") && FindParam(*bp, "condition") && std::string(bp->adapter.call).find("BreakpointManager::EnableExecution") == 0,
          "execution breakpoint: no cpu, optional condition, EnableExecution");
    {
        bool rejectsCpu = false;
        for (const RejectedParam& r : bp->rejected) rejectsCpu = rejectsCpu || std::string(r.name) == "cpu";
        Check(rejectsCpu, "and a cpu field is refused with a reason");
    }
    const ActionSpec* mw = FindAction("add_memory_watchpoint");
    Check(mw && !mw->codeSpecific && std::string(mw->adapter.call) == "BreakpointManager::AddMemory", "memory watchpoint: AddMemory");
    {
        const ParamSpec* size = FindParam(*mw, "size");
        std::vector<std::string> got;
        if (size) for (const char* v : size->values) got.push_back(v);
        Check(size && size->type == ParamType::HexChoice && got == std::vector<std::string>({ "0x1", "0x2", "0x4" }), "size is one of 1, 2, 4");
        const ParamSpec* kind = FindParam(*mw, "kind");
        std::vector<std::string> kinds;
        if (kind) for (const char* v : kind->values) kinds.push_back(v);
        Check(kinds == std::vector<std::string>({ "read", "write", "read_write" }), "kind is read, write or read-write");
        Check(!FindParam(*mw, "cpu") && !FindParam(*mw, "condition") && mw->rejected.size() == 2, "no cpu and no condition, each refused with a reason");
        bool writeNeedsBreak = false, readDoesNot = true;
        for (const CapRule& c : mw->target.caps)
        {
            if ((c.caps & kCapBreakWrite) && c.param && std::string(c.param) == "kind")
            {
                std::vector<std::string> v(c.values.begin(), c.values.end());
                writeNeedsBreak = v == std::vector<std::string>({ "write", "read_write" });
                for (const char* x : c.values) readDoesNot = readDoesNot && std::string(x) != "read";
            }
        }
        Check(writeNeedsBreak && readDoesNot, "kCapBreakWrite for the write kinds only (§7.2)");
    }
    const ActionSpec* tp = FindAction("add_log_tracepoint");
    Check(tp && tp->codeSpecific && FindParam(*tp, "cpu") && FindParam(*tp, "format") && !FindParam(*tp, "effects"), "tracepoint: cpu, format, no effects");
    Check(tp->rejected.size() >= 1 && std::string(tp->rejected[0].name) == "effects" && Has(tp->rejected[0].reason, "only write to the log"),
          "any effect other than writeToLog is refused");
    const ActionSpec* re = FindAction("request_evidence");
    const ActionSpec* rce = FindAction("request_code_evidence");
    Check(re && !re->target.required && re->adapter.milestone == 3 && rce && rce->target.required, "requests: ranges are parameters, or a function entry is the target; both M3");
    Check(!re->codeSpecific && !rce->codeSpecific, "a read-only request is not code-specific (§6.4)");

    // §6.4: the code-specific actions.
    std::set<std::string> alwaysCode;
    for (const ActionSpec& a : ActionRegistry()) if (a.codeSpecific) alwaysCode.insert(a.type);
    Check(alwaysCode == std::set<std::string>({ "rename_function", "add_execution_breakpoint", "add_log_tracepoint" }),
          "rename_function, add_execution_breakpoint and add_log_tracepoint are always code-specific");
    const CanonicalTarget code = CanonicalizeBusAddress(0x06004010).target;
    const CanonicalTarget vdp2 = CanonicalizeBusAddress(0x05E00010).target;
    Check(IsCodeSpecific(*comment, &code) && !IsCodeSpecific(*comment, &vdp2) && !IsCodeSpecific(*comment, nullptr),
          "add_comment is code-specific exactly on a kCapSh2Code region");
    Check(IsCodeSpecific(*rename, nullptr) && !IsCodeSpecific(*label, &code) && !IsCodeSpecific(*watch, &code), "and the others as the table says");

    // Limits the schema and prompt quote.
    Check(kMaxLogTracepoints == SE_LIVE_MAX_TRACE_DESCS, "256 tracepoints is the protocol's SE_LIVE_MAX_TRACE_DESCS");
    Check(kMaxCommentChars == 256, "the comment limit is §6.3's");
}

// ---- The adapters, against the code -------------------------------------------------------------

void TestAdapterBehaviour()
{
    using namespace sfe;
    // Breakpoints: EnableExecution adds, arms, and leaves an existing breakpoint alone -- unlike
    // ToggleExecution, which would remove it.
    BreakpointManager bps;
    bps.EnableExecution(0x06004010);
    Check(bps.HasEnabledExecutionAt(0x06004010), "EnableExecution adds a breakpoint");
    const uint64_t id = bps.ExecutionAt(0x06004010)->id;
    bps.SetCondition(id, "r4 == 5");
    bps.EnableExecution(0x06004010);
    Check(bps.ExecutionAt(0x06004010) && bps.ExecutionAt(0x06004010)->id == id && bps.ExecutionAt(0x06004010)->condition == "r4 == 5",
          "a second EnableExecution leaves the existing breakpoint, its condition included (so an import cannot disturb a user's)");
    bps.SetEnabled(id, false);
    bps.EnableExecution(0x06004010);
    Check(bps.HasEnabledExecutionAt(0x06004010) && bps.ExecutionAt(0x06004010)->condition == "r4 == 5", "it re-arms a disabled one without touching its condition");
    BreakpointManager toggled;
    toggled.EnableExecution(0x06004010);
    toggled.ToggleExecution(0x06004010);
    Check(!toggled.HasExecutionAt(0x06004010), "whereas ToggleExecution removes it: the reason it is never used");

    // AddMemory dedups by (address, kind, size) and returns the EXISTING id -- possibly the user's.
    BreakpointManager mem;
    const uint64_t a = mem.AddMemory(0x06012340, 2, BpKind::MemWrite);
    const uint64_t b = mem.AddMemory(0x06012340, 2, BpKind::MemWrite);
    const uint64_t c = mem.AddMemory(0x06012340, 4, BpKind::MemWrite);
    const uint64_t d = mem.AddMemory(0x06012340, 2, BpKind::MemRead);
    Check(a == b && a != c && a != d && c != d, "AddMemory dedups, and size and kind distinguish watchpoints (the registry's semantic key)");

    // Tracepoints: Add does not dedup, so the applier must check (cpu, address) first.
    ExecutionActions tps;
    ExecutionAction act;
    act.cpu = 0;
    act.address = 0x06004012;
    act.format = "x";
    tps.Add(act);
    Check(tps.LogAt(0, 0x06004012) != nullptr && tps.LogAt(1, 0x06004012) == nullptr, "LogAt is keyed by (cpu, address)");
    tps.Add(act);
    Check(tps.Count() == 2, "Add does not dedup, so a key check before it is required");

    // Watches: no dedup either, and the expression is a raw hex address today.
    WatchList watches;
    watches.Add("A", "0x06012340", WatchType::U16, true);
    watches.Add("A", "0x06012340", WatchType::U16, true);
    Check(watches.Entries().size() == 2, "WatchList::Add does not dedup");
    Check(watches.Entries()[0].id != watches.Entries()[1].id && watches.Entries()[0].id < 1000, "watch ids are small session-local counters, not persisted UUIDs");

    // Function names are keyed by the RAW address: a mirror is a different key (until M2 re-keys them).
    FunctionNames names;
    names.Rename(0x26004010, "Mirror");
    Check(names.HasName(0x26004010) && !names.HasName(0x06004010), "FunctionNames does not fold mirrors today (PLAN.md §2), hence M2's re-key");
    names.Rename(0x06004010, "Canonical");
    Check(names.NameOf(0x06004010) == "Canonical" && names.NameOf(0x26004010) == "Mirror", "so two mirrors hold two names");

    // The validator-side size table matches the Watch window's.
    for (WatchType t : kAllWatchTypes)
    {
        const uint32_t size = WatchTypeSize(t);
        const std::string name = [&] { std::string n = WatchTypeName(t); for (char& ch : n) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); return n; }();
        // The last `size` bytes of HWRAM fit; one byte further does not.
        char buf[2048];
        auto at = [&](uint32_t address) {
            std::snprintf(buf, sizeof buf, "{\"id\":\"w\",\"type\":\"add_watch\",\"hypothesis_id\":\"h1\",\"target\":{\"bus_address\":\"0x%08X\"},"
                                          "\"parameters\":{\"name\":\"W\",\"data_type\":\"%s\"}}", address, name.c_str());
            std::string doc = Replace(ValidResponse(), "\"actions\": [", std::string("\"actions\": [") + buf + ",");
            return ValidateResponse(doc, MakeManifest(), ValidateOptions());
        };
        const ValidationResult fits = at(0x06100000u - size);
        Check(!HasIssue(fits, IssueCode::RangeOutOfRegion), (name + " fits at the end of HWRAM").c_str());
        // One byte further it would straddle the end of the region. (For a one-byte type "one further" is
        // 0x06100000, which is the next copy of HWRAM and folds to offset 0, so there is nothing to refuse.)
        if (size > 1) Check(HasIssue(at(0x06100000u - size + 1), IssueCode::RangeOutOfRegion), (name + " one byte further does not").c_str());
    }
}

// ---- Generated artefacts ------------------------------------------------------------------------

// A valid sample action for every registry entry, built from the table alone. If the validator stopped
// reading the table (or the table gained a row the validator cannot handle), these stop validating.
JsonValue SampleParam(const ParamSpec& p)
{
    switch (p.type)
    {
    case ParamType::Name: return JsonValue::String("SampleName");
    case ParamType::Text: return JsonValue::String("a note");
    case ParamType::Condition: return JsonValue::String("r0 == 5");
    case ParamType::Format: return JsonValue::String("r0 = {r0}");
    case ParamType::Hex:
        // max_bytes must cover the sample's ranges; the other hex parameters take their smallest value.
        return JsonValue::String(FormatHex(std::string(p.name) == "max_bytes" ? p.max : (p.min ? p.min : 1)));
    case ParamType::HexChoice:
    case ParamType::Enum: return JsonValue::String(p.values[0]);
    case ParamType::Integer: return JsonValue::UInt(p.min);
    case ParamType::Bool: return JsonValue::Bool(true);
    case ParamType::Uuid: return JsonValue::String(kEvidence);
    case ParamType::Handle: return JsonValue::String("A");
    case ParamType::RangeList:
    {
        JsonValue r = JsonValue::Object();
        r.Set("region", JsonValue::String("hwram"));
        r.Set("offset", JsonValue::String("0x4000"));
        r.Set("length", JsonValue::String("0x10"));
        JsonValue a = JsonValue::Array();
        a.Append(r);
        return a;
    }
    }
    return JsonValue();
}

JsonValue SampleAction(const ActionSpec& spec, bool includeOptional)
{
    JsonValue a = JsonValue::Object();
    a.Set("id", JsonValue::String("s1"));
    a.Set("type", JsonValue::String(spec.type));
    a.Set("hypothesis_id", JsonValue::String("h2"));
    if (spec.target.required)
    {
        JsonValue t = JsonValue::Object();
        t.Set("bus_address", JsonValue::String("0x06004010"));
        a.Set("target", t);
    }
    JsonValue params = JsonValue::Object();
    for (const ParamSpec& p : spec.params)
        if (p.required || includeOptional) params.Set(p.name, SampleParam(p));
    a.Set("parameters", params);
    return a;
}

ValidationResult ValidateSample(const JsonValue& action)
{
    JsonValue doc;
    ParseJson(ValidResponse(), JsonLimits(), &doc, nullptr);
    JsonValue only = JsonValue::Array();
    only.Append(action);
    doc.Set("actions", only);
    return ValidateResponseJson(doc, MakeManifest(), ValidateOptions());
}

void TestEveryEntryValidates()
{
    for (const ActionSpec& spec : ActionRegistry())
    {
        const ValidationResult r = ValidateSample(SampleAction(spec, false));
        if (!r.ok)
        {
            std::printf("FAIL: the required-only sample of %s does not validate:\n", spec.type);
            for (const ValidationIssue& i : r.issues) std::printf("    %s: %s\n", i.where.c_str(), i.message.c_str());
            ++gFail;
        }
        Check(r.actions.size() == 1 && r.actions[0].spec == &spec, "and the validator resolved it to this registry row");

        // Every required parameter really is required, and a mistyped one is refused.
        for (const ParamSpec& p : spec.params)
        {
            if (!p.required) continue;
            JsonValue action = SampleAction(spec, false);
            JsonValue params = JsonValue::Object();
            for (const JsonValue::Member& m : action.Find("parameters")->members())
                if (m.first != p.name) params.Set(m.first, m.second);
            action.Set("parameters", params);
            const ValidationResult missing = ValidateSample(action);
            Check(!missing.ok && HasIssue(missing, IssueCode::MissingField, p.name), (std::string(spec.type) + ": missing " + p.name + " is refused").c_str());

            JsonValue wrong = SampleAction(spec, false);
            JsonValue wp = *wrong.Find("parameters");
            wp.Set(p.name, p.type == ParamType::Integer ? JsonValue::String("x") : JsonValue::UInt(7));
            wrong.Set("parameters", wp);
            Check(!ValidateSample(wrong).ok, (std::string(spec.type) + ": a wrongly typed " + p.name + " is refused").c_str());
        }
        JsonValue extra = SampleAction(spec, false);
        JsonValue ep = *extra.Find("parameters");
        ep.Set("nonsense", JsonValue::UInt(1));
        extra.Set("parameters", ep);
        Check(HasIssue(ValidateSample(extra), IssueCode::UnknownField, "nonsense"), (std::string(spec.type) + ": an unknown parameter is refused").c_str());

        // Every parameter the table names is accepted by the validator in some valid combination.
        for (const ParamSpec& p : spec.params)
        {
            JsonValue action = SampleAction(spec, false);
            JsonValue pp = *action.Find("parameters");
            pp.Set(p.name, SampleParam(p));
            if (std::string(spec.type) == "add_address_label" && std::string(p.name) == "count") pp.Set("data_type", JsonValue::String("u8"));
            if (std::string(spec.type) == "add_log_tracepoint" && std::string(p.name) == "repeat_n") pp.Set("repeat", JsonValue::String("every_n"));
            action.Set("parameters", pp);
            const ValidationResult r = ValidateSample(action);
            Check(r.ok, (std::string(spec.type) + ": parameter " + p.name + " is accepted by the validator").c_str());
        }
    }
}

// Read without carriage returns: a Windows checkout may have converted the checked-in file's line endings,
// and the generator writes '\n'.
std::string ReadFile(const std::string& path)
{
    std::ifstream f(path.c_str(), std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string text = ss.str();
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    return text;
}
bool WriteFile(const std::string& path, const std::string& text)
{
    std::ofstream f(path.c_str(), std::ios::binary);
    f << text;
    return static_cast<bool>(f);
}

void TestGenerated(const std::string& dir, bool write)
{
    const std::string schemaText = BuildResponseSchemaText();
    const std::string promptText = BuildPromptActionList();

    // The schema is strict JSON that SE's own reader accepts, and is stable.
    JsonValue schema;
    JsonError err;
    Check(ParseJson(schemaText, JsonLimits(), &schema, &err), "response.schema.json parses with the strict reader");
    Check(schema.Equals(BuildResponseSchema()) && BuildResponseSchemaText() == schemaText, "and generation is deterministic");
    Check(schema.Find("$schema") && schema.Find("additionalProperties") && !schema.Find("additionalProperties")->AsBool(), "unknown fields are refused at the top level");
    const JsonValue* req = schema.Find("required");
    std::vector<std::string> reqNames;
    if (req) for (const JsonValue& v : req->items()) reqNames.push_back(v.AsString());
    Check(reqNames == std::vector<std::string>({ "schema_version", "investigation_id", "evidence_id", "response_id", "hypotheses", "actions" }),
          "the six required fields (§6.3)");
    const JsonValue* props = schema.Find("properties");
    Check(props && !props->Has("experiment"), "schema v1 has no experiment field");
    Check(props && props->Find("schema_version") && props->Find("schema_version")->Find("const") && props->Find("schema_version")->Find("const")->ToUint64(nullptr), "schema_version is const 1");

    // One oneOf branch per registry row, in order, each with the row's parameters.
    const JsonValue* oneOf = props && props->Find("actions") && props->Find("actions")->Find("items") ? props->Find("actions")->Find("items")->Find("oneOf") : nullptr;
    Check(oneOf && oneOf->items().size() == ActionRegistry().size(), "the schema has one branch per registry row");
    for (size_t i = 0; oneOf && i < oneOf->items().size() && i < ActionRegistry().size(); ++i)
    {
        const ActionSpec& spec = ActionRegistry()[i];
        const JsonValue& branch = oneOf->items()[i];
        const JsonValue* type = branch.Find("properties") ? branch.Find("properties")->Find("type") : nullptr;
        Check(type && type->Find("const") && type->Find("const")->AsString() == spec.type, "the branch is for this action type");
        const JsonValue* params = branch.Find("properties") ? branch.Find("properties")->Find("parameters") : nullptr;
        const JsonValue* pprops = params ? params->Find("properties") : nullptr;
        Check(pprops && pprops->members().size() == spec.params.size(), "with exactly its parameters");
        const JsonValue* preq = params ? params->Find("required") : nullptr;
        size_t required = 0;
        for (const ParamSpec& p : spec.params) required += p.required ? 1 : 0;
        Check((preq ? preq->items().size() : 0) == required, "and its required ones");
        bool hasTarget = false;
        if (const JsonValue* br = branch.Find("required"))
            for (const JsonValue& v : br->items()) hasTarget = hasTarget || v.AsString() == "target";
        Check(hasTarget == spec.target.required, "a target is required exactly when the row says so");
        Check(params && params->Find("additionalProperties") && !params->Find("additionalProperties")->AsBool(), "unknown parameters are refused");
    }

    // The prompt names every action and parameter, and every refused field with its reason.
    for (const ActionSpec& a : ActionRegistry())
    {
        Check(Has(promptText, std::string("- ") + a.type + ":"), "the prompt lists each action type");
        for (const ParamSpec& p : a.params) Check(Has(promptText, std::string("parameter ") + p.name + (p.required ? " (required)" : " (optional)")), "and each parameter");
        for (const RejectedParam& r : a.rejected) Check(Has(promptText, std::string("not accepted: ") + r.name) && Has(promptText, r.reason), "and each refused field with its reason");
        if (a.codeSpecific) Check(Has(promptText, "code-specific"), "and which actions are code-specific");
    }
    Check(Has(promptText, "0x") && Has(promptText, "upper-case"), "the prompt states the hex spelling");

    // Adding a row would flow through: simulate by checking that nothing in the output is hand-written
    // per action type except what the table holds.
    Check(schemaText.find("run_shell") == std::string::npos && promptText.find("run_shell") == std::string::npos, "no unlisted action appears");

    if (dir.empty()) return;
    const std::string schemaPath = dir + "/response.schema.json";
    const std::string promptPath = dir + "/prompt-actions.txt";
    if (write)
    {
        Check(WriteFile(schemaPath, schemaText) && WriteFile(promptPath, promptText), "wrote the generated reference files");
        std::printf("wrote %s and %s\n", schemaPath.c_str(), promptPath.c_str());
        return;
    }
    Check(ReadFile(schemaPath) == schemaText, "Docs/AIInvestigation/Generated/response.schema.json is what the registry generates (regenerate: ActionRegistryTests <dir> --write)");
    Check(ReadFile(promptPath) == promptText, "Docs/AIInvestigation/Generated/prompt-actions.txt is what the registry generates (regenerate: ActionRegistryTests <dir> --write)");
}
}  // namespace

int main(int argc, char** argv)
{
    TestTableShape();
    TestPinnedToThePlan();
    TestAdapterBehaviour();
    TestEveryEntryValidates();
    TestGenerated(argc > 1 ? argv[1] : "", argc > 2 && std::strcmp(argv[2], "--write") == 0);
    if (gFail == 0) std::printf("ActionRegistryTests: all passed\n");
    return gFail == 0 ? 0 : 1;
}
