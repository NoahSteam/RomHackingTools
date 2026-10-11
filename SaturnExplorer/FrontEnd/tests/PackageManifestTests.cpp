// PackageManifest tests (PLAN.md §5.4): layout constants, folder names, path rules, the manifest's
// consistency rules and strict JSON form, and evidence-reference resolution.
#include <cstdio>
#include <string>

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

bool Mentions(const std::vector<std::string>& problems, const char* needle)
{
    for (const std::string& p : problems) if (Has(p, needle)) return true;
    return false;
}

void TestLayout()
{
    Check(std::string(layout::kManifestFile) == "manifest.json" && std::string(layout::kPromptFile) == "prompt.md" &&
              std::string(layout::kEvidenceFile) == "evidence.json" && std::string(layout::kResponseSchemaFile) == "response.schema.json" &&
              std::string(layout::kAttachmentsDir) == "attachments" && std::string(layout::kSourceDir) == "source" &&
              std::string(layout::kDefaultInvestigationsDir) == "investigations",
          "the names §5.4 gives");
    Check(layout::kPackageVersion == 1 && layout::kResponseSchemaVersion == 1, "versions");

    UtcTime t;
    t.year = 2026; t.month = 10; t.day = 11; t.hour = 13; t.minute = 45; t.second = 1;
    Check(PackageFolderName(t, "find_gameplay_value", kEvidence) == "20261011-134501-find_gameplay_value-9b07e1c4", "<yyyymmdd-hhmmss>-<preset>-<short evidence id>");
    Check(FormatUtc(t) == "2026-10-11T13:45:01Z" && IsUtcStamp(FormatUtc(t)), "the stamp");
    Check(ShortEvidenceId(kEvidence) == "9b07e1c4" && ShortEvidenceId("nope").empty(), "the short id is the UUID's first group");
    Check(PackageFolderName(t, "Find", kEvidence).empty() && PackageFolderName(t, "", kEvidence).empty() && PackageFolderName(t, "a-b", kEvidence).empty() &&
              PackageFolderName(t, "../x", kEvidence).empty(), "a bad preset id cannot become part of a path");
    Check(PackageFolderName(t, "x", "not-a-uuid").empty(), "a bad evidence id");
    UtcTime bad = t;
    bad.month = 13;
    Check(PackageFolderName(bad, "x", kEvidence).empty(), "a bad time");
    Check(IsPresetId("find_gameplay_value") && IsPresetId("custom") && IsPresetId("a1") && !IsPresetId("1a") && !IsPresetId("A") &&
              !IsPresetId("") && !IsPresetId(std::string(49, 'a')) && IsPresetId(std::string(48, 'a')), "preset ids");
    for (const char* s : { "2026-10-11T13:45:01Z" }) Check(IsUtcStamp(s), "a valid stamp");
    for (const char* s : { "2026-10-11 13:45:01Z", "2026-10-11T13:45:01", "2026-13-11T13:45:01Z", "2026-10-32T13:45:01Z", "2026-10-11T24:45:01Z",
                           "2026-10-11T13:60:01Z", "26-10-11T13:45:01Z", "" })
        Check(!IsUtcStamp(s), "an invalid stamp");
}

void TestPaths()
{
    std::string why;
    for (const char* p : { "prompt.md", "evidence.json", "response.schema.json", "attachments/a.bin", "attachments/diff/x-1.bin",
                           "source/lwram.bin", "source/a/b/c" })
        Check(IsValidPackagePath(p), (std::string("valid path ") + p).c_str());
    struct Bad { const char* path; const char* why; };
    const Bad bad[] = {
        { "", "empty" }, { "/etc/passwd", "absolute" }, { "C:/x", "drive" }, { "c:\\x", "drive" }, { "../x", "'..'" },
        { "attachments/../x", "'..'" }, { "attachments/./x", "'.'" }, { "attachments//x", "empty path segment" }, { "attachments/", "empty path segment" },
        { "attachments\\x", "backslash" }, { "attachments/x.", "ends in" }, { "attachments/x ", "ends in" }, { "manifest.json", "does not list itself" },
        { "other.txt", "fixed files" }, { "notes/x.bin", "outside" }, { "prompt.md/x", "outside" }, { "attachments/a\nb", "control" },
        { "attachments/a:b", "':'" }, { "attachments/a\x7F", "control" }, { "ATTACHMENTS/x", "outside" }, { "/attachments/x", "absolute" },
        { "attachments/..", "'..'" }, { ".", "'.'" },
    };
    for (const Bad& b : bad)
    {
        why.clear();
        Check(!IsValidPackagePath(b.path, &why) && Has(why, b.why), (std::string("rejected path '") + b.path + "' (" + why + ")").c_str());
    }
}

