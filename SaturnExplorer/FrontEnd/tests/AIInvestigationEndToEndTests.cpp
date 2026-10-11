// A synthetic end-to-end run of the M0 contracts (PLAN.md §11 M0, "Done when": a synthetic response
// validates and previews with no side effects).
//
//   captures (owned, immutable, hashed)  ->  manifest model  ->  a response that cites it
//     ->  validate  ->  preview structure
//
// and, while it runs, the application models that live OUTSIDE the AIInvestigation module are held under
// observation: a populated Watch list, breakpoint manager, tracepoint list and function-name store, the
// bytes of every capture, the manifest, and the working directory. None of them may change. This target
// links those models only to watch them; the AIInvestigation sources never reference them.
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "AIFixtures.h"
#include "AIInvestigation/EvidenceCapture.h"
#include "AIInvestigation/WriteInterval.h"
#include "Debug/BreakpointManager.h"
#include "Debug/CallStack.h"
#include "Debug/ExecutionActions.h"
#include "Debug/WatchList.h"

using namespace aifix;

namespace
{
int gFail = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; }
}
bool Has(const std::string& s, const std::string& needle) { return s.find(needle) != std::string::npos; }

// ---- Synthetic captures ---------------------------------------------------------------------------

ByteBlob Pattern(RegionId id, uint8_t salt)
{
    auto v = std::make_shared<std::vector<uint8_t>>(Traits(id).size);
    for (size_t i = 0; i < v->size(); ++i) (*v)[i] = static_cast<uint8_t>((i * 31 + static_cast<size_t>(id) * 7 + salt) & 0xFF);
    return v;
}

CaptureData CompareCapture(const char* frame, uint64_t serial, uint8_t salt, size_t changedOffset, uint8_t changedValue)
{
    CaptureData d;
    d.key.kind = SourceKind::CompareSnapshot;
    d.key.sourceId = 5; d.key.deriveSerial = 12; d.key.sessionId = 1; d.key.timelineEpoch = 1;
    d.key.frame = frame;
    for (size_t i = 0; i < kRegionCount; ++i)
    {
        const RegionId id = static_cast<RegionId>(i);
        if (!InCompare(id)) continue;
        ByteBlob b = Pattern(id, salt);
        if (id == RegionId::Hwram && changedValue) (*std::const_pointer_cast<std::vector<uint8_t>>(b))[changedOffset] = changedValue;
        d.regions[i] = b;
        d.key.contentSha256[RegionApiName(id)] = Sha256Hex(b->data(), b->size());
    }
    d.tracking.valid = true;
    d.tracking.captureSerial = serial;
    d.tracking.sessionId = 1; d.tracking.timelineEpoch = 1; d.tracking.connectionGeneration = 1;
    d.tracking.ledgerPosition = 0;
    d.tracking.serverCounters = ServerCounters::NotApplicable;   // a savestate source: no live server
    d.registers.reason = d.stop.reason = d.callStacks.reason = "a compare snapshot taken before register capture (M1) has none";
    return d;
}

// A paused live head whose HWRAM holds the fixture's code bytes at +0x4000.
CaptureData LiveCapture(uint8_t codeSeed, uint64_t serial)
{
    CaptureData d;
    d.key.kind = SourceKind::PausedLiveHead;
    d.key.sourceId = 5; d.key.deriveSerial = 40; d.key.sessionId = 1; d.key.timelineEpoch = 1;
    d.key.frame = "1400";
    d.key.hasConnectionGeneration = true;
    d.key.connectionGeneration = 1;
    for (RegionId id : { RegionId::Lwram, RegionId::Hwram })
    {
        ByteBlob b = Pattern(id, 99);
        if (id == RegionId::Hwram)
        {
            const std::vector<uint8_t> code = CodeBytes(codeSeed, 0x40);
            std::copy(code.begin(), code.end(), std::const_pointer_cast<std::vector<uint8_t>>(b)->begin() + 0x4000);
        }
        d.regions[static_cast<size_t>(id)] = b;
        d.key.contentSha256[RegionApiName(id)] = Sha256Hex(b->data(), b->size());
    }
    d.tracking.valid = true;
    d.tracking.captureSerial = serial;
    d.tracking.sessionId = 1; d.tracking.timelineEpoch = 1; d.tracking.connectionGeneration = 1;
    d.registers.available = true;
    d.registers.cpu[0].valid = true;
    d.registers.cpu[0].pc = 0x06004010;
    d.stop.available = true;
    d.stop.stopReason = 1;
    d.stop.pc = 0x06004010;
    d.stop.stopSeq = 3;
    d.callStacks.reason = "none recorded";
    return d;
}

