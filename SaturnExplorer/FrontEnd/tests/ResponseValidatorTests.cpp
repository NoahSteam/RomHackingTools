// ResponseValidator tests (PLAN.md §6, §12 "Response"): extraction, strict parsing, schema v1, targets,
// parameters, evidence resolution, code fingerprints, duplicates, identity, and the preview.
//
// This target links no application model (no WatchList, BreakpointManager, FunctionNames, ...): the
// validator has to work without them, which is part of "no side effects".
#include <cstdio>
#include <string>
#include <vector>

#include "AIFixtures.h"
#include "AIInvestigation/Encoding.h"

using namespace aifix;

namespace
{
int gFail = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; }
}
bool Has(const std::string& s, const std::string& needle) { return s.find(needle) != std::string::npos; }

ValidationResult Validate(const std::string& text, const ValidateOptions& o = ValidateOptions(), const Manifest& m = MakeManifest())
{
    return ValidateResponse(text, m, o);
}
ValidationResult V(const std::string& text) { return Validate(text); }

// The first issue's message, or empty: tests quote it, and a validator that wrongly accepted must fail the
// test rather than crash it.
std::string Msg(const ValidationResult& r) { return r.issues.empty() ? std::string() : r.issues[0].message; }

// The list of messages, for diagnosing a failure.
std::string Dump(const ValidationResult& r)
{
    std::string s;
    for (const ValidationIssue& i : r.issues) s += "  [" + std::string(IssueCodeName(i.code)) + "] " + i.where + ": " + i.message + "\n";
    return s;
}
void Expect(const ValidationResult& r, IssueCode code, const char* what, const char* where = nullptr)
{
    if (r.ok || !HasIssue(r, code, where))
    {
        std::printf("FAIL: %s (wanted %s)\n%s", what, IssueCodeName(code), Dump(r).c_str());
        ++gFail;
    }
}
void ExpectOk(const ValidationResult& r, const char* what)
{
    if (!r.ok)
    {
        std::printf("FAIL: %s\n%s", what, Dump(r).c_str());
        ++gFail;
    }
}

// Edit one action of the valid response: replace the text of its JSON.
std::string With(const std::string& from, const std::string& to) { return Replace(ValidResponse(), from, to); }

// ---- 1. Extraction ------------------------------------------------------------------------------

void TestExtraction()
{
    const std::string doc = ValidResponse();
    Extraction e = ExtractResponseJson(doc);
    Check(e.ok && !e.fenced && e.json.find("schema_version") != std::string::npos, "a bare document");
    e = ExtractResponseJson("  \n\t" + doc + "\n\n");
    Check(e.ok && e.json[0] == '{', "surrounding whitespace is trimmed");
    e = ExtractResponseJson("\xEF\xBB\xBF" + doc);
    Check(e.ok, "a leading BOM is ignored");

    e = ExtractResponseJson("Here is my analysis.\n\n```json\n" + doc + "\n```\n\nLet me know.");
    Check(e.ok && e.fenced && e.json.find("\"actions\"") != std::string::npos && e.json.find("analysis") == std::string::npos,
          "exactly one fenced json block among prose");
    Check(ExtractResponseJson("```JSON\n{}\n```").ok, "the label is case-insensitive");
    Check(ExtractResponseJson("```json title=\"x\"\n{}\n```").ok, "extra words after the label are allowed");
    Check(ExtractResponseJson("text\r\n```json\r\n{\"a\":1}\r\n```\r\n").json == "{\"a\":1}\n", "CRLF line endings");
    Check(ExtractResponseJson("````json\n{\"a\":\"```\"}\n````").ok, "a longer fence can contain ```");
    Check(ExtractResponseJson("   ```json\n{}\n   ```").ok, "up to three spaces of indent");

    // Other fenced blocks are ignored; only json-labelled ones count.
    e = ExtractResponseJson("```cpp\nint x;\n```\ntext\n```json\n{\"a\":1}\n```\n```text\nnotes\n```");
    Check(e.ok && e.json == "{\"a\":1}\n", "other labelled blocks are ignored");

    e = ExtractResponseJson("```json\n{}\n```\nand\n```json\n{}\n```");
    Check(!e.ok && Has(e.error, "2") && Has(e.error, "exactly one"), "two json blocks are rejected");
    e = ExtractResponseJson("I could not find anything.");
    Check(!e.ok && Has(e.error, "no JSON found"), "no JSON at all");
    e = ExtractResponseJson("```\n{\"a\":1}\n```");
    Check(!e.ok, "an unlabelled fence is not a json block");
    e = ExtractResponseJson("```text\n{\"a\":1}\n```");
    Check(!e.ok, "a differently labelled fence is not a json block");
    e = ExtractResponseJson("~~~json\n{}\n~~~");
    Check(!e.ok, "tilde fences are not recognised");
    e = ExtractResponseJson("```json\n{\"a\":1}\n");
    Check(!e.ok && Has(e.error, "never closed"), "an unterminated fence");
    e = ExtractResponseJson("```json\n```");
    Check(e.ok && e.json.empty(), "an empty block extracts as empty (and then fails to parse)");
    e = ExtractResponseJson("");
    Check(!e.ok, "empty");
    e = ExtractResponseJson("   \n ");
    Check(!e.ok, "whitespace only");
    e = ExtractResponseJson("`` `json\n{}\n`` `");
    Check(!e.ok, "stray backticks are not a fence");
    e = ExtractResponseJson("Use ```json inline``` like so.");
    Check(!e.ok, "inline triple backticks are not a fence");

    // A bare document followed by prose is not accepted by guessing where it ends.
    ValidationResult r = V(doc + "\nHope this helps!");
    Expect(r, IssueCode::NotJson, "a bare document with trailing prose");
    Check(Has(Msg(r), "single ```json fenced block"), "and the message says what to do");
    r = V("Sure!\n" + doc);
    Expect(r, IssueCode::NotJson, "prose before an unfenced document");
    r = V("```json\n" + doc + "\n```");
    ExpectOk(r, "a valid response in a fenced block");
    Check(r.fenced, "and it says so");
    r = V("```json\n```");
    Expect(r, IssueCode::NotJson, "an empty fenced block");
}

// ---- 2. A valid response ------------------------------------------------------------------------

void TestValid()
{
    const ValidationResult r = V(ValidResponse());
    ExpectOk(r, "the fixture response");
    Check(r.hypotheses.size() == 2 && r.actions.size() == 8, "two hypotheses, eight actions");
    Check(r.investigationId == kInvestigation && r.evidenceId == kEvidence && r.responseId == kResponse, "ids are carried");
    Check(!r.needsRebind && !r.rebound && r.boundEvidenceId == kEvidence, "bound to the package it names");
    Check(r.extensions.IsObject() && r.extensions.Has("com.example.tool"), "extensions are retained");
    for (const ValidatedAction& a : r.actions)
        Check(a.valid && a.spec && IsSha256Hex(a.payloadSha256), "every action is valid, with a registry spec and a payload hash");

    auto find = [&](const char* id) -> const ValidatedAction* {
        for (const ValidatedAction& a : r.actions) if (a.id == id) return &a;
        return nullptr;
    };
    const ValidatedAction* a1 = find("a1");
    Check(a1 && a1->hasTarget && a1->target.region == RegionId::Hwram && a1->target.offset == 0x12340 && a1->target.bus == 0x06012340u,
          "a bus address resolves to its canonical target");
    Check(!a1->codeSpecific && !a1->hasFingerprint, "a label is not code-specific");
    const ValidatedAction* a3 = find("a3");
    Check(a3 && a3->target == a1->target, "{region, offset} and the bus address are the same target");
    const ValidatedAction* a4 = find("a4");
    Check(a4 && a4->codeSpecific && a4->hasFingerprint && a4->fingerprint.sha256 == HwramCodeHash() &&
              a4->fingerprint.evidenceRef == "code/range-1" && a4->fingerprint.offset == 0x4000 && a4->fingerprint.length == 0x40,
          "a rename is code-specific, and SE derived the fingerprint from the hypothesis's evidence");
    const ValidatedAction* a5 = find("a5");
    Check(a5 && a5->codeSpecific && a5->fingerprint.sha256 == HwramCodeHash(), "so is a breakpoint, whose expected hash agreed");
    const ValidatedAction* a6 = find("a6");
    Check(a6 && a6->codeSpecific && a6->hasFingerprint, "so is a tracepoint");
    const ValidatedAction* a7 = find("a7");
    Check(a7 && a7->codeSpecific, "a comment in a code region is code-specific");
    const ValidatedAction* a8 = find("a8");
    Check(a8 && !a8->hasTarget && !a8->codeSpecific && a8->semanticKey.empty(), "a read-only request has no target and no key");

    Check(a1->semanticKey == "add_address_label|hwram+0x12340" && find("a2")->semanticKey == "add_watch|hwram+0x12340|u16" &&
              a3->semanticKey == "add_memory_watchpoint|hwram+0x12340|write|0x2" && a6->semanticKey == "add_log_tracepoint|cpu0|hwram+0x4012",
          "semantic keys are built from the canonical target and the parameters that identify the item");

    // Dependencies come first, otherwise in the order written.
    Check(r.applyOrder.size() == 8, "every action is ordered");
    auto pos = [&](const char* id) { for (size_t i = 0; i < r.applyOrder.size(); ++i) if (r.applyOrder[i] == id) return static_cast<int>(i); return -1; };
    Check(pos("a1") < pos("a2") && pos("a1") < pos("a3"), "a label is applied before the watch that depends on it");
    Check(r.applyOrder[0] == "a1" && r.applyOrder[1] == "a2" && r.applyOrder[2] == "a3" && r.applyOrder[3] == "a4", "otherwise written order");
    Check(find("a6")->warnings.size() == 1 && Has(find("a6")->warnings[0], "next snapshot"), "memory in a format is flagged as read at the next snapshot");
}