void TestFixtureIsValid()
{
    const std::vector<std::string> p = ValidateManifest(MakeManifest());
    for (const std::string& s : p) std::printf("  problem: %s\n", s.c_str());
    Check(p.empty(), "the fixture manifest is self-consistent");
}

Manifest Edited(void (*edit)(Manifest&))
{
    Manifest m = MakeManifest();
    edit(m);
    return m;
}

void TestFaults()
{
    struct Case { const char* name; void (*edit)(Manifest&); const char* mentions; };
    const Case cases[] = {
        { "package version", [](Manifest& m) { m.packageVersion = 2; }, "package_version" },
        { "schema version", [](Manifest& m) { m.responseSchemaVersion = 7; }, "response_schema_version" },
        { "investigation id", [](Manifest& m) { m.investigationId = "x"; }, "investigation_id" },
        { "evidence id", [](Manifest& m) { m.evidenceId = "x"; }, "evidence_id" },
        { "preset", [](Manifest& m) { m.preset = "Bad Preset"; }, "preset" },
        { "created", [](Manifest& m) { m.createdUtc = "yesterday"; }, "created_utc" },
        { "folder name", [](Manifest& m) { m.folderName = "20261011-134501-find_gameplay_value-deadbeef"; }, "folder_name" },
        { "empty folder", [](Manifest& m) { m.folderName.clear(); }, "folder_name" },
        { "parent ids", [](Manifest& m) { m.hasParent = true; m.parent.evidenceId = "x"; }, "parent" },
        { "duplicate file", [](Manifest& m) { m.files.push_back(m.files[0]); }, "listed twice" },
        { "bad file hash", [](Manifest& m) { m.files[0].sha256 = "xyz"; }, "sha256" },
        { "file outside the package", [](Manifest& m) { m.files[0].path = "../escape.bin"; }, "'..'" },
        { "file path absolute", [](Manifest& m) { m.files[0].path = "/etc/x"; }, "absolute" },
        { "duplicate source", [](Manifest& m) { m.sources.push_back(m.sources[0]); }, "listed twice" },
        { "bad source id", [](Manifest& m) { m.sources[0].id = "a/b"; }, "not a handle" },
        { "bad source key", [](Manifest& m) { m.sources[0].key.frame = "0"; }, "frame is 0" },
        { "item with an unknown source", [](Manifest& m) { m.items[0].sources.push_back("Z"); }, "unknown source" },
        { "item with no source", [](Manifest& m) { m.items[1].sources.clear(); }, "names no source" },
        { "item source listed twice", [](Manifest& m) { m.items[0].sources.push_back("A"); }, "twice" },
        { "configuration item with a source", [](Manifest& m) { m.items[3].sources.push_back("A"); }, "names no source" },
        { "configuration item without a stamp", [](Manifest& m) { m.items[3].generatedUtc.clear(); }, "generated_utc" },
        { "configuration item with ranges", [](Manifest& m) { m.items[3].ranges.push_back(m.items[1].ranges[0]); }, "no memory ranges" },
        { "stamp on a non-configuration item", [](Manifest& m) { m.items[0].generatedUtc = "2026-10-11T13:45:01Z"; }, "only a configuration item" },
        { "duplicate item", [](Manifest& m) { m.items.push_back(m.items[0]); }, "listed twice" },
        { "bad item id", [](Manifest& m) { m.items[0].id = "has space"; }, "not a handle" },
        { "item file not listed", [](Manifest& m) { m.items[1].files.push_back("attachments/ghost.bin"); }, "not in the package's file list" },
        { "duplicate range", [](Manifest& m) { m.items[0].ranges.push_back(m.items[0].ranges[0]); }, "listed twice" },
        { "range source not in item", [](Manifest& m) { m.items[1].ranges[0].sources = { "B" }; }, "not one of the item's sources" },
        { "range with no source", [](Manifest& m) { m.items[0].ranges[0].sources.clear(); }, "names one source" },
        { "range with three sources", [](Manifest& m) { m.items[0].ranges[0].sources = { "A", "B", "live" }; }, "names one source" },
        { "range outside its region", [](Manifest& m) { m.items[0].ranges[0].offset = 0xFFFFF; m.items[0].ranges[0].length = 2; }, "does not fit" },
        { "zero-length range", [](Manifest& m) { m.items[0].ranges[0].length = 0; }, "does not fit" },
        { "region the source does not hold", [](Manifest& m) { m.items[0].ranges[0].region = RegionId::SoundRam; }, "holds no soundram" },
        { "range from a file source", [](Manifest& m) {
              SourceKey k;
              k.kind = SourceKind::File;
              k.contentSha256["file"] = Fill('1');
              m.sources.push_back({ "f", k });
              m.items[1].sources = { "f" };
              m.items[1].ranges[0].sources = { "f" };
          }, "is a file" },
        { "attached range from two sources", [](Manifest& m) { m.items[1].sources = { "A", "B" }; m.items[1].ranges[0].sources = { "A", "B" }; }, "exactly one source" },
        { "attached range without a hash", [](Manifest& m) { m.items[1].ranges[0].sha256.clear(); }, "sha256" },
        { "range file missing", [](Manifest& m) { m.items[1].ranges[0].file = "attachments/ghost.bin"; }, "not in the package's file list" },
        { "range file size", [](Manifest& m) { m.files[2].size = 0x81; }, "size is not the range's length" },
        { "range file hash", [](Manifest& m) { m.files[2].sha256 = Fill('0'); }, "sha256 is not the range's" },
        { "range file outside attachments", [](Manifest& m) { m.items[1].ranges[0].file = "evidence.json"; m.files[3].size = 0x80; m.files[3].sha256 = Fill('c'); }, "lives under" },
        { "code range from two sources", [](Manifest& m) { m.items[2].sources = { "live", "A" }; m.items[2].ranges[0].sources = { "live", "A" }; }, "one source" },
        { "code range without a hash", [](Manifest& m) { m.items[2].ranges[0].sha256.clear(); m.items[2].ranges[0].file.clear(); }, "carries the hash" },
        { "code in a region that cannot hold code", [](Manifest& m) {
              m.items[2].ranges[0].region = RegionId::Vdp2Ram;
              m.items[2].sources = { "live" };
              m.sources[2].key.contentSha256["vdp2ram"] = Fill('9');
              m.items[2].ranges[0].file.clear();
          }, "cannot hold SH-2 code" },
        { "omitted range of zero length", [](Manifest& m) { m.items[0].coverage.omitted.emplace_back(0x10, 0); }, "zero length" },
    };
    for (const Case& c : cases)
    {
        const std::vector<std::string> p = ValidateManifest(Edited(c.edit));
        if (!Mentions(p, c.mentions))
        {
            std::printf("FAIL: manifest fault '%s' (wanted a problem mentioning '%s')\n", c.name, c.mentions);
            for (const std::string& s : p) std::printf("      got: %s\n", s.c_str());
            ++gFail;
        }
    }

    // Every fault is reported, not just the first.
    Manifest m = MakeManifest();
    m.packageVersion = 2;
    m.evidenceId = "x";
    m.files[0].sha256 = "q";
    Check(ValidateManifest(m).size() >= 3, "all faults are reported together");

    // A diff range between A and B is allowed to be unhashed and unattached.
    m = MakeManifest();
    Check(m.items[0].ranges[0].sources.size() == 2 && m.items[0].ranges[0].sha256.empty() && ValidateManifest(m).empty(), "a diff range names both sources and needs no hash");
}

