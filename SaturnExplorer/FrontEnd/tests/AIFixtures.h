// Shared fixtures for the AI investigation tests: a synthetic package manifest with real hashes, and a
// response that cites it. Header-only so each test target links only what it is testing.
#pragma once

#include <string>
#include <vector>

#include "AIInvestigation/CanonicalTarget.h"
#include "AIInvestigation/PackageManifest.h"
#include "AIInvestigation/ResponseValidator.h"
#include "Sha256.h"

namespace aifix
{

using namespace sfe;
using namespace sfe::ai;

static const char kInvestigation[] = "2f1c0a9e-4b7d-4c21-9a3e-0d5b6c7e8f90";
static const char kEvidence[]      = "9b07e1c4-1a2b-4c3d-8e4f-5a6b7c8d9e0f";
static const char kResponse[]      = "c4e2a7b1-5d6e-4f70-8192-a3b4c5d6e7f8";
static const char kOtherEvidence[] = "0a1b2c3d-4e5f-4a6b-8c7d-9e0f1a2b3c4d";

inline std::string Fill(char c) { return std::string(64, c); }

// The bytes of the two code ranges, so a test can compute the fingerprint SE should derive.
inline std::vector<uint8_t> CodeBytes(uint8_t seed, size_t n)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(seed + i * 3);
    return v;
}
inline std::string CodeHash(uint8_t seed, size_t n)
{
    const std::vector<uint8_t> b = CodeBytes(seed, n);
    return Sha256Hex(b.data(), b.size());
}
inline std::string HwramCodeHash() { return CodeHash(0x10, 0x40); }   // hwram +0x4000, length 0x40
inline std::string LwramCodeHash() { return CodeHash(0x20, 0x20); }   // lwram +0x2000, length 0x20

inline SourceKey CompareKey(const char* frame, char hashSeed)
{
    SourceKey k;
    k.kind = SourceKind::CompareSnapshot;
    k.sourceId = 5; k.deriveSerial = 12; k.sessionId = 1; k.timelineEpoch = 1;
    k.frame = frame;
    for (size_t i = 0; i < kRegionCount; ++i)
        if (InCompare(static_cast<RegionId>(i))) k.contentSha256[RegionApiName(static_cast<RegionId>(i))] = Fill(hashSeed);
    return k;
}

inline Manifest MakeManifest()
{
    Manifest m;
    m.responseSchemaVersion = 1;
    m.investigationId = kInvestigation;
    m.evidenceId = kEvidence;
    m.preset = "find_gameplay_value";
    m.folderName = "20261011-134501-find_gameplay_value-9b07e1c4";
    m.createdUtc = "2026-10-11T13:45:01Z";

    const std::vector<uint8_t> hwCode = CodeBytes(0x10, 0x40), lwCode = CodeBytes(0x20, 0x20);
    m.files.push_back({ "attachments/code-live-hwram-4000.bin", 0x40, Sha256Hex(hwCode.data(), hwCode.size()) });
    m.files.push_back({ "attachments/code-live-lwram-2000.bin", 0x20, Sha256Hex(lwCode.data(), lwCode.size()) });
    m.files.push_back({ "attachments/mem-a-hwram-12300.bin", 0x80, Fill('c') });
    m.files.push_back({ "evidence.json", 0x1234, Fill('d') });

    m.sources.push_back({ "A", CompareKey("1200", 'a') });
    m.sources.push_back({ "B", CompareKey("1300", 'b') });
    SourceKey live;
    live.kind = SourceKind::PausedLiveHead;
    live.sourceId = 5; live.deriveSerial = 40; live.sessionId = 1; live.timelineEpoch = 1;
    live.frame = "1400";
    live.hasConnectionGeneration = true;
    live.connectionGeneration = 1;
    live.contentSha256["lwram"] = Fill('e');
    live.contentSha256["hwram"] = Fill('f');
    m.sources.push_back({ "live", live });

    ManifestItem diff;
    diff.id = "diff"; diff.kind = "diff"; diff.title = "Changed ranges between A and B";
    diff.sources = { "A", "B" };
    ManifestRange r12; r12.id = "range-12"; r12.sources = { "A", "B" }; r12.region = RegionId::Hwram; r12.offset = 0x12340; r12.length = 2;
    ManifestRange r13; r13.id = "range-13"; r13.sources = { "A", "B" }; r13.region = RegionId::Lwram; r13.offset = 0x100; r13.length = 4;
    diff.ranges = { r12, r13 };
    m.items.push_back(diff);

    ManifestItem mem;
    mem.id = "mem-a"; mem.kind = "memory_range"; mem.title = "HWRAM around the change, frame A";
    mem.sources = { "A" };
    ManifestRange mr; mr.id = "r1"; mr.sources = { "A" }; mr.region = RegionId::Hwram; mr.offset = 0x12300; mr.length = 0x80;
    mr.file = "attachments/mem-a-hwram-12300.bin"; mr.sha256 = Fill('c');
    mem.ranges = { mr };
    mem.files = { mr.file };
    m.items.push_back(mem);

    ManifestItem code;
    code.id = "code"; code.kind = "code_range"; code.title = "Code near the write"; code.sources = { "live" };
    ManifestRange c1; c1.id = "range-1"; c1.sources = { "live" }; c1.region = RegionId::Hwram; c1.offset = 0x4000; c1.length = 0x40;
    c1.kind = RangeKind::Code; c1.file = "attachments/code-live-hwram-4000.bin"; c1.sha256 = HwramCodeHash();
    ManifestRange c2; c2.id = "range-2"; c2.sources = { "live" }; c2.region = RegionId::Lwram; c2.offset = 0x2000; c2.length = 0x20;
    c2.kind = RangeKind::Code; c2.file = "attachments/code-live-lwram-2000.bin"; c2.sha256 = LwramCodeHash();
    code.ranges = { c1, c2 };
    code.files = { c1.file, c2.file };
    m.items.push_back(code);

    ManifestItem cfg;
    cfg.id = "watches"; cfg.kind = "watches"; cfg.title = "Watch definitions at generation time";
    cfg.configuration = true;
    cfg.generatedUtc = "2026-10-11T13:45:01Z";
    m.items.push_back(cfg);
    return m;
}

