// PackageManifest -- the in-memory model of a package's manifest.json, and the layout constants of the
// package directory (PLAN.md §5.4). Reading and writing the files is M1; this is the data, its JSON
// form, and the rules a manifest must satisfy, which is what a response is validated against.
//
//   <investigations>/<yyyymmdd-hhmmss>-<preset>-<short evidence id>/
//     manifest.json          package/schema version, IDs, SourceKeys, item -> files, ranges, sizes, SHA-256, coverage
//     prompt.md              exactly what Copy Prompt copies
//     evidence.json          captured facts, observations and derived interpretations in separate fields
//     response.schema.json   machine-readable response schema (only when the contract is included)
//     attachments/           selected binary ranges, text listings, images
//     source/                retained source data (only when the user chose it, §5.6)
//
// What the manifest guarantees:
//   * Every source is named by SourceKey. Every non-configuration item and range names the source (or,
//     for a diff range, both sources) it was read from: provenance comes from the capture, not from the
//     collector (§5.1).
//   * Configuration items (watch, breakpoint, name and comment definitions) carry no source and their own
//     generation-time stamp; they never claim to describe frame A or B.
//   * An attachment is one contiguous range of one region of one capture; the range records region, offset,
//     length, source and hash, and the file's own hash and size must agree with it.
//   * Every path is relative, uses '/', stays inside the package and lies under attachments/ or source/
//     (or is one of the fixed files); the reader rejects anything else.
//
// No ImGui, no platform headers, no file I/O.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "AIInvestigation/CanonicalTarget.h"
#include "AIInvestigation/Json.h"
#include "AIInvestigation/SourceKey.h"

namespace sfe
{
namespace ai
{

namespace layout
{
constexpr int kPackageVersion = 1;
constexpr int kResponseSchemaVersion = 1;
extern const char kManifestFile[];         // "manifest.json"
extern const char kPromptFile[];           // "prompt.md"
extern const char kEvidenceFile[];         // "evidence.json"
extern const char kResponseSchemaFile[];   // "response.schema.json"
extern const char kAttachmentsDir[];       // "attachments"
extern const char kSourceDir[];            // "source"
extern const char kDefaultInvestigationsDir[];   // "investigations", under Settings::ConfigSubDir
}  // namespace layout

// A UTC time, the parts of a package folder name. A plain struct so naming needs no clock or locale.
struct UtcTime
{
    int year = 1970, month = 1, day = 1, hour = 0, minute = 0, second = 0;
};

// [a-z][a-z0-9_]{0,47}: a preset's stable id.
bool IsPresetId(const std::string& s);
// The first eight characters of a UUID (its first group), as the folder name carries it.
std::string ShortEvidenceId(const std::string& evidenceId);
// "yyyymmdd-hhmmss-<preset>-<short evidence id>". Empty when the preset or the ID is malformed.
std::string PackageFolderName(const UtcTime& when, const std::string& preset, const std::string& evidenceId);
// "YYYY-MM-DDTHH:MM:SSZ".
std::string FormatUtc(const UtcTime& when);
bool IsUtcStamp(const std::string& s);

// A path inside the package: relative, '/'-separated, no empty / "." / ".." segments, no drive letter,
// no control characters or backslash, and either one of the fixed files or under attachments/ or
// source/. This is the textual half of "the reader rejects absolute paths, traversal, links escaping the
// package and hash mismatches"; links and hashes are checked against the real files in M1.
bool IsValidPackagePath(const std::string& path, std::string* why = nullptr);

struct ManifestFile
{
    std::string path;
    uint64_t size = 0;
    std::string sha256;
};

struct ManifestSource
{
    std::string id;     // a handle, "A", "B", "live", ...
    SourceKey key;
};

enum class RangeKind : uint8_t
{
    Data,   // bytes the agent may read as data
    Code,   // bytes of SH-2 code; the range's hash is the fingerprint code-specific actions are checked against
};

struct ManifestRange
{
    std::string id;                     // a handle, unique within its item
    std::vector<std::string> sources;   // one source, or two for a range of a diff between A and B
    RegionId region = RegionId::Lwram;
    uint32_t offset = 0;
    uint32_t length = 0;
    RangeKind kind = RangeKind::Data;
    std::string file;                   // the attachment holding exactly these bytes; empty if not attached
    std::string sha256;                 // of those bytes; empty if not hashed (a diff range has none)
};

struct ManifestCoverage
{
    bool complete = true;
    std::vector<std::pair<uint32_t, uint32_t>> omitted;   // (offset, length) the export left out
};

struct ManifestItem
{
    std::string id;
    std::string kind;     // handle-like: "diff", "memory_range", "file_sample", "code_range", "watches", ...
    std::string title;
    std::vector<std::string> sources;     // empty only for a configuration item
    bool configuration = false;           // recorded as configuration at generation time
    std::string generatedUtc;             // the configuration item's own timestamp
    std::vector<ManifestRange> ranges;
    std::vector<std::string> files;       // other files that belong to the item (listings, images)
    ManifestCoverage coverage;
};

struct ManifestParent
{
    std::string evidenceId;
    std::string responseId;
};

struct Manifest
{
    int packageVersion = layout::kPackageVersion;
    int responseSchemaVersion = 0;        // 0: the response contract was not included
    std::string investigationId;
    std::string evidenceId;
    std::string folderName;
    std::string preset;
    std::string createdUtc;
    bool hasParent = false;
    ManifestParent parent;
    std::vector<ManifestFile> files;      // every file in the package except manifest.json itself
    std::vector<ManifestSource> sources;
    std::vector<ManifestItem> items;

    const ManifestSource* FindSource(const std::string& id) const;
    const ManifestItem* FindItem(const std::string& id) const;
    const ManifestFile* FindFile(const std::string& path) const;
};

// Empty when the manifest is self-consistent, else every problem found (so a package can be rejected
// with all its faults at once). Checks IDs and stamps, unique ids, path validity, that every reference
// resolves (source, file), that ranges fit their regions and agree with their files' hashes and sizes,
// and that provenance rules above hold.
std::vector<std::string> ValidateManifest(const Manifest& manifest);

JsonValue ManifestToJson(const Manifest& manifest);
// Strict (unknown members are errors) and, on success, the result has passed ValidateManifest.
bool ManifestFromJson(const JsonValue& json, Manifest* out, std::string* error);

// An evidence reference: "<item id>" or "<item id>/<range id>".
struct ResolvedRef
{
    const ManifestItem* item = nullptr;
    const ManifestRange* range = nullptr;   // null for a whole-item reference
};
bool ResolveEvidenceRef(const Manifest& manifest, const std::string& ref, ResolvedRef* out, std::string* why = nullptr);

}  // namespace ai
}  // namespace sfe