void TestJson()
{
    const Manifest m = MakeManifest();
    const std::string text = WriteJson(ManifestToJson(m), JsonWriteOptions());
    JsonValue parsed;
    JsonError err;
    Check(ParseJson(text, JsonLimits(), &parsed, &err), "the manifest text parses");
    Manifest back;
    std::string why;
    Check(ManifestFromJson(parsed, &back, &why), "and reads back");
    Check(WriteJson(ManifestToJson(back), JsonWriteOptions()) == text, "byte-identically on a second write");
    Check(back.items.size() == m.items.size() && back.sources.size() == 3 && back.sources[2].key.hasConnectionGeneration, "with its content");
    Check(back.items[3].configuration && back.items[3].generatedUtc == "2026-10-11T13:45:01Z" && back.items[3].sources.empty(), "a configuration item keeps its own stamp and no source");

    // Offsets and sizes are the plan's hex strings.
    const JsonValue j = ManifestToJson(m);
    const JsonValue* r = j.Find("items")->items()[0].Find("ranges")->items()[0].Find("offset");
    Check(r && r->AsString() == "0x12340", "range offsets are 0x-hex strings");
    Check(j.Find("files")->items()[0].Find("size")->AsString() == "0x40", "file sizes are 0x-hex strings");
    Check(j.Find("package_version") && j.Find("package_version")->IsInteger(), "versions are plain numbers");

    // Parent link.
    Manifest withParent = m;
    withParent.hasParent = true;
    withParent.parent.evidenceId = kOtherEvidence;
    withParent.parent.responseId = kResponse;
    Check(ParseJson(WriteJson(ManifestToJson(withParent), JsonWriteOptions()), JsonLimits(), &parsed, nullptr) && ManifestFromJson(parsed, &back, nullptr) &&
              back.hasParent && back.parent.evidenceId == kOtherEvidence,
          "a child package links its parent");

    // Strictness: an unknown or mistyped member anywhere is an error, and so is an inconsistent manifest.
    auto rejects = [&](const std::string& edited) {
        JsonValue v;
        Manifest out;
        std::string error;
        if (!ParseJson(edited, JsonLimits(), &v, nullptr)) return false;
        return !ManifestFromJson(v, &out, &error) && !error.empty();
    };
    Check(rejects(Replace(text, "\"package_version\"", "\"surprise\":1,\"package_version\"")), "an unknown top-level member");
    Check(rejects(Replace(text, "\"title\"", "\"colour\":1,\"title\"")), "an unknown item member");
    Check(rejects(Replace(text, "\"region\":\"hwram\"", "\"extra\":1,\"region\":\"hwram\"")), "an unknown range member");
    Check(rejects(Replace(text, "\"sha256\":\"" + HwramCodeHash() + "\"", "\"sha256\":\"zz\"")), "a bad hash");
    Check(rejects(Replace(text, "\"offset\":\"0x12340\"", "\"offset\":\"0x12340\",\"offset2\":1")), "another unknown member");
    Check(rejects(Replace(text, "\"offset\":\"0x12340\"", "\"offset\":\"0x1234a\"")), "a lower-case hex offset");
    Check(rejects(Replace(text, "\"offset\":\"0x12340\"", "\"offset\":74560")), "an offset as a number");
    Check(rejects(Replace(text, "\"region\":\"hwram\",\"offset\":\"0x12340\"", "\"region\":\"HWRAM\",\"offset\":\"0x12340\"")), "a region in the wrong case");
    Check(rejects(Replace(text, "\"kind\":\"code\"", "\"kind\":\"script\"")), "an unknown range kind");
    Check(rejects(Replace(text, "\"package_version\":1", "\"package_version\":2")), "an unsupported package version");
    Check(rejects(Replace(text, "\"id\":\"A\"", "\"id\":\"A\",\"x\":1")), "an unknown source member");
    Check(rejects("[]") && rejects("{}"), "not a manifest");
    Check(rejects(Replace(text, ",\"configuration\":true", ",\"configuration\":1")), "configuration is a boolean");
    Check(rejects(Replace(text, "\"files\":[{\"path\":\"attachments/code-live-hwram-4000.bin\"", "\"files\":[{\"path\":\"attachments/../x\"")), "a traversal path");
}