// A response the validator accepts, exercising every action type. Values are placed in a raw string so
// a test can read the JSON, then edit it with Replace().
inline std::string ValidResponseText()
{
    return R"JSON({
  "schema_version": 1,
  "investigation_id": "2f1c0a9e-4b7d-4c21-9a3e-0d5b6c7e8f90",
  "evidence_id": "9b07e1c4-1a2b-4c3d-8e4f-5a6b7c8d9e0f",
  "response_id": "c4e2a7b1-5d6e-4f70-8192-a3b4c5d6e7f8",
  "hypotheses": [
    { "id": "h1", "claim": "General HP, 16-bit", "confidence": "medium",
      "evidence_refs": ["diff/range-12"], "prediction": "A damage event decreases this value" },
    { "id": "h2", "claim": "The routine at 0x06004010 writes HP", "confidence": "low",
      "evidence_refs": ["code/range-1"], "prediction": "A breakpoint there fires when HP changes" }
  ],
  "actions": [
    { "id": "a1", "type": "add_address_label", "hypothesis_id": "h1",
      "target": { "bus_address": "0x06012340" },
      "parameters": { "name": "GeneralHP", "size": "0x2", "data_type": "u16" } },
    { "id": "a2", "type": "add_watch", "hypothesis_id": "h1", "depends_on": ["a1"],
      "target": { "bus_address": "0x06012340" },
      "parameters": { "name": "GeneralHP", "data_type": "u16" } },
    { "id": "a3", "type": "add_memory_watchpoint", "hypothesis_id": "h1", "depends_on": ["a1"],
      "target": { "region": "hwram", "offset": "0x12340" },
      "parameters": { "kind": "write", "size": "0x2" } },
    { "id": "a4", "type": "rename_function", "hypothesis_id": "h2",
      "target": { "bus_address": "0x06004010" },
      "parameters": { "name": "ApplyDamage" } },
    { "id": "a5", "type": "add_execution_breakpoint", "hypothesis_id": "h2", "evidence_refs": ["code/range-1"],
      "expected_code_sha256": "@HWRAM_CODE_SHA@",
      "target": { "bus_address": "0x06004010" },
      "parameters": { "condition": "r4 == 5" } },
    { "id": "a6", "type": "add_log_tracepoint", "hypothesis_id": "h2",
      "target": { "bus_address": "0x06004012" },
      "parameters": { "cpu": 0, "format": "HP = {*0x6012340:u16}" } },
    { "id": "a7", "type": "add_comment", "hypothesis_id": "h2",
      "target": { "bus_address": "0x06004010" },
      "parameters": { "text": "Subtracts damage from HP" } },
    { "id": "a8", "type": "request_evidence", "hypothesis_id": "h1",
      "parameters": { "source": "A", "ranges": [ { "region": "hwram", "offset": "0x12300", "length": "0x80" } ],
                      "representation": "binary", "max_bytes": "0x100", "reason": "see the structure around HP" } }
  ],
  "extensions": { "com.example.tool": { "note": "kept as is" } }
})JSON";
}

inline std::string Replace(std::string text, const std::string& from, const std::string& to)
{
    const size_t at = text.find(from);
    if (at != std::string::npos) text.replace(at, from.size(), to);
    return text;
}

inline std::string ValidResponse()
{
    return Replace(ValidResponseText(), "@HWRAM_CODE_SHA@", HwramCodeHash());
}

inline bool HasIssue(const ValidationResult& r, IssueCode code, const char* whereContains = nullptr)
{
    for (const ValidationIssue& i : r.issues)
        if (i.code == code && (!whereContains || i.where.find(whereContains) != std::string::npos)) return true;
    return false;
}

}  // namespace aifix
