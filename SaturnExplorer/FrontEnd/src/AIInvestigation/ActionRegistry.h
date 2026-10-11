// ActionRegistry -- the one table that defines the actions of response schema v1 (PLAN.md §6.3, §7.1-7.2).
//
// For every action type it records its parameters, its target rules, the region capabilities it needs,
// its semantic key, the model call that applies it (or the milestone that adds one), whether it is
// code-specific (§6.4), and the milestone that implements it. Three things are generated from it and
// nothing else, so they cannot drift:
//
//   * ResponseValidator's per-action checks (it walks this table; the only action-specific code is the
//     handful of cross-parameter rules, each named in the table's notes);
//   * response.schema.json (BuildResponseSchema);
//   * the action list in the prompt's response contract (BuildPromptActionList).
//
// Adding an action is adding a row here: the schema, the prompt and the validator pick it up, and the
// registry tests fail until its sample action validates.
//
// The table is data about the actions, not the code that applies them: nothing here reaches a model.
// Application is M3. "Adapter" names the model call M3 will make; its status says whether that call
// exists in the tree today (verified against the headers by a compile-time test) or arrives with a
// later milestone.
//
// No ImGui, no platform headers.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "AIInvestigation/CanonicalTarget.h"
#include "AIInvestigation/Json.h"

namespace sfe
{
namespace ai
{

enum class ParamType : uint8_t
{
    Name,        // [A-Za-z_][A-Za-z0-9_]{0,63}
    Text,        // one line, at most maxChars characters, no control characters
    Condition,   // Text that ConditionEval accepts
    Format,      // Text that FormatString accepts
    Hex,         // "0x"-prefixed upper-case hex, min..max
    HexChoice,   // one of the listed hex strings (e.g. "0x1", "0x2", "0x4")
    Enum,        // one of the listed lower-case words
    Integer,     // a JSON number, min..max (other integers are plain numbers, §5.3)
    Bool,
    Uuid,
    Handle,      // an item id inside a document ([A-Za-z0-9][A-Za-z0-9_.-]{0,63})
    RangeList,   // [{region, offset, length}, ...] for request_evidence
};

struct ParamSpec
{
    const char* name;
    ParamType type;
    bool required;
    const char* summary;                     // one line for the prompt and the schema's description
    std::vector<const char*> values;         // Enum / HexChoice
    uint64_t min = 0, max = 0;               // Integer, Hex (inclusive); 0/0 means unbounded for Hex
    size_t maxChars = 0;                     // Text, Condition, Format
};

// A field the emulator or SE cannot honour. It is refused with this reason rather than as "unknown".
struct RejectedParam
{
    const char* name;
    const char* reason;
};

// A capability bit (RegionCaps) a target's region must have, only when a parameter has one of the
// listed values (or always, when 'param' is null).
struct CapRule
{
    uint8_t caps;
    const char* param;
    std::vector<const char*> values;
};

struct TargetSpec
{
    bool required = true;            // false: the action has no target
    bool needsBus = false;           // must be a bus-addressable region (not the VDP1 frame buffer image)
    bool evenAddress = false;        // an SH-2 instruction address: even
    std::vector<CapRule> caps;
    const char* summary = "";
};

enum class AdapterStatus : uint8_t
{
    Existing,   // the model call is in the tree today
    Planned,    // arrives with 'milestone'
};

struct AdapterSpec
{
    const char* call;             // e.g. "WatchList::Add"; for a planned model, what it will be
    AdapterStatus status;
    int milestone;                // 0 when Existing; else the milestone that adds the model (2 or 3)
    const char* prerequisites;    // what else M2 must add first (UUIDs, canonical keys, ...), verified against the code
};

struct ActionSpec
{
    const char* type;
    const char* summary;
    TargetSpec target;
    std::vector<ParamSpec> params;
    std::vector<RejectedParam> rejected;
    const char* semanticKey;       // text of the key rule; SemanticKey() builds it
    AdapterSpec adapter;
    // §6.4: always true for rename_function, add_execution_breakpoint, add_log_tracepoint; true for
    // add_comment only when its target is in a region with kCapSh2Code (codeSpecificOnCodeRegion).
    bool codeSpecific;
    bool codeSpecificOnCodeRegion;
    int implementedIn;             // milestone that applies this action (M3 for every v1 action)
    const char* notes;             // the cross-parameter rules the validator applies
};

// The v1 table, in the order of PLAN.md §7.1. Built once; stable for the process.
const std::vector<ActionSpec>& ActionRegistry();
const ActionSpec* FindAction(const std::string& type);
const ParamSpec* FindParam(const ActionSpec& spec, const std::string& name);

// Whether an action of this type aimed at 'target' is code-specific (§6.4).
bool IsCodeSpecific(const ActionSpec& spec, const CanonicalTarget* target);

// The semantic key duplicate detection and undo use: the canonical target plus whichever parameters
// make two actions "the same item". Assumes the parameters already validated.
std::string SemanticKey(const ActionSpec& spec, const CanonicalTarget* target, const JsonValue& parameters);

// Limits the response contract states and the validator enforces. Kept beside the table because the
// schema and the prompt quote them.
constexpr size_t kMaxNameChars = 64;
constexpr size_t kMaxCommentChars = 256;
constexpr size_t kMaxConditionChars = 256;
constexpr size_t kMaxFormatChars = 256;
// The emulator installs at most this many tracepoints (SE_LIVE_MAX_TRACE_DESCS); a response may not
// ask for more in one go. A test pins it to the protocol constant.
constexpr size_t kMaxLogTracepoints = 256;
constexpr size_t kMaxRequestRanges = 64;

// response.schema.json: a JSON Schema (draft 2020-12) describing the whole response, generated from the
// table. SE validates with its own code, not a schema engine; this is the agent's copy of the contract.
JsonValue BuildResponseSchema();
std::string BuildResponseSchemaText();   // indented

// The plain-text action list for the prompt's response contract (§3.3 item 4).
std::string BuildPromptActionList();

}  // namespace ai
}  // namespace sfe