void TestResolve()
{
    const Manifest m = MakeManifest();
    ResolvedRef r;
    std::string why;
    Check(ResolveEvidenceRef(m, "diff", &r, &why) && r.item && r.item->id == "diff" && !r.range, "a whole item");
    Check(ResolveEvidenceRef(m, "diff/range-12", &r, &why) && r.range && r.range->id == "range-12" && r.range->offset == 0x12340, "an item and range");
    Check(ResolveEvidenceRef(m, "watches", &r, &why) && r.item->configuration, "a configuration item");
    for (const char* bad : { "", "/", "diff/", "/range-12", "diff/range-12/x", "nope", "diff/range-99", "diff range", "Diff", "diff/RANGE-12" })
    {
        why.clear();
        Check(!ResolveEvidenceRef(m, bad, &r, &why) && !why.empty(), (std::string("unresolved reference '") + bad + "'").c_str());
    }
    ResolveEvidenceRef(m, "diff/range-99", &r, &why);
    Check(Has(why, "range-99") && Has(why, "diff"), "the message names the item and the range");
    ResolveEvidenceRef(m, "ghost/range-1", &r, &why);
    Check(Has(why, "ghost"), "or the item");
    Check(m.FindSource("A") && !m.FindSource("Z") && m.FindItem("code") && !m.FindItem("x") && m.FindFile("evidence.json") && !m.FindFile("x"), "lookups");
}
}  // namespace

int main()
{
    TestLayout();
    TestPaths();
    TestFixtureIsValid();
    TestFaults();
    TestJson();
    TestResolve();
    if (gFail == 0) std::printf("PackageManifestTests: all passed\n");
    return gFail == 0 ? 0 : 1;
}