// ---- 3. Schema ----------------------------------------------------------------------------------

void TestTopLevel()
{
    for (const char* field : { "schema_version", "investigation_id", "evidence_id", "response_id", "hypotheses", "actions" })
    {
        JsonValue doc;
        ParseJson(ValidResponse(), JsonLimits(), &doc, nullptr);
        JsonValue stripped = JsonValue::Object();
        for (const JsonValue::Member& m : doc.members())
            if (m.first != field) stripped.Set(m.first, m.second);
        const ValidationResult r = ValidateResponseJson(stripped, MakeManifest(), ValidateOptions());
        Check(!r.ok && HasIssue(r, IssueCode::MissingField, field), (std::string("missing ") + field).c_str());
    }
    Expect(V(With("\"extensions\":", "\"surprise\": 1, \"extensions\":")), IssueCode::UnknownField, "an unknown top-level field", "surprise");

    // Schema v1 has no experiment field, and says so rather than calling it unknown.
    ValidationResult r = V(With("\"extensions\":", "\"experiment\": {\"steps\": []}, \"extensions\":"));
    Expect(r, IssueCode::UnsupportedInSchemaV1, "experiment is rejected");
    Check(!HasIssue(r, IssueCode::UnknownField, "experiment") && Has(Msg(r), "unsupported in schema v1"),
          "as unsupported in schema v1, not as unknown");
    Expect(V(With("\"id\": \"a1\", ", "\"experiment\": 1, \"id\": \"a1\", ")), IssueCode::UnsupportedInSchemaV1, "experiment on an action", "actions[0].experiment");
    Expect(V(With("\"claim\": \"General HP, 16-bit\",", "\"experiment\": 1, \"claim\": \"General HP, 16-bit\",")), IssueCode::UnsupportedInSchemaV1, "experiment on a hypothesis");

    Expect(V(With("\"schema_version\": 1", "\"schema_version\": 2")), IssueCode::BadSchemaVersion, "schema 2");
    Expect(V(With("\"schema_version\": 1", "\"schema_version\": 0")), IssueCode::BadSchemaVersion, "schema 0");
    Expect(V(With("\"schema_version\": 1", "\"schema_version\": \"1\"")), IssueCode::BadSchemaVersion, "schema as a string");
    Expect(V(With("\"schema_version\": 1", "\"schema_version\": 1.0")), IssueCode::NotJson, "schema as a real number");
    Expect(V(With("\"schema_version\": 1", "\"schema_version\": 18446744073709551616")), IssueCode::NotJson, "an integer past 2^64");
    Expect(V(With("2f1c0a9e-4b7d-4c21-9a3e-0d5b6c7e8f90", "2F1C0A9E-4B7D-4C21-9A3E-0D5B6C7E8F90")), IssueCode::BadValue, "an upper-case UUID");
    Expect(V(With("c4e2a7b1-5d6e-4f70-8192-a3b4c5d6e7f8", "c4e2")), IssueCode::BadValue, "an abbreviated response id", "response_id");

    Expect(V("[]"), IssueCode::NotJson, "an array is not a document");
    Expect(V("{\"schema_version\":1,\"schema_version\":1}"), IssueCode::NotJson, "duplicate keys");
    Check(Has(Msg(V("{\"a\":1,\"a\":2}")), "duplicate object key"), "and it says so");
    ValidationResult empty = V("{}");
    Check(!empty.ok && empty.issues.size() == 6, "an empty object misses all six required fields");
}

void TestHypotheses()
{
    Expect(V(With("\"id\": \"h2\"", "\"id\": \"h1\"")), IssueCode::DuplicateId, "duplicate hypothesis ids");
    Expect(V(With("\"confidence\": \"medium\"", "\"confidence\": \"certain\"")), IssueCode::BadValue, "confidence is low, medium or high");
    Expect(V(With("\"confidence\": \"medium\"", "\"confidence\": 3")), IssueCode::WrongType, "confidence is a word, not a number");
    Expect(V(With("\"confidence\": \"medium\"", "\"confidence\": 0.9")), IssueCode::NotJson, "and certainly not a probability");
    Expect(V(With("\"claim\": \"General HP, 16-bit\"", "\"claim\": \"\"")), IssueCode::BadValue, "an empty claim");
    Expect(V(With("\"prediction\": \"A damage event decreases this value\"", "\"prediction\": \"\"")), IssueCode::BadValue, "an empty prediction");
    Expect(V(With("\"claim\": \"General HP, 16-bit\"", "\"claim\": \"two\\nlines\"")), IssueCode::Grammar, "a multi-line claim");
    Expect(V(With("\"id\": \"h1\", \"claim\"", "\"id\": \"h 1\", \"claim\"")), IssueCode::Grammar, "a bad hypothesis id");
    Expect(V(With("\"evidence_refs\": [\"diff/range-12\"], \"prediction\"", "\"prediction\"")), IssueCode::MissingField, "evidence_refs is required on a hypothesis");
    Expect(V(With("\"confidence\": \"medium\",", "\"confidence\": \"medium\", \"weight\": 3,")), IssueCode::UnknownField, "an unknown hypothesis field");
    Expect(V(With("\"hypotheses\": [\n", "\"hypotheses\": [ 7,\n")), IssueCode::WrongType, "a hypothesis that is not an object");
    ValidateOptions o;
    o.limits.maxHypotheses = 1;
    Expect(Validate(ValidResponse(), o), IssueCode::LimitExceeded, "too many hypotheses");
    // An empty evidence_refs list is allowed on a hypothesis: not every claim has a cited range.
    ExpectOk(V(With("\"evidence_refs\": [\"diff/range-12\"], \"prediction\"", "\"evidence_refs\": [], \"prediction\"")), "an empty evidence_refs list");
}

void TestActionsShape()
{
    Expect(V(With("\"type\": \"add_watch\"", "\"type\": \"run_shell\"")), IssueCode::UnknownActionType, "an unlisted action type");
    Check(Has(Msg(V(With("\"type\": \"add_watch\"", "\"type\": \"run_shell\""))), "add_address_label"), "the message lists what is allowed");
    for (const char* t : { "write_memory", "add_script", "fetch_url", "launch", "toggle_execution_breakpoint", "experiment", "ADD_WATCH", "" })
        Expect(V(With("\"type\": \"add_watch\"", std::string("\"type\": \"") + t + "\"")), IssueCode::UnknownActionType, (std::string("type ") + t).c_str());
    Expect(V(With("\"type\": \"add_watch\"", "\"type\": 7")), IssueCode::WrongType, "a numeric type");
    Expect(V(With("\"id\": \"a2\"", "\"id\": \"a1\"")), IssueCode::DuplicateId, "duplicate action ids");
    Expect(V(With("\"hypothesis_id\": \"h1\", \"depends_on\"", "\"hypothesis_id\": \"h9\", \"depends_on\"")), IssueCode::UnknownHypothesis, "an unknown hypothesis_id");
    Expect(V(With("\"depends_on\": [\"a1\"],\n      \"target\": { \"bus_address\": \"0x06012340\" },\n      \"parameters\": { \"name\": \"GeneralHP\", \"data_type\"",
                  "\"depends_on\": [\"a1\"], \"colour\": 1,\n      \"target\": { \"bus_address\": \"0x06012340\" },\n      \"parameters\": { \"name\": \"GeneralHP\", \"data_type\"")),
           IssueCode::UnknownField, "an unknown action field");
    Expect(V(With("\"actions\": [", "\"actions\": [ \"x\", ")), IssueCode::WrongType, "an action that is not an object");
    ValidateOptions o;
    o.limits.maxActions = 3;
    Expect(Validate(ValidResponse(), o), IssueCode::LimitExceeded, "too many actions");
    Expect(V(With("\"parameters\": { \"name\": \"ApplyDamage\" }", "\"parameters\": []")), IssueCode::WrongType, "parameters must be an object");
    Expect(V(With(",\n      \"parameters\": { \"name\": \"ApplyDamage\" }", "")), IssueCode::MissingField, "parameters are required");
    Expect(V(With("\"depends_on\": [\"a1\"],\n      \"target\": { \"region\"", "\"depends_on\": \"a1\",\n      \"target\": { \"region\"")), IssueCode::WrongType, "depends_on must be an array");

    // An action invalid on its own leaves the others valid, and marks only itself.
    const ValidationResult r = V(With("\"name\": \"ApplyDamage\"", "\"name\": \"9bad\""));
    Check(!r.ok, "one bad name fails the response");
    int bad = 0;
    for (const ValidatedAction& a : r.actions) bad += a.valid ? 0 : 1;
    Check(bad == 1 && r.actions.size() == 8, "and exactly the action it concerns is marked invalid");
}

