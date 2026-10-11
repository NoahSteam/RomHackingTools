// ResponseValidator -- turns text an external agent returned into either a list of reasons it cannot be
// used, or a validated response and its preview (PLAN.md §6). It has no side effects: it reads the
// text and a manifest model and returns values. Nothing is applied, nothing is written, and none of the
// application models (watches, breakpoints, names, ...) is touched or even linked. Importing a response
// never starts execution; this is the half that makes sure what the user is shown is what was sent.
//
// Pipeline, each step deterministic text handling and never interpretation:
//   1. Extraction (§6.1): the whole text is one bare JSON object, or it holds exactly one fenced code
//      block labelled json, which is taken. Anything else is rejected with the reason.
//   2. Strict parse (Json.h): limits on bytes, depth, array length and string length; duplicate keys
//      and out-of-range integers are errors.
//   3. Schema v1 (§6.3), driven by ActionRegistry: required and unknown fields (the only free-form
//      place is a namespaced "extensions" object, kept inertly), an "experiment" field rejected as
//      unsupported in schema v1, name / comment grammar, parameters per action type, targets resolved to
//      CanonicalTarget and checked against the region capabilities the action needs, conditions and
//      formats parsed with ConditionValidate / FormatValidate under a length bound.
//   4. Evidence (§6.4): investigation_id / evidence_id must match the package. A mismatch stops the
//      import -- it is never fixed silently; the caller may validate again with options.rebound set, which
//      is recorded in the result, and every reference is then resolved against the package chosen. Every
//      evidence reference resolves to a manifest item or range. Code-specific actions must cite code
//      evidence covering their target; SE derives the fingerprint from that range itself, and an
//      expected_code_sha256 the agent supplies must agree.
//   5. Identity: payload_sha256 is the SHA-256 of each action object re-serialised canonically (sorted
//      keys, no insignificant whitespace), so the same (investigation, response, action id) with a
//      different payload can be told apart from a re-import (CompareIdentity).
//
// All problems are collected, not just the first. `ok` means none; a caller applying actions applies
// none of a response that is not ok.
//
// No ImGui, no platform headers, no I/O.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "AIInvestigation/ActionRegistry.h"
#include "AIInvestigation/CanonicalTarget.h"
#include "AIInvestigation/Json.h"
#include "AIInvestigation/PackageManifest.h"

namespace sfe
{
namespace ai
{

struct ResponseLimits
{
    size_t maxBytes = 1024u * 1024u;
    size_t maxDepth = 12;
    size_t maxArrayLength = 512;
    size_t maxObjectMembers = 64;
    size_t maxStringBytes = 4096;
    size_t maxHypotheses = kMaxHypotheses;
    size_t maxActions = kMaxActions;
    size_t maxEvidenceRefs = kMaxEvidenceRefs;
    size_t maxDependsOn = kMaxDependsOn;
    size_t maxClaimChars = kMaxClaimChars;
};

// ---- Step 1 -------------------------------------------------------------------------------------

struct Extraction
{
    bool ok = false;
    std::string json;    // the document text, when ok
    bool fenced = false; // taken from a fenced block rather than being the whole text
    std::string error;
};
// Backtick fences only (``` or longer), with an info string whose first word is "json" in any case.
// Other fenced blocks may appear and are ignored; tilde fences are not recognised.
Extraction ExtractResponseJson(const std::string& text);

// ---- Result -------------------------------------------------------------------------------------

enum class IssueCode : uint8_t
{
    NotJson,               // extraction or parse failed
    UnsupportedInSchemaV1, // "experiment"
    UnknownField,
    MissingField,
    WrongType,
    BadValue,
    BadSchemaVersion,
    InvestigationIdMismatch,
    EvidenceIdMismatch,    // stops the import; the caller may rebind
    UnknownEvidenceRef,
    UnknownActionType,
    DuplicateId,
    UnknownHypothesis,
    UnknownDependency,
    DependencyCycle,
    BadTarget,             // CanonicalTarget refused it
    MissingCapability,
    Misaligned,
    Grammar,               // name / comment / handle grammar
    Unparseable,           // condition or format
    RejectedParameter,     // a field SE or the emulator cannot honour
    LimitExceeded,
    RangeOutOfRegion,
    UnknownSource,
    DuplicateSemanticKey,
    DuplicateLabelName,
    TooManyTracepoints,
    CodeEvidenceMissing,
    FingerprintMismatch,
};
const char* IssueCodeName(IssueCode code);

struct ValidationIssue
{
    IssueCode code;
    std::string where;       // a JSON path, "actions[2].parameters.name"
    std::string actionId;    // the action it concerns, when it concerns one
    std::string message;
};

struct ValidatedHypothesis
{
    std::string id, claim, confidence, prediction;
    std::vector<std::string> evidenceRefs;
};

// The fingerprint SE derived from the evidence a code-specific action cites.
struct CodeFingerprint
{
    std::string sha256;        // of the bytes over the covered range
    std::string evidenceRef;   // "<item>/<range>" the fingerprint came from
    RegionId region = RegionId::Lwram;
    uint32_t offset = 0;
    uint32_t length = 0;
};

struct ValidatedAction
{
    size_t index = 0;                          // position in the response's "actions" array
    std::string id, type, hypothesisId;
    const ActionSpec* spec = nullptr;
    bool hasTarget = false;
    CanonicalTarget target;
    JsonValue parameters;
    std::vector<std::string> dependsOn;
    std::vector<std::string> evidenceRefs;     // the action's own, as written
    bool codeSpecific = false;
    bool hasFingerprint = false;
    CodeFingerprint fingerprint;
    std::string semanticKey;                   // empty for the read-only requests
    std::string payloadSha256;
    std::vector<std::string> warnings;
    bool valid = true;                         // false when any issue concerns this action
};

struct ValidationResult
{
    bool ok = false;                           // true only when there are no issues at all
    bool needsRebind = false;                  // the ids named a package other than the one given
    bool rebound = false;                      // validated against a package the user chose despite that
    std::vector<ValidationIssue> issues;