// The diff of two captures' HWRAM as [offset, length) runs, found by comparing the bytes.
std::vector<std::pair<uint32_t, uint32_t>> ChangedRuns(const EvidenceCapture& a, const EvidenceCapture& b, RegionId id)
{
    std::vector<std::pair<uint32_t, uint32_t>> runs;
    const std::vector<uint8_t>& x = *a.RegionBytes(id);
    const std::vector<uint8_t>& y = *b.RegionBytes(id);
    for (uint32_t i = 0; i < x.size();)
    {
        if (x[i] == y[i]) { ++i; continue; }
        uint32_t j = i;
        while (j < x.size() && x[j] != y[j]) ++j;
        runs.emplace_back(i, j - i);
        i = j;
    }
    return runs;
}

// The manifest a Generate would publish for these captures: sources by SourceKey, the diff item
// referencing both A and B, a code item read from the live head, a configuration item stamped with its own time.
Manifest BuildManifest(const OwnedCapture& a, const OwnedCapture& b, const OwnedCapture& live)
{
    Manifest m;
    m.responseSchemaVersion = layout::kResponseSchemaVersion;
    m.investigationId = kInvestigation;
    m.evidenceId = kEvidence;
    m.preset = "find_gameplay_value";
    UtcTime when;
    when.year = 2026; when.month = 10; when.day = 11; when.hour = 13; when.minute = 45; when.second = 1;
    m.folderName = PackageFolderName(when, m.preset, m.evidenceId);
    m.createdUtc = FormatUtc(when);
    m.sources = { { "A", a.Key() }, { "B", b.Key() }, { "live", live.Key() } };

    ManifestItem diff;
    diff.id = "diff"; diff.kind = "diff"; diff.title = "HWRAM changes between A and B"; diff.sources = { "A", "B" };
    int n = 0;
    for (const auto& run : ChangedRuns(a, b, RegionId::Hwram))
    {
        ManifestRange r;
        r.id = "range-" + std::to_string(++n);
        r.sources = { "A", "B" };
        r.region = RegionId::Hwram; r.offset = run.first; r.length = run.second;
        diff.ranges.push_back(r);
    }
    m.items.push_back(diff);

    // The code range: the bytes are copied out of the live capture, not re-read from anywhere.
    const std::vector<uint8_t>& hw = *live.RegionBytes(RegionId::Hwram);
    const std::string codeFile = "attachments/code-live-hwram-4000.bin";
    const std::string codeHash = Sha256Hex(hw.data() + 0x4000, 0x40);
    m.files.push_back({ codeFile, 0x40, codeHash });
    ManifestItem code;
    code.id = "code"; code.kind = "code_range"; code.title = "Code at the write"; code.sources = { "live" };
    ManifestRange cr;
    cr.id = "range-1"; cr.sources = { "live" }; cr.region = RegionId::Hwram; cr.offset = 0x4000; cr.length = 0x40;
    cr.kind = RangeKind::Code; cr.file = codeFile; cr.sha256 = codeHash;
    code.ranges = { cr };
    code.files = { codeFile };
    m.items.push_back(code);

    ManifestItem cfg;
    cfg.id = "watches"; cfg.kind = "watches"; cfg.title = "Watches at generation time"; cfg.configuration = true; cfg.generatedUtc = m.createdUtc;
    m.items.push_back(cfg);
    return m;
}

// ---- Observing everything outside the module -----------------------------------------------------

struct World
{
    sfe::WatchList watches;
    sfe::BreakpointManager breakpoints;
    sfe::ExecutionActions tracepoints;
    sfe::FunctionNames names;

    World()
    {
        watches.Add("UserWatch", "0x06012340", sfe::WatchType::U16, true);
        breakpoints.EnableExecution(0x06004010);
        breakpoints.AddMemory(0x06012340, 2, sfe::BpKind::MemWrite);
        sfe::ExecutionAction a;
        a.cpu = 0; a.address = 0x06004012; a.format = "mine";
        tracepoints.Add(a);
        names.Rename(0x06004010, "UserNamedThis");
    }