void TestExtensions()
{
    Expect(V(With("\"com.example.tool\"", "\"tool\"")), IssueCode::Grammar, "extension keys must be namespaced");
    Expect(V(With("\"com.example.tool\"", "\"Com.Example\"")), IssueCode::Grammar, "namespaces are lower case");
    Expect(V(With("\"extensions\": { \"com.example.tool\": { \"note\": \"kept as is\" } }", "\"extensions\": []")), IssueCode::WrongType, "extensions is an object");
    ExpectOk(V(With("\"com.example.tool\": { \"note\": \"kept as is\" }", "\"com.example.tool\": { \"any\": [1, {\"deep\": null}] }, \"org.x\": true")),
             "free-form content inside extensions");
    const ValidationResult r = V(ValidResponse());
    // Extensions on an action and on a hypothesis are accepted and part of the payload hash.
    const std::string withExt = With("\"id\": \"a7\", ", "\"id\": \"a7\", \"extensions\": {\"com.example.x\": 1}, ");
    const ValidationResult r2 = V(withExt);
    ExpectOk(r2, "extensions on an action");
    Check(r2.actions[6].payloadSha256 != r.actions[6].payloadSha256, "which change that action's payload hash");
}

// ---- 4. Evidence --------------------------------------------------------------------------------

void TestEvidenceIds()
{
    const std::string other = With("9b07e1c4-1a2b-4c3d-8e4f-5a6b7c8d9e0f", kOtherEvidence);
    ValidationResult r = V(other);
    Expect(r, IssueCode::EvidenceIdMismatch, "an unknown evidence_id");
    Check(r.needsRebind && !r.rebound, "the import stops and asks for an explicit rebind");
    Check(r.actions.empty() && r.hypotheses.empty(), "and validates nothing further against the wrong package");
    Check(Has(Msg(r), kOtherEvidence), "naming the id it found");

    ValidateOptions rebound;
    rebound.rebound = true;
    r = Validate(other, rebound);
    ExpectOk(r, "the same response, rebound by the user");
    Check(r.rebound && !r.needsRebind && r.evidenceId == kOtherEvidence && r.boundEvidenceId == kEvidence,
          "the rebinding is recorded: the response's id and the package it was validated against");
    Check(r.actions.size() == 8, "and every action is re-validated against the chosen package");

    // After rebinding, a reference that does not exist in the chosen package is still an error.
    r = Validate(Replace(other, "diff/range-12", "elsewhere/range-1"), rebound);
    Expect(r, IssueCode::UnknownEvidenceRef, "a reference missing from the chosen package");
    // Code evidence is re-derived from the chosen package, not carried over.
    Manifest altered = MakeManifest();
    altered.items[2].ranges[0].sha256 = Fill('9');
    altered.files[0].sha256 = Fill('9');
    r = Validate(other, rebound, altered);
    Expect(r, IssueCode::FingerprintMismatch, "the fingerprint comes from the chosen package, so the agent's hash no longer matches");

    r = V(With("2f1c0a9e-4b7d-4c21-9a3e-0d5b6c7e8f90", "11111111-1111-4111-8111-111111111111"));
    Expect(r, IssueCode::InvestigationIdMismatch, "a different investigation");
    Check(r.needsRebind, "also needs an explicit rebind");
    // Both wrong: both reported.
    r = V(Replace(With("2f1c0a9e-4b7d-4c21-9a3e-0d5b6c7e8f90", "11111111-1111-4111-8111-111111111111"), kEvidence, kOtherEvidence));
    Check(HasIssue(r, IssueCode::InvestigationIdMismatch) && HasIssue(r, IssueCode::EvidenceIdMismatch), "both mismatches are reported");
}

void TestEvidenceRefs()
{
    Expect(V(With("[\"diff/range-12\"]", "[\"nope/range-1\"]")), IssueCode::UnknownEvidenceRef, "an unknown item");
    Expect(V(With("[\"diff/range-12\"]", "[\"diff/range-99\"]")), IssueCode::UnknownEvidenceRef, "an unknown range");
    Expect(V(With("[\"diff/range-12\"]", "[\"diff/\"]")), IssueCode::UnknownEvidenceRef, "a malformed reference");
    Expect(V(With("[\"diff/range-12\"]", "[\"diff/range-12/x\"]")), IssueCode::UnknownEvidenceRef, "too many segments");
    Expect(V(With("[\"diff/range-12\"]", "[\"\"]")), IssueCode::UnknownEvidenceRef, "an empty reference");
    Expect(V(With("[\"diff/range-12\"]", "[7]")), IssueCode::WrongType, "a non-string reference");
    Expect(V(With("[\"diff/range-12\"]", "\"diff/range-12\"")), IssueCode::WrongType, "evidence_refs as a string");
    ExpectOk(V(With("[\"diff/range-12\"]", "[\"diff\", \"mem-a/r1\", \"watches\"]")), "whole-item references and a configuration item resolve");
    Check(Has(Msg(V(With("[\"diff/range-12\"]", "[\"diff/range-99\"]"))), "range-99"), "the message names the missing range");
    ValidateOptions o;
    o.limits.maxEvidenceRefs = 1;
    Expect(Validate(With("[\"diff/range-12\"]", "[\"diff\", \"mem-a\"]"), o), IssueCode::LimitExceeded, "too many references");
    // An unresolved reference on an action, not just a hypothesis.
    Expect(V(With("\"evidence_refs\": [\"code/range-1\"],\n      \"expected_code_sha256\"", "\"evidence_refs\": [\"ghost\"],\n      \"expected_code_sha256\"")),
           IssueCode::UnknownEvidenceRef, "an unknown reference on an action", "actions[4]");
}

void TestCodeSpecific()
{
    const std::string noRefs = "\"evidence_refs\": [\"code/range-1\"], \"prediction\"";
    // Remove the hypothesis's evidence: the rename, the breakpoint (which also cites on its own), the
    // tracepoint and the comment lose their coverage unless they cite it themselves.
    ValidationResult r = V(With(noRefs, "\"evidence_refs\": [], \"prediction\""));
    Expect(r, IssueCode::CodeEvidenceMissing, "a code-specific action without covering evidence", "actions[3]");
    Check(HasIssue(r, IssueCode::CodeEvidenceMissing, "actions[5]") && HasIssue(r, IssueCode::CodeEvidenceMissing, "actions[6]"),
          "a tracepoint and a comment too");
    Check(!HasIssue(r, IssueCode::CodeEvidenceMissing, "actions[4]"), "but not the breakpoint, which cites the range itself");

    // The range must cover the target, be code, and be in the same region.
    Expect(V(With("\"bus_address\": \"0x06004010\" },\n      \"parameters\": { \"name\": \"ApplyDamage\"", "\"bus_address\": \"0x06005000\" },\n      \"parameters\": { \"name\": \"ApplyDamage\"")),
           IssueCode::CodeEvidenceMissing, "a rename outside the cited range");
    Expect(V(With(noRefs, "\"evidence_refs\": [\"diff/range-12\"], \"prediction\"")), IssueCode::CodeEvidenceMissing, "a data range is not code evidence");
    Expect(V(With(noRefs, "\"evidence_refs\": [\"code/range-2\"], \"prediction\"")), IssueCode::CodeEvidenceMissing, "a code range in another region");
    Expect(V(With(noRefs, "\"evidence_refs\": [\"mem-a/r1\"], \"prediction\"")), IssueCode::CodeEvidenceMissing, "attached data is not code evidence");

    // Boundaries of the covered range [0x4000, 0x4040): an instruction needs both its bytes inside.
    auto renameAt = [&](const char* address) {
        return V(With("\"bus_address\": \"0x06004010\" },\n      \"parameters\": { \"name\": \"ApplyDamage\"",
                      std::string("\"bus_address\": \"") + address + "\" },\n      \"parameters\": { \"name\": \"ApplyDamage\""));
    };
    ExpectOk(renameAt("0x06004000"), "the first instruction of the range");
    ExpectOk(renameAt("0x0600403E"), "the last whole instruction of the range");
    Expect(renameAt("0x06004040"), IssueCode::CodeEvidenceMissing, "the instruction just past the range");
    Expect(renameAt("0x06003FFE"), IssueCode::CodeEvidenceMissing, "the instruction just before it");

    // Whole-item references cover through their ranges.
    ExpectOk(V(With(noRefs, "\"evidence_refs\": [\"code\"], \"prediction\"")), "a whole-item reference finds the covering range");

    // The agent's expected hash must agree with the one SE derived.
    const std::string sha = HwramCodeHash();
    Expect(V(With(sha, Fill('0'))), IssueCode::FingerprintMismatch, "a stale hash");
    Check(Has(Msg(V(With(sha, Fill('0')))), sha), "which names the hash SE derived");
    Expect(V(With(sha, sha.substr(0, 63))), IssueCode::BadValue, "a short hash");
    std::string upper = sha;
    for (char& c : upper) if (c >= 'a' && c <= 'f') c = static_cast<char>(c - 32);
    Expect(V(With(sha, upper)), IssueCode::BadValue, "an upper-case hash");
    // On an action that is not code-specific, the field has no meaning.
    Expect(V(With("\"id\": \"a1\", \"type\": \"add_address_label\"", "\"id\": \"a1\", \"expected_code_sha256\": \"" + sha + "\", \"type\": \"add_address_label\"")),
           IssueCode::BadValue, "an expected hash on a label");
    // And with no hash given, SE still derives it: nothing depends on the agent supplying one.
    const ValidationResult plain = V(With("\"expected_code_sha256\": \"" + sha + "\",", ""));
    ExpectOk(plain, "no expected hash");
    for (const ValidatedAction& a : plain.actions)
        if (a.id == "a5") Check(a.hasFingerprint && a.fingerprint.sha256 == sha, "the fingerprint is derived regardless");
}