    std::string investigationId, evidenceId, responseId;   // as the response wrote them
    std::string boundEvidenceId;                           // the package it was validated against
    std::vector<ValidatedHypothesis> hypotheses;
    std::vector<ValidatedAction> actions;                  // in the order written
    std::vector<std::string> applyOrder;                   // action ids, dependencies first
    JsonValue extensions;                                  // the top-level extensions object, inert
    bool fenced = false;
};

struct ValidateOptions
{
    ResponseLimits limits;
    bool rebound = false;   // the user chose to bind a response with unmatched ids to this package
};

// Validates already-parsed JSON.
ValidationResult ValidateResponseJson(const JsonValue& document, const Manifest& manifest, const ValidateOptions& options);
// Steps 1-5 from the raw text.
ValidationResult ValidateResponse(const std::string& text, const Manifest& manifest, const ValidateOptions& options);

// ---- Identity -----------------------------------------------------------------------------------

struct ActionIdentity
{
    std::string investigationId, responseId, actionId;
    std::string payloadSha256;
};
ActionIdentity IdentityOf(const ValidationResult& result, const ValidatedAction& action);

enum class IdentityRelation : uint8_t
{
    Different,        // another action
    SamePayload,      // a re-import of the same action: shown as already applied, changes nothing
    ConflictingPayload,   // the same (investigation, response, action id) with a different payload: an error
};
IdentityRelation CompareIdentity(const ActionIdentity& a, const ActionIdentity& b);

// ---- Preview ------------------------------------------------------------------------------------

struct PreviewRow
{
    std::string actionId, type, hypothesisId;
    bool valid = false;
    std::string summary;              // one line a person can read before choosing
    std::string target;               // "hwram+0x12340 (0x06012340)", or empty
    std::string semanticKey;
    std::string adapter;              // the model call M3 will make
    bool adapterExists = false;       // verified present in the tree today
    int adapterMilestone = 0;         // when it arrives, if not
    int implementedIn = 0;
    bool codeSpecific = false;
    std::string fingerprint;          // sha256 of the covered code, when code-specific
    std::string fingerprintCovers;    // "hwram+0x12000 length 0x400 from diff/range-3"
    std::string payloadSha256;
    std::vector<std::string> dependsOn;
    std::vector<std::string> warnings;
    std::vector<std::string> problems;   // messages of the issues that concern this action
};

struct ResponsePreview
{
    bool ok = false;
    bool rebound = false;
    std::string investigationId, evidenceId, responseId;
    std::vector<std::string> topLevelProblems;
    std::vector<ValidatedHypothesis> hypotheses;
    std::vector<PreviewRow> rows;          // in applyOrder, then any not in it (invalid ones)
};

// Pure: builds the structure the Responses panel shows. Never mutates the result.
ResponsePreview BuildPreview(const ValidationResult& result);

}  // namespace ai
}  // namespace sfe