    // Everything observable about the four models, as one string.
    std::string Fingerprint() const
    {
        std::string s = watches.ToJson();
        for (const sfe::Breakpoint& b : breakpoints.All())
            s += "|bp:" + std::to_string(b.id) + "," + std::to_string(static_cast<int>(b.kind)) + "," + std::to_string(b.address) + "," +
                 std::to_string(b.size) + "," + std::to_string(b.enabled) + "," + b.condition;
        for (const sfe::ExecutionAction& a : tracepoints.All())
            s += "|tp:" + std::to_string(a.id) + "," + std::to_string(a.cpu) + "," + std::to_string(a.address) + "," + a.format + "," +
                  std::to_string(a.hits) + "," + std::to_string(tracepoints.Generation());
        for (uint32_t addr : { 0x06004010u, 0x26004010u, 0x06012340u, 0x06004012u })
            s += "|name:" + std::to_string(addr) + "=" + (names.HasName(addr) ? names.NameOf(addr) : "-");
        return s;
    }
};

bool FileExists(const char* path)
{
    std::ifstream f(path);
    return static_cast<bool>(f);
}

void TestEndToEnd()
{
    // Captures.
    const OwnedCapture a(CompareCapture("1200", 10, 1, 0, 0));
    const OwnedCapture b(CompareCapture("1300", 11, 1, 0x12340, 0x5A));   // one byte of HWRAM differs, at +0x12340
    const OwnedCapture live(LiveCapture(0x10, 12));
    Check(ValidateCapture(a).empty() && ValidateCapture(b).empty() && ValidateCapture(live).empty(), "the captures validate: each key identifies its bytes");
    const auto runs = ChangedRuns(a, b, RegionId::Hwram);
    Check(runs.size() == 1 && runs[0].first == 0x12340 && runs[0].second == 1, "the diff finds the one changed byte");

    // The comparison interval: nothing was written between A and B, tracking was complete.
    LedgerSegment ledger;
    ledger.sessionId = 1; ledger.timelineEpoch = 1; ledger.connectionGeneration = 1;
    ledger.complete = true;
    const IntervalResult interval = ClassifyWriteInterval(b.Tracking(), a.Tracking(), ledger);   // B given first: order is by capture, not by marker
    Check(interval.status == WriteStatus::NoneDetected, "no debugger write is detected between the captures");

    // Manifest.
    const Manifest manifest = BuildManifest(a, b, live);
    const std::vector<std::string> problems = ValidateManifest(manifest);
    for (const std::string& p : problems) std::printf("  manifest problem: %s\n", p.c_str());
    Check(problems.empty(), "the manifest built from the captures is consistent");
    Check(manifest.items[0].ranges.size() == 1 && manifest.items[0].ranges[0].sources.size() == 2, "a diff range references both A and B");
    Check(manifest.items[2].sources.empty() && manifest.items[2].configuration, "the configuration item names no capture");
    // It survives its own file format.
    JsonValue parsed;
    Manifest reread;
    std::string why;
    Check(ParseJson(WriteJson(ManifestToJson(manifest), JsonWriteOptions()), JsonLimits(), &parsed, nullptr) && ManifestFromJson(parsed, &reread, &why),
          "and round-trips through manifest.json");

    // A response that cites it: label the changed byte's word, watch it, break on a write to it, and name
    // the function whose code was captured -- fingerprinted by SE from the code range, never taken on faith.
    const std::string codeHash = manifest.items[1].ranges[0].sha256;
    const std::string response = Replace(R"JSON({
      "schema_version": 1,
      "investigation_id": "2f1c0a9e-4b7d-4c21-9a3e-0d5b6c7e8f90",
      "evidence_id": "9b07e1c4-1a2b-4c3d-8e4f-5a6b7c8d9e0f",
      "response_id": "c4e2a7b1-5d6e-4f70-8192-a3b4c5d6e7f8",
      "hypotheses": [
        { "id": "h1", "claim": "The byte that changed is part of a 16-bit HP value", "confidence": "medium",
          "evidence_refs": ["diff/range-1"], "prediction": "Damage lowers it" },
        { "id": "h2", "claim": "The routine at 0x06004010 stores HP", "confidence": "low",
          "evidence_refs": ["code/range-1"], "prediction": "It runs when HP changes" }
      ],
      "actions": [
        { "id": "a1", "type": "add_address_label", "hypothesis_id": "h1", "target": { "bus_address": "0x06012340" },
          "parameters": { "name": "PlayerHP", "size": "0x2", "data_type": "u16" } },
        { "id": "a2", "type": "add_watch", "hypothesis_id": "h1", "depends_on": ["a1"], "target": { "bus_address": "0x06012340" },
          "parameters": { "name": "PlayerHP", "data_type": "u16" } },
        { "id": "a3", "type": "add_memory_watchpoint", "hypothesis_id": "h1", "depends_on": ["a1"], "target": { "region": "hwram", "offset": "0x12340" },
          "parameters": { "kind": "write", "size": "0x2" } },
        { "id": "a4", "type": "rename_function", "hypothesis_id": "h2", "target": { "bus_address": "0x06004010" },
          "expected_code_sha256": "@CODE@", "evidence_refs": ["code/range-1"], "parameters": { "name": "StoreHP" } }
      ]
    })JSON", "@CODE@", codeHash);

    // The world outside the module, before.
    World world;
    const std::string modelsBefore = world.Fingerprint();
    const std::string manifestBefore = WriteJson(ManifestToJson(manifest), JsonWriteOptions());
    const std::string hwramBefore = Sha256Hex(live.RegionBytes(RegionId::Hwram)->data(), live.RegionBytes(RegionId::Hwram)->size());
    const std::string aBefore = Sha256Hex(a.RegionBytes(RegionId::Hwram)->data(), a.RegionBytes(RegionId::Hwram)->size());
    const bool functionsFileBefore = FileExists("saturn_functions.txt"), commentsFileBefore = FileExists("assembly_comments.txt");

    // Validate, then preview.
    const ValidationResult result = ValidateResponse(response, manifest, ValidateOptions());
    for (const ValidationIssue& i : result.issues) std::printf("  issue: %s: %s\n", i.where.c_str(), i.message.c_str());
    Check(result.ok, "the response validates against the manifest");
    const ResponsePreview preview = BuildPreview(result);
    Check(preview.ok && preview.rows.size() == 4, "and previews four actions");
    Check(preview.rows[0].actionId == "a1" && preview.rows[0].target == "hwram+0x12340 (0x06012340)", "the label comes first, at the canonical target");
    Check(preview.rows[3].codeSpecific && preview.rows[3].fingerprint == codeHash &&
              Has(preview.rows[3].fingerprintCovers, "hwram+0x4000 length 0x40 from code/range-1"),
          "the rename carries the fingerprint SE derived from the live capture's bytes");
    Check(preview.rows[3].adapter == "FunctionNames::Rename" && preview.rows[3].adapterExists, "and names the model call M3 will make");

    // Nothing outside the module changed.
    Check(world.Fingerprint() == modelsBefore, "the watch list, breakpoints, tracepoints and function names are exactly as they were");
    Check(WriteJson(ManifestToJson(manifest), JsonWriteOptions()) == manifestBefore, "the manifest is unchanged");
    Check(Sha256Hex(live.RegionBytes(RegionId::Hwram)->data(), live.RegionBytes(RegionId::Hwram)->size()) == hwramBefore &&
              Sha256Hex(a.RegionBytes(RegionId::Hwram)->data(), a.RegionBytes(RegionId::Hwram)->size()) == aBefore,
          "and so are the captured bytes");
    Check(FileExists("saturn_functions.txt") == functionsFileBefore && FileExists("assembly_comments.txt") == commentsFileBefore,
          "no store file was created in the working directory");
    Check(world.names.NameOf(0x06004010) == "UserNamedThis" && !world.names.HasName(0x26004010), "the user's own name is untouched (the response's rename was only previewed)");

    // The response is stale against a new capture: the code changed.
    const OwnedCapture liveLater(LiveCapture(0x77, 13));
    const Manifest later = BuildManifest(a, b, liveLater);
    const ValidationResult stale = ValidateResponse(response, later, ValidateOptions());
    Check(!stale.ok && HasIssue(stale, IssueCode::FingerprintMismatch), "against a package whose code differs, the agent's expected_code_sha256 is stale and the action is refused");
    Check(!BuildPreview(stale).rows.empty() && !BuildPreview(stale).rows[3].valid, "and the preview marks that action invalid");
    Check(world.Fingerprint() == modelsBefore, "still nothing changed outside the module");
}
}  // namespace

int main()
{
    TestEndToEnd();
    if (gFail == 0) std::printf("AIInvestigationEndToEndTests: all passed\n");
    return gFail == 0 ? 0 : 1;
}