// ---- 5. Targets ---------------------------------------------------------------------------------

void TestTargets()
{
    auto labelAt = [&](const std::string& target) {
        return V(With("\"target\": { \"bus_address\": \"0x06012340\" },\n      \"parameters\": { \"name\": \"GeneralHP\", \"size\"",
                      "\"target\": " + target + ",\n      \"parameters\": { \"name\": \"GeneralHP\", \"size\""));
    };
    ExpectOk(labelAt("{ \"bus_address\": \"0x26112340\" }"), "a cache-through mirror and the HWRAM repeat");
    Expect(labelAt("{ \"bus_address\": \"0x06012340\", \"region\": \"hwram\", \"offset\": \"0x12342\" }"), IssueCode::BadTarget, "bus_address and offset disagree", "target");
    Check(Has(Msg(labelAt("{ \"bus_address\": \"0x06012340\", \"region\": \"lwram\", \"offset\": \"0x12340\" }")), "hwram+0x12340"), "the message names both");
    Expect(labelAt("{ \"bus_address\": \"0x00300000\" }"), IssueCode::BadTarget, "an unmapped address");
    Expect(labelAt("{ \"bus_address\": \"0x66012340\" }"), IssueCode::BadTarget, "a cache address array address");
    Expect(labelAt("{ \"bus_address\": \"0x06012340\" , \"extra\": 1}"), IssueCode::BadTarget, "an unknown target field");
    Expect(labelAt("{ \"bus_address\": \"0x0601234a\" }"), IssueCode::BadTarget, "a lower-case hex digit");
    Expect(labelAt("{ \"bus_address\": \"06012340\" }"), IssueCode::BadTarget, "hex without the prefix");
    Expect(labelAt("{ \"bus_address\": 100740928 }"), IssueCode::BadTarget, "an address as a number");
    Expect(labelAt("{ \"region\": \"HWRAM\", \"offset\": \"0x12340\" }"), IssueCode::BadTarget, "a region in the wrong case");
    Expect(labelAt("{ \"region\": \"hwram\", \"offset\": \"0x100000\" }"), IssueCode::BadTarget, "an offset past the region");
    Expect(labelAt("{}"), IssueCode::BadTarget, "an empty target");
    Expect(labelAt("\"0x06012340\""), IssueCode::BadTarget, "a target that is a string");
    Expect(labelAt("{ \"bus_address\": \"0x05C80010\" }"), IssueCode::BadTarget, "a bus address in the frame buffer window");
    Expect(labelAt("{ \"region\": \"vdp1fb\", \"offset\": \"0x10\" }"), IssueCode::BadTarget, "a label on an app-derived image (no canonical address)");
    ExpectOk(labelAt("{ \"region\": \"vdp2cram\", \"offset\": \"0x0\" }") , "a label in colour RAM (the size 0x2 fits)");

    // Alignment: an instruction address is even.
    auto breakpointAt = [&](const char* addr) {
        return V(With("\"target\": { \"bus_address\": \"0x06004010\" },\n      \"parameters\": { \"condition\"", std::string("\"target\": { \"bus_address\": \"") + addr + "\" },\n      \"parameters\": { \"condition\""));
    };
    Expect(breakpointAt("0x06004011"), IssueCode::Misaligned, "an odd breakpoint address");
    Expect(V(With("\"bus_address\": \"0x06004012\" },\n      \"parameters\": { \"cpu\"", "\"bus_address\": \"0x06004013\" },\n      \"parameters\": { \"cpu\"")), IssueCode::Misaligned, "an odd tracepoint address");
    Expect(V(With("\"bus_address\": \"0x06004010\" },\n      \"parameters\": { \"name\": \"ApplyDamage\"", "\"bus_address\": \"0x06004011\" },\n      \"parameters\": { \"name\": \"ApplyDamage\"")), IssueCode::Misaligned, "an odd function entry");
    // A comment or label may sit on an odd address.
    ExpectOk(V(With("\"bus_address\": \"0x06004010\" },\n      \"parameters\": { \"text\"", "\"bus_address\": \"0x06004011\" },\n      \"parameters\": { \"text\"")), "an odd comment address");

    // Capabilities: code actions need a region that can hold SH-2 code.
    Expect(breakpointAt("0x05E00010"), IssueCode::MissingCapability, "a breakpoint in VDP2 RAM", "target");
    Check(Has(Msg(breakpointAt("0x05E00010")), "kCapSh2Code"), "naming the capability");
    Expect(V(With("\"bus_address\": \"0x06004012\" },\n      \"parameters\": { \"cpu\"", "\"bus_address\": \"0x05A00010\" },\n      \"parameters\": { \"cpu\"")), IssueCode::MissingCapability, "a tracepoint in sound RAM");
    // Evidence is matched by region: an LWRAM breakpoint is covered by the LWRAM code range, not the HWRAM one.
    const std::string lwramBp = With("\"evidence_refs\": [\"code/range-1\"],\n      \"expected_code_sha256\": \"" + HwramCodeHash() + "\",\n      \"target\": { \"bus_address\": \"0x06004010\" }",
                                    "\"evidence_refs\": [\"code/range-2\"],\n      \"expected_code_sha256\": \"" + LwramCodeHash() + "\",\n      \"target\": { \"bus_address\": \"0x00202010\" }");
    const ValidationResult lw = V(lwramBp);
    ExpectOk(lw, "an LWRAM breakpoint cited against the LWRAM code range");
    for (const ValidatedAction& a : lw.actions)
        if (a.id == "a5") Check(a.fingerprint.evidenceRef == "code/range-2" && a.fingerprint.sha256 == LwramCodeHash() && a.fingerprint.region == RegionId::Lwram,
                               "with that range's fingerprint");
    Expect(V(Replace(lwramBp, LwramCodeHash(), HwramCodeHash())), IssueCode::FingerprintMismatch, "and the HWRAM hash is stale for it");

    // The frame buffer cannot be watched, named or broken on (needsBus).
    Expect(V(With("\"target\": { \"region\": \"hwram\", \"offset\": \"0x12340\" },\n      \"parameters\": { \"kind\"", "\"target\": { \"region\": \"vdp1fb\", \"offset\": \"0x10\" },\n      \"parameters\": { \"kind\"")),
           IssueCode::BadTarget, "a memory watchpoint on the frame buffer image");
    Expect(V(With("\"target\": { \"bus_address\": \"0x06012340\" },\n      \"parameters\": { \"name\": \"GeneralHP\", \"data_type\"", "\"target\": { \"region\": \"vdp1fb\", \"offset\": \"0x10\" },\n      \"parameters\": { \"name\": \"GeneralHP\", \"data_type\"")),
           IssueCode::BadTarget, "a watch on the frame buffer image");

    // A request takes no target; a code request needs one.
    Expect(V(With("\"id\": \"a8\", \"type\": \"request_evidence\", \"hypothesis_id\": \"h1\",", "\"id\": \"a8\", \"type\": \"request_evidence\", \"hypothesis_id\": \"h1\", \"target\": {\"bus_address\": \"0x06012340\"},")),
           IssueCode::UnknownField, "a target on request_evidence", "target");
    const std::string codeRequest = "{ \"id\": \"a9\", \"type\": \"request_code_evidence\", \"hypothesis_id\": \"h2\", %s\"parameters\": { \"source\": \"live\", \"representation\": \"disassembly\", \"max_bytes\": \"0x200\", \"reason\": \"need the callers\" } }";
    char buf[1024];
    std::snprintf(buf, sizeof buf, codeRequest.c_str(), "\"target\": { \"bus_address\": \"0x06004010\" }, ");
    ExpectOk(V(With("\"actions\": [", std::string("\"actions\": [ ") + buf + ",")), "a code evidence request with a target");
    std::snprintf(buf, sizeof buf, codeRequest.c_str(), "");
    Expect(V(With("\"actions\": [", std::string("\"actions\": [ ") + buf + ",")), IssueCode::MissingField, "a code evidence request without one", "target");
}

// ---- 6. Parameters ------------------------------------------------------------------------------

void TestNamesAndComments()
{
    for (const char* bad : { "9bad", "a-b", "a b", "a.b", "", "\\u00e9t\\u00e9", "name\\n" })
        Expect(V(With("\"name\": \"ApplyDamage\"", std::string("\"name\": \"") + bad + "\"")), IssueCode::Grammar, (std::string("function name '") + bad + "'").c_str());
    Expect(V(With("\"name\": \"ApplyDamage\"", "\"name\": \"" + std::string(65, 'a') + "\"")), IssueCode::Grammar, "a 65-character name");
    ExpectOk(V(With("\"name\": \"ApplyDamage\"", "\"name\": \"" + std::string(64, 'a') + "\"")), "a 64-character name");
    ExpectOk(V(With("\"name\": \"ApplyDamage\"", "\"name\": \"_x9\"")), "a name starting with an underscore");
    Expect(V(With("\"name\": \"GeneralHP\", \"data_type\": \"u16\" }", "\"name\": \"General HP\", \"data_type\": \"u16\" }")), IssueCode::Grammar, "a watch name with a space");
    Expect(V(With("\"name\": \"ApplyDamage\"", "\"name\": 7")), IssueCode::WrongType, "a numeric name");

    // Comments: one line, at most 256 characters, no control characters.
    auto comment = [&](const std::string& text) { return V(With("\"text\": \"Subtracts damage from HP\"", "\"text\": \"" + text + "\"")); };
    ExpectOk(comment(std::string(256, 'x')), "256 characters");
    Expect(comment(std::string(257, 'x')), IssueCode::Grammar, "257 characters");
    std::string wide;
    for (int i = 0; i < 256; ++i) wide += "\xC3\xA9";
    ExpectOk(comment(wide), "256 two-byte characters (limit counts characters)");
    Expect(comment(wide + "\xC3\xA9"), IssueCode::Grammar, "257 two-byte characters");
    Expect(comment("two\\nlines"), IssueCode::Grammar, "a newline");
    Expect(comment("tab\\there"), IssueCode::Grammar, "a tab");
    Expect(comment("bell\\u0007"), IssueCode::Grammar, "a control character");
    Expect(comment("sep\\u2028x"), IssueCode::Grammar, "a line separator");
    Expect(comment(""), IssueCode::BadValue, "an empty comment");
    ExpectOk(comment("\xE3\x83\x80\xE3\x83\xA1\xE3\x83\xBC\xE3\x82\xB8"), "Japanese text");
}

void TestParameterRules()
{
    // Unsupported CPU, condition and effect.
    Expect(V(With("\"cpu\": 0", "\"cpu\": 2")), IssueCode::BadValue, "cpu 2");
    Expect(V(With("\"cpu\": 0", "\"cpu\": \"master\"")), IssueCode::WrongType, "cpu as a word");
    Expect(V(With("\"cpu\": 0", "\"cpu\": -1")), IssueCode::WrongType, "cpu -1");
    Expect(V(With("\"cpu\": 0", "\"cpu\": 18446744073709551616")), IssueCode::NotJson, "a cpu past 2^64");
    Expect(V(With("\"cpu\": 0", "\"cpu\": 0.5")), IssueCode::NotJson, "a fractional cpu");
    ExpectOk(V(With("\"cpu\": 0", "\"cpu\": 1")), "cpu 1 (the slave)");
    ValidationResult r = V(With("\"condition\": \"r4 == 5\"", "\"condition\": \"r4 == 5\", \"cpu\": 1"));
    Expect(r, IssueCode::RejectedParameter, "cpu on an execution breakpoint");
    Check(Has(Msg(r), "whichever SH-2"), "with the reason");
    Expect(V(With("\"kind\": \"write\"", "\"kind\": \"write\", \"cpu\": 0")), IssueCode::RejectedParameter, "cpu on a memory watchpoint");
    r = V(With("\"kind\": \"write\"", "\"kind\": \"write\", \"condition\": \"r0 == 1\""));
    Expect(r, IssueCode::RejectedParameter, "a condition on a memory watchpoint");
    Check(Has(Msg(r), "no condition"), "with the reason");
    r = V(With("\"cpu\": 0, \"format\"", "\"cpu\": 0, \"effects\": {\"pauseEmulator\": true}, \"format\""));
    Expect(r, IssueCode::RejectedParameter, "a tracepoint effect");
    Check(Has(Msg(r), "only write to the log"), "with the reason");
    Expect(V(With("\"cpu\": 0, \"format\"", "\"cpu\": 0, \"script\": \"x\", \"format\"")), IssueCode::RejectedParameter, "a script");
    r = V(With("\"name\": \"GeneralHP\", \"data_type\": \"u16\" }", "\"name\": \"GeneralHP\", \"data_type\": \"u16\", \"byte_order\": \"little\" }"));
    Expect(r, IssueCode::RejectedParameter, "a watch byte order");
    Check(Has(Msg(r), "big-endian"), "watches are always big-endian");
    Expect(V(With("\"condition\": \"r4 == 5\"", "\"condition\": \"r4 == 5\", \"surprise\": 1")), IssueCode::UnknownField, "an unknown parameter", "parameters.surprise");

    // Conditions and formats parse, under a length bound.
    Expect(V(With("\"r4 == 5\"", "\"r4 ==\"")), IssueCode::Unparseable, "a condition that does not parse");
    Expect(V(With("\"r4 == 5\"", "\"r4 == 5 &&& 1\"")), IssueCode::Unparseable, "a malformed operator");
    Expect(V(With("\"r4 == 5\"", "\"\"")), IssueCode::BadValue, "an empty condition");
    Expect(V(With("\"r4 == 5\"", "\"" + std::string(257, '1') + "\"")), IssueCode::Grammar, "a 257-character condition");
    Expect(V(With("\"r4 == 5\"", "\"" + std::string(256, '(') + "\"")), IssueCode::Unparseable, "256 open parentheses terminate quickly with an error");
    Expect(V(With("\"HP = {*0x6012340:u16}\"", "\"HP = {\"")), IssueCode::Unparseable, "an unterminated format token");
    Expect(V(With("\"HP = {*0x6012340:u16}\"", "\"\"")), IssueCode::BadValue, "an empty format");
    Expect(V(With("\"HP = {*0x6012340:u16}\"", "\"" + std::string(257, 'x') + "\"")), IssueCode::Grammar, "a 257-character format");
    ExpectOk(V(With("\"HP = {*0x6012340:u16}\"", "\"" + std::string(256, 'x') + "\"")), "a 256-character format");
    ExpectOk(V(With("\"HP = {*0x6012340:u16}\"", "\"pc={pc:X8} r4={r4} next {{literal}}\"")), "ordinary format tokens");
    Expect(V(With("\"HP = {*0x6012340:u16}\"", "\"line\\nbreak\"")), IssueCode::Grammar, "a multi-line format");

    // Repeat policy.
    Expect(V(With("\"cpu\": 0, \"format\": \"HP = {*0x6012340:u16}\"", "\"cpu\": 0, \"format\": \"x\", \"repeat\": \"every_n\"")), IssueCode::MissingField, "every_n needs repeat_n");
    Expect(V(With("\"cpu\": 0, \"format\": \"HP = {*0x6012340:u16}\"", "\"cpu\": 0, \"format\": \"x\", \"repeat\": \"once\", \"repeat_n\": 5")), IssueCode::BadValue, "repeat_n with once");
    Expect(V(With("\"cpu\": 0, \"format\": \"HP = {*0x6012340:u16}\"", "\"cpu\": 0, \"format\": \"x\", \"repeat_n\": 5")), IssueCode::BadValue, "repeat_n without a repeat");
    ExpectOk(V(With("\"cpu\": 0, \"format\": \"HP = {*0x6012340:u16}\"", "\"cpu\": 0, \"format\": \"x\", \"repeat\": \"every_n\", \"repeat_n\": 5")), "every_n with repeat_n");
    Expect(V(With("\"cpu\": 0, \"format\": \"HP = {*0x6012340:u16}\"", "\"cpu\": 0, \"format\": \"x\", \"repeat\": \"every_n\", \"repeat_n\": 1")), IssueCode::BadValue, "repeat_n 1 is every time");
    Expect(V(With("\"cpu\": 0, \"format\": \"HP = {*0x6012340:u16}\"", "\"cpu\": 0, \"format\": \"x\", \"repeat\": \"every_n\", \"repeat_n\": 16777216")), IssueCode::BadValue, "repeat_n past the wire limit");
    Expect(V(With("\"cpu\": 0, \"format\": \"HP = {*0x6012340:u16}\"", "\"cpu\": 0, \"format\": \"x\", \"repeat\": \"sometimes\"")), IssueCode::BadValue, "an unknown repeat");

    // Labels: size, type and count agree and fit.
    auto label = [&](const std::string& params) {
        return V(With("\"parameters\": { \"name\": \"GeneralHP\", \"size\": \"0x2\", \"data_type\": \"u16\" }", "\"parameters\": { " + params + " }"));
    };
    ExpectOk(label("\"name\": \"A\", \"size\": \"0x2\", \"data_type\": \"u16\""), "size agrees with the type");
    Expect(label("\"name\": \"A\", \"size\": \"0x4\", \"data_type\": \"u16\""), IssueCode::BadValue, "size disagrees with the type");
    ExpectOk(label("\"name\": \"A\", \"size\": \"0x8\", \"data_type\": \"u16\", \"count\": 4"), "an array of four halfwords");
    Expect(label("\"name\": \"A\", \"size\": \"0x6\", \"data_type\": \"u16\", \"count\": 4"), IssueCode::BadValue, "an array of the wrong size");
    Expect(label("\"name\": \"A\", \"count\": 4"), IssueCode::BadValue, "count without a type");
    ExpectOk(label("\"name\": \"A\", \"data_type\": \"s32\", \"count\": 4096"), "the largest array");
    Expect(label("\"name\": \"A\", \"data_type\": \"s32\", \"count\": 4097"), IssueCode::BadValue, "an array past the limit");
    Expect(label("\"name\": \"A\", \"data_type\": \"s32\", \"count\": 0"), IssueCode::BadValue, "a zero count");
    ExpectOk(label("\"name\": \"A\""), "a bare name");
    ExpectOk(label("\"name\": \"A\", \"size\": \"0x40\""), "a size with no type");
    Expect(label("\"name\": \"A\", \"size\": \"0x0\""), IssueCode::BadValue, "size zero");
    Expect(label("\"name\": \"A\", \"size\": \"0x100001\""), IssueCode::BadValue, "size past the largest region");
    Expect(label("\"name\": \"A\", \"size\": \"2\""), IssueCode::BadValue, "size without the 0x prefix");
    Expect(label("\"name\": \"A\", \"size\": \"0x2a\""), IssueCode::BadValue, "size in lower-case hex");
    Expect(label("\"name\": \"A\", \"size\": 2"), IssueCode::WrongType, "size as a number");
    Expect(label("\"name\": \"A\", \"data_type\": \"U16\""), IssueCode::BadValue, "an upper-case type");
    Expect(label("\"name\": \"A\", \"data_type\": \"float\""), IssueCode::BadValue, "an unknown type");
    ExpectOk(label("\"name\": \"A\", \"note\": \"hit points\""), "a note");
    Expect(label("\"name\": \"A\", \"note\": \"" + std::string(257, 'n') + "\""), IssueCode::Grammar, "a long note");
    Expect(label("\"size\": \"0x2\""), IssueCode::MissingField, "a label needs a name", "parameters.name");
    // The labelled range must fit the region.
    Expect(V(With("\"bus_address\": \"0x06012340\" },\n      \"parameters\": { \"name\": \"GeneralHP\", \"size\"", "\"bus_address\": \"0x060FFFFF\" },\n      \"parameters\": { \"name\": \"GeneralHP\", \"size\"")),
           IssueCode::RangeOutOfRegion, "a halfword label on the last byte of HWRAM");

    // Watches: type vocabulary and the bytes read.
    for (const char* t : { "u8", "s8", "u16", "s16", "u32", "s32", "rgb555", "pointer" })
        ExpectOk(V(With("\"name\": \"GeneralHP\", \"data_type\": \"u16\" }", std::string("\"name\": \"GeneralHP\", \"data_type\": \"") + t + "\" }")), (std::string("watch type ") + t).c_str());
    Expect(V(With("\"name\": \"GeneralHP\", \"data_type\": \"u16\" }", "\"name\": \"GeneralHP\", \"data_type\": \"U16\" }")), IssueCode::BadValue, "watch type in upper case");
    Expect(V(With("\"name\": \"GeneralHP\", \"data_type\": \"u16\" }", "\"name\": \"GeneralHP\" }")), IssueCode::MissingField, "a watch needs a type");
    Expect(V(With("\"name\": \"GeneralHP\", \"data_type\": \"u16\" }", "\"name\": \"GeneralHP\", \"data_type\": \"u32\", \"enabled\": \"yes\" }")), IssueCode::WrongType, "enabled is a boolean");
    ExpectOk(V(With("\"name\": \"GeneralHP\", \"data_type\": \"u16\" }", "\"name\": \"GeneralHP\", \"data_type\": \"u32\", \"enabled\": false }")), "enabled false");
    Expect(V(With("\"target\": { \"bus_address\": \"0x06012340\" },\n      \"parameters\": { \"name\": \"GeneralHP\", \"data_type\": \"u16\" } },\n    { \"id\": \"a3\"", "\"target\": { \"bus_address\": \"0x060FFFFF\" },\n      \"parameters\": { \"name\": \"GeneralHP\", \"data_type\": \"u16\" } },\n    { \"id\": \"a3\"")),
           IssueCode::RangeOutOfRegion, "a halfword watch on the last byte");

    // Memory watchpoints.
    Expect(V(With("\"size\": \"0x2\" } },\n    { \"id\": \"a4\"", "\"size\": \"0x3\" } },\n    { \"id\": \"a4\"")), IssueCode::BadValue, "watch size 3");
    Expect(V(With("\"size\": \"0x2\" } },\n    { \"id\": \"a4\"", "\"size\": \"0x8\" } },\n    { \"id\": \"a4\"")), IssueCode::BadValue, "watch size 8");
    Expect(V(With("\"size\": \"0x2\" } },\n    { \"id\": \"a4\"", "\"size\": 2 } },\n    { \"id\": \"a4\"")), IssueCode::WrongType, "watch size as a number");
    for (const char* k : { "read", "write", "read_write" })
        ExpectOk(V(With("\"kind\": \"write\"", std::string("\"kind\": \"") + k + "\"")), (std::string("kind ") + k).c_str());
    Expect(V(With("\"kind\": \"write\"", "\"kind\": \"rw\"")), IssueCode::BadValue, "an unknown kind");
    Expect(V(With("\"kind\": \"write\", \"size\": \"0x2\"", "\"size\": \"0x2\"")), IssueCode::MissingField, "a watchpoint needs a kind");
}

void TestRequests()
{
    auto request = [&](const std::string& params) {
        return V(With("\"parameters\": { \"source\": \"A\", \"ranges\": [ { \"region\": \"hwram\", \"offset\": \"0x12300\", \"length\": \"0x80\" } ],\n                      \"representation\": \"binary\", \"max_bytes\": \"0x100\", \"reason\": \"see the structure around HP\" }",
                      "\"parameters\": { " + params + " }"));
    };
    const std::string range = "\"ranges\": [ { \"region\": \"hwram\", \"offset\": \"0x12300\", \"length\": \"0x80\" } ]";
    const std::string rest = "\"representation\": \"binary\", \"max_bytes\": \"0x100\", \"reason\": \"r\"";
    ExpectOk(request("\"source\": \"A\", " + range + ", " + rest), "a plain request");
    ExpectOk(request("\"source\": \"B\", " + range + ", " + rest), "a request to the other source");
    Expect(request("\"source\": \"Z\", " + range + ", " + rest), IssueCode::UnknownSource, "a source the package does not have");
    Expect(request("\"source\": \"A\", \"ranges\": [ { \"region\": \"soundram\", \"offset\": \"0x0\", \"length\": \"0x10\" } ], " + rest), IssueCode::UnknownSource, "a region a compare snapshot does not hold");
    ExpectOk(request("\"source\": \"Z\", \"evidence_id\": \"" + std::string(kOtherEvidence) + "\", " + range + ", " + rest), "a source in an earlier package is resolved when that package is loaded");
    ExpectOk(request("\"source\": \"A\", \"evidence_id\": \"" + std::string(kEvidence) + "\", " + range + ", " + rest), "the package's own id");
    Expect(request("\"source\": \"A\", \"evidence_id\": \"nope\", " + range + ", " + rest), IssueCode::BadValue, "a malformed evidence_id");
    Expect(request("\"source\": \"A\", \"ranges\": [], " + rest), IssueCode::LimitExceeded, "no ranges");
    Expect(request("\"source\": \"A\", \"ranges\": [ { \"region\": \"hwram\", \"offset\": \"0xFFFF0\", \"length\": \"0x80\" } ], " + rest), IssueCode::RangeOutOfRegion, "a range past the end of its region");
    Expect(request("\"source\": \"A\", \"ranges\": [ { \"region\": \"hwram\", \"offset\": \"0x0\", \"length\": \"0x0\" } ], " + rest), IssueCode::RangeOutOfRegion, "a zero-length range");
    Expect(request("\"source\": \"A\", \"ranges\": [ { \"region\": \"hwram\", \"offset\": \"0x0\" } ], " + rest), IssueCode::MissingField, "a range without a length");
    Expect(request("\"source\": \"A\", \"ranges\": [ { \"region\": \"hwram\", \"offset\": \"0x0\", \"length\": \"0x10\", \"x\": 1 } ], " + rest), IssueCode::UnknownField, "a range with an extra member");
    Expect(request("\"source\": \"A\", \"ranges\": [ { \"region\": \"mars\", \"offset\": \"0x0\", \"length\": \"0x10\" } ], " + rest), IssueCode::BadTarget, "an unknown region in a range");
    Expect(request("\"source\": \"A\", " + range + ", \"representation\": \"binary\", \"max_bytes\": \"0x10\", \"reason\": \"r\""), IssueCode::BadValue, "the ranges total more than max_bytes");
    Expect(request("\"source\": \"A\", " + range + ", \"representation\": \"pdf\", \"max_bytes\": \"0x100\", \"reason\": \"r\""), IssueCode::BadValue, "an unknown representation");
    Expect(request("\"source\": \"A\", " + range + ", \"representation\": \"binary\", \"max_bytes\": \"0x100\""), IssueCode::MissingField, "a request needs a reason");
    Expect(request("\"source\": \"A\", " + range + ", \"representation\": \"binary\", \"max_bytes\": \"0x0\", \"reason\": \"r\""), IssueCode::BadValue, "max_bytes zero");
    Expect(request("\"source\": \"A\", " + range + ", \"representation\": \"binary\", \"max_bytes\": \"0x1000001\", \"reason\": \"r\""), IssueCode::BadValue, "max_bytes past the cap");
    std::string many = "[";
    for (int i = 0; i < 65; ++i) many += std::string(i ? "," : "") + "{ \"region\": \"hwram\", \"offset\": \"0x0\", \"length\": \"0x1\" }";
    Expect(request("\"source\": \"A\", \"ranges\": " + many + "], " + rest), IssueCode::LimitExceeded, "65 ranges");
}

// ---- 7. Whole-response rules --------------------------------------------------------------------

void TestDependencies()
{
    Expect(V(With("\"depends_on\": [\"a1\"],\n      \"target\": { \"bus_address\": \"0x06012340\" },\n      \"parameters\": { \"name\": \"GeneralHP\", \"data_type\"", "\"depends_on\": [\"a9\"],\n      \"target\": { \"bus_address\": \"0x06012340\" },\n      \"parameters\": { \"name\": \"GeneralHP\", \"data_type\"")),
           IssueCode::UnknownDependency, "a dependency on a missing action");
    Expect(V(With("\"depends_on\": [\"a1\"],\n      \"target\": { \"bus_address\": \"0x06012340\" },\n      \"parameters\": { \"name\": \"GeneralHP\", \"data_type\"", "\"depends_on\": [\"a2\"],\n      \"target\": { \"bus_address\": \"0x06012340\" },\n      \"parameters\": { \"name\": \"GeneralHP\", \"data_type\"")),
           IssueCode::DependencyCycle, "an action that depends on itself");
    // a1 -> a2 -> a1
    ValidationResult r = V(With("\"id\": \"a1\", \"type\": \"add_address_label\", \"hypothesis_id\": \"h1\",", "\"id\": \"a1\", \"type\": \"add_address_label\", \"hypothesis_id\": \"h1\", \"depends_on\": [\"a2\"],"));
    Expect(r, IssueCode::DependencyCycle, "a two-action cycle");
    Check(r.applyOrder.empty(), "which leaves no apply order");
    // A chain written out of order is sorted.
    r = V(With("\"id\": \"a1\", \"type\": \"add_address_label\", \"hypothesis_id\": \"h1\",", "\"id\": \"a1\", \"type\": \"add_address_label\", \"hypothesis_id\": \"h1\", \"depends_on\": [\"a8\"],"));
    ExpectOk(r, "a1 after a8");
    Check(r.applyOrder.size() == 8 && r.applyOrder[0] == "a4" && r.applyOrder.back() != "a8", "a dependency runs first even when written last");
    {
        size_t i1 = 0, i8 = 0;
        for (size_t i = 0; i < r.applyOrder.size(); ++i) { if (r.applyOrder[i] == "a1") i1 = i; if (r.applyOrder[i] == "a8") i8 = i; }
        Check(i8 < i1, "a8 before a1");
    }
    Expect(V(With("\"depends_on\": [\"a1\"],\n      \"target\": { \"region\"", "\"depends_on\": [\"a1\", \"a1\"],\n      \"target\": { \"region\"")), IssueCode::DuplicateId, "a dependency listed twice");
    ValidateOptions o;
    o.limits.maxDependsOn = 0;
    Expect(Validate(ValidResponse(), o), IssueCode::LimitExceeded, "too many dependencies");
}

std::string TracepointAction(int i)
{
    char buf[512];
    std::snprintf(buf, sizeof buf,
                  "{ \"id\": \"t%d\", \"type\": \"add_log_tracepoint\", \"hypothesis_id\": \"h2\", \"evidence_refs\": [\"big\"], "
                  "\"target\": { \"region\": \"hwram\", \"offset\": \"0x%X\" }, \"parameters\": { \"cpu\": 0, \"format\": \"x\" } }",
                  i, 0x100 + i * 2);
    return buf;
}

Manifest ManifestWithBigCode()
{
    Manifest m = MakeManifest();
    ManifestItem big;
    big.id = "big"; big.kind = "code_range"; big.title = "a lot of code"; big.sources = { "live" };
    ManifestRange r; r.id = "r"; r.sources = { "live" }; r.region = RegionId::Hwram; r.offset = 0; r.length = 0x10000; r.kind = RangeKind::Code; r.sha256 = Fill('7');
    big.ranges = { r };
    m.items.push_back(big);
    return m;
}

std::string ResponseWithTracepoints(int n)
{
    std::string actions;
    for (int i = 0; i < n; ++i) actions += (i ? ",\n" : "") + TracepointAction(i);
    return "{ \"schema_version\": 1, \"investigation_id\": \"" + std::string(kInvestigation) + "\", \"evidence_id\": \"" + kEvidence +
           "\", \"response_id\": \"" + kResponse + "\", \"hypotheses\": [ { \"id\": \"h2\", \"claim\": \"c\", \"confidence\": \"low\", "
           "\"evidence_refs\": [], \"prediction\": \"p\" } ], \"actions\": [" + actions + "] }";
}

void TestTracepointLimit()
{
    ValidateOptions o;
    o.limits.maxActions = 1000;
    o.limits.maxArrayLength = 1000;
    ValidationResult r = Validate(ResponseWithTracepoints(256), o, ManifestWithBigCode());
    ExpectOk(r, "exactly 256 tracepoints");
    r = Validate(ResponseWithTracepoints(257), o, ManifestWithBigCode());
    Expect(r, IssueCode::TooManyTracepoints, "257 tracepoints");
    int tooMany = 0;
    for (const ValidationIssue& i : r.issues) tooMany += i.code == IssueCode::TooManyTracepoints ? 1 : 0;
    Check(tooMany == 1 && r.issues.size() == 1, "reported once, against the first one over");
    // The default action limit is 256 as well, so 257 tracepoints under defaults trips that first.
    r = Validate(ResponseWithTracepoints(257), ValidateOptions(), ManifestWithBigCode());
    Expect(r, IssueCode::LimitExceeded, "257 actions under the default limit");
    Check(kMaxLogTracepoints == 256, "the cap is the protocol's 256");
}

void TestDuplicates()
{
    // The same item twice, spelled as a mirror and as the canonical address.
    const std::string twoWatches = With("\"id\": \"a2\", \"type\": \"add_watch\", \"hypothesis_id\": \"h1\", \"depends_on\": [\"a1\"],\n      \"target\": { \"bus_address\": \"0x06012340\" },\n      \"parameters\": { \"name\": \"GeneralHP\", \"data_type\": \"u16\" } },",
                                        "\"id\": \"a2\", \"type\": \"add_watch\", \"hypothesis_id\": \"h1\", \"depends_on\": [\"a1\"],\n      \"target\": { \"bus_address\": \"0x06012340\" },\n      \"parameters\": { \"name\": \"GeneralHP\", \"data_type\": \"u16\" } },\n    { \"id\": \"a2b\", \"type\": \"add_watch\", \"hypothesis_id\": \"h1\",\n      \"target\": { \"bus_address\": \"0x26112340\" },\n      \"parameters\": { \"name\": \"Other\", \"data_type\": \"u16\" } },");
    ValidationResult r = V(twoWatches);
    Expect(r, IssueCode::DuplicateSemanticKey, "the same watch spelled as a mirror and as the canonical address");
    Check(Has(Msg(r), "a2b") && Has(Msg(r), "a2"), "naming both actions");
    // A different type at the same address is a different item.
    ExpectOk(V(Replace(twoWatches, "\"name\": \"Other\", \"data_type\": \"u16\"", "\"name\": \"Other\", \"data_type\": \"u8\"")), "a u8 and a u16 watch at one address");
    // Two labels with one name, at different addresses.
    r = V(With("\"actions\": [", "\"actions\": [ { \"id\": \"l2\", \"type\": \"add_address_label\", \"hypothesis_id\": \"h1\", \"target\": { \"bus_address\": \"0x06012350\" }, \"parameters\": { \"name\": \"GeneralHP\" } },"));
    Expect(r, IssueCode::DuplicateLabelName, "two labels with the same name");
    // Memory watchpoints differ by size or kind.
    const std::string second = "{ \"id\": \"w2\", \"type\": \"add_memory_watchpoint\", \"hypothesis_id\": \"h1\", \"target\": { \"bus_address\": \"0x06012340\" }, \"parameters\": { \"kind\": \"%s\", \"size\": \"%s\" } }";
    char buf[512];
    std::snprintf(buf, sizeof buf, second.c_str(), "write", "0x2");
    Expect(V(With("\"actions\": [", std::string("\"actions\": [ ") + buf + ",")), IssueCode::DuplicateSemanticKey, "the same watchpoint twice");
    std::snprintf(buf, sizeof buf, second.c_str(), "write", "0x4");
    ExpectOk(V(With("\"actions\": [", std::string("\"actions\": [ ") + buf + ",")), "a different size");
    std::snprintf(buf, sizeof buf, second.c_str(), "read", "0x2");
    ExpectOk(V(With("\"actions\": [", std::string("\"actions\": [ ") + buf + ",")), "a different kind");
    // Tracepoints are keyed by CPU as well as address.
    const std::string tp = "{ \"id\": \"t2\", \"type\": \"add_log_tracepoint\", \"hypothesis_id\": \"h2\", \"target\": { \"bus_address\": \"0x06004012\" }, \"evidence_refs\": [\"code\"], \"parameters\": { \"cpu\": %d, \"format\": \"x\" } }";
    std::snprintf(buf, sizeof buf, tp.c_str(), 1);
    ExpectOk(V(With("\"actions\": [", std::string("\"actions\": [ ") + buf + ",")), "the same address on the other CPU");
    std::snprintf(buf, sizeof buf, tp.c_str(), 0);
    Expect(V(With("\"actions\": [", std::string("\"actions\": [ ") + buf + ",")), IssueCode::DuplicateSemanticKey, "the same CPU and address");
}

// ---- 8. Identity and the canonical payload hash --------------------------------------------------

void TestPayloadIdentity()
{
    const ValidationResult r = V(ValidResponse());
    JsonValue doc;
    ParseJson(ValidResponse(), JsonLimits(), &doc, nullptr);
    const JsonValue& a1 = doc.Find("actions")->items()[0];
    const std::string canonical = ToCanonicalJson(a1);
    Check(r.actions[0].payloadSha256 == sfe::Sha256Hex(canonical.data(), canonical.size()),
          "payload_sha256 is the SHA-256 of the canonical re-serialisation of the action object");
    Check(Has(canonical, "{\"hypothesis_id\":\"h1\",\"id\":\"a1\",\"parameters\":{\"data_type\":\"u16\",\"name\":\"GeneralHP\",\"size\":\"0x2\"},"),
          "sorted keys, no insignificant whitespace");

    // Spelling does not change it: key order, spacing.
    const std::string reordered = With("{ \"id\": \"a1\", \"type\": \"add_address_label\", \"hypothesis_id\": \"h1\",\n      \"target\": { \"bus_address\": \"0x06012340\" },\n      \"parameters\": { \"name\": \"GeneralHP\", \"size\": \"0x2\", \"data_type\": \"u16\" } }",
                                       "{\"parameters\":{\"data_type\":\"u16\",\"size\":\"0x2\",\"name\":\"GeneralHP\"},\"target\":{\"bus_address\":\"0x06012340\"},\"hypothesis_id\":\"h1\",\"type\":\"add_address_label\",\"id\":\"a1\"}");
    const ValidationResult r2 = V(reordered);
    ExpectOk(r2, "the reordered action");
    Check(r2.actions[0].payloadSha256 == r.actions[0].payloadSha256, "the same payload hash");
    // Content does.
    const ValidationResult r3 = V(With("\"name\": \"GeneralHP\", \"size\"", "\"name\": \"GeneralHp\", \"size\""));
    Check(r3.actions[0].payloadSha256 != r.actions[0].payloadSha256, "a changed parameter changes it");
    // And a mirror address is a different payload even though it is the same target: identity is the text SE was sent.
    const ValidationResult r4 = V(With("\"target\": { \"bus_address\": \"0x06012340\" },\n      \"parameters\": { \"name\": \"GeneralHP\", \"size\"", "\"target\": { \"bus_address\": \"0x26012340\" },\n      \"parameters\": { \"name\": \"GeneralHP\", \"size\""));
    Check(r4.ok && r4.actions[0].payloadSha256 != r.actions[0].payloadSha256 && r4.actions[0].target == r.actions[0].target,
          "a different spelling of one target is a different payload, with the same canonical target");

    // (investigation, response, action) + payload: a re-import and a conflict.
    const ActionIdentity first = IdentityOf(r, r.actions[0]);
    Check(CompareIdentity(first, IdentityOf(V(ValidResponse()), V(ValidResponse()).actions[0])) == IdentityRelation::SamePayload, "a re-import is the same payload");
    Check(CompareIdentity(first, IdentityOf(r3, r3.actions[0])) == IdentityRelation::ConflictingPayload, "the same ids with another payload conflict");
    Check(CompareIdentity(first, IdentityOf(r, r.actions[1])) == IdentityRelation::Different, "another action id is a different action");
    ActionIdentity otherResponse = first;
    otherResponse.responseId = "11111111-1111-4111-8111-111111111111";
    otherResponse.payloadSha256 = Fill('1');
    Check(CompareIdentity(first, otherResponse) == IdentityRelation::Different, "another response is a different action even with another payload");
    ActionIdentity otherInvestigation = first;
    otherInvestigation.investigationId = "11111111-1111-4111-8111-111111111111";
    Check(CompareIdentity(first, otherInvestigation) == IdentityRelation::Different, "another investigation too");
}

// ---- 9. Limits at the JSON level ----------------------------------------------------------------

void TestJsonLimits()
{
    Expect(V("{\"schema_version\": 1,"), IssueCode::NotJson, "truncated JSON");
    Expect(V("{\"schema_version\": tru}"), IssueCode::NotJson, "a bad literal");
    ValidateOptions o;
    o.limits.maxBytes = 100;
    Expect(Validate(ValidResponse(), o), IssueCode::NotJson, "a response over the byte limit");
    o = ValidateOptions();
    o.limits.maxDepth = 3;
    Expect(Validate(ValidResponse(), o), IssueCode::NotJson, "a response deeper than the limit");
    o = ValidateOptions();
    o.limits.maxStringBytes = 20;
    Expect(Validate(ValidResponse(), o), IssueCode::NotJson, "a string over the length limit");
    o = ValidateOptions();
    o.limits.maxArrayLength = 2;
    Expect(Validate(ValidResponse(), o), IssueCode::NotJson, "an array over the length limit");
    Expect(V(std::string("{\"a\":") + std::string(100000, '[')), IssueCode::NotJson, "a depth bomb");
    // The default limits accept a response of the maximum size.
    ValidateOptions big;
    big.limits.maxActions = 1000;
    big.limits.maxArrayLength = 1000;
    Check(Validate(ResponseWithTracepoints(256), big, ManifestWithBigCode()).ok, "256 actions are well inside the default byte limit");
}

// ---- 10. The preview ----------------------------------------------------------------------------

void TestPreview()
{
    const ValidationResult r = V(ValidResponse());
    const ResponsePreview p = BuildPreview(r);
    Check(p.ok && p.rows.size() == 8 && p.hypotheses.size() == 2 && p.topLevelProblems.empty(), "a preview of the valid response");
    Check(p.responseId == kResponse && !p.rebound, "carries the ids");
    Check(p.rows[0].actionId == "a1" && p.rows[1].actionId == "a2", "rows follow the apply order");
    const PreviewRow& label = p.rows[0];
    Check(label.valid && Has(label.summary, "GeneralHP") && Has(label.summary, "hwram+0x12340") && label.target == "hwram+0x12340 (0x06012340)",
          "a readable summary and the canonical target");
    Check(label.adapter == "AddressSymbols::Add" && !label.adapterExists && label.adapterMilestone == 2 && label.implementedIn == 3,
          "the label's adapter is planned in M2");
    const PreviewRow* watch = nullptr;
    const PreviewRow* rename = nullptr;
    const PreviewRow* trace = nullptr;
    const PreviewRow* request = nullptr;
    for (const PreviewRow& row : p.rows)
    {
        if (row.actionId == "a2") watch = &row;
        if (row.actionId == "a4") rename = &row;
        if (row.actionId == "a6") trace = &row;
        if (row.actionId == "a8") request = &row;
    }
    Check(watch && watch->adapter == "WatchList::Add" && watch->adapterExists && watch->adapterMilestone == 0, "the watch adapter exists today");
    Check(rename && rename->codeSpecific && rename->fingerprint == HwramCodeHash() && Has(rename->fingerprintCovers, "hwram+0x4000 length 0x40 from code/range-1"),
          "a code-specific row shows the fingerprint and what it covers");
    Check(trace && !trace->warnings.empty() && Has(trace->summary, "master"), "a tracepoint row carries its warning");
    Check(request && request->target.empty() && !request->adapterExists && request->adapterMilestone == 3, "a request row");
    Check(watch->dependsOn.size() == 1 && watch->dependsOn[0] == "a1", "dependencies are shown");

    // An invalid response previews with the problems beside the actions they concern.
    const ValidationResult bad = V(With("\"name\": \"ApplyDamage\"", "\"name\": \"9bad\""));
    const ResponsePreview pb = BuildPreview(bad);
    Check(!pb.ok && pb.rows.size() == 8, "an invalid response still previews");
    int withProblems = 0;
    for (const PreviewRow& row : pb.rows) if (!row.problems.empty()) { ++withProblems; Check(row.actionId == "a4" && !row.valid, "the problem is on the right row"); }
    Check(withProblems == 1, "one row carries problems");
    // A response that stopped at the id check has only top-level problems.
    const ResponsePreview stopped = BuildPreview(V(With("9b07e1c4-1a2b-4c3d-8e4f-5a6b7c8d9e0f", kOtherEvidence)));
    Check(!stopped.ok && stopped.rows.empty() && stopped.topLevelProblems.size() == 1, "an id mismatch previews as a top-level problem");
    // Building a preview does not change the result it was built from.
    ValidationResult copy = r;
    BuildPreview(copy);
    Check(copy.ok == r.ok && copy.actions.size() == r.actions.size() && copy.applyOrder == r.applyOrder && copy.issues.size() == r.issues.size(),
          "BuildPreview leaves its input as it was");
}

void TestDeterminism()
{
    const Manifest m = MakeManifest();
    const std::string before = WriteJson(ManifestToJson(m), JsonWriteOptions());
    const ValidationResult a = ValidateResponse(ValidResponse(), m, ValidateOptions());
    const ValidationResult b = ValidateResponse(ValidResponse(), m, ValidateOptions());
    Check(a.ok && b.ok && a.applyOrder == b.applyOrder && a.actions.size() == b.actions.size(), "validating twice gives one answer");
    for (size_t i = 0; i < a.actions.size(); ++i)
        Check(a.actions[i].payloadSha256 == b.actions[i].payloadSha256 && a.actions[i].semanticKey == b.actions[i].semanticKey, "and the same hashes and keys");
    Check(WriteJson(ManifestToJson(m), JsonWriteOptions()) == before, "the manifest is not modified");
}
}  // namespace

int main()
{
    TestExtraction();
    TestValid();
    TestTopLevel();
    TestHypotheses();
    TestActionsShape();
    TestExtensions();
    TestEvidenceIds();
    TestEvidenceRefs();
    TestCodeSpecific();
    TestTargets();
    TestNamesAndComments();
    TestParameterRules();
    TestRequests();
    TestDependencies();
    TestTracepointLimit();
    TestDuplicates();
    TestPayloadIdentity();
    TestJsonLimits();
    TestPreview();
    TestDeterminism();
    if (gFail == 0) std::printf("ResponseValidatorTests: all passed\n");
    return gFail == 0 ? 0 : 1;
}
