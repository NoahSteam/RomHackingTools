// SourceKey -- the identity of one evidence capture (PLAN.md §5.1). Immutable once taken.
//
// A package never says "the current frame". Every attachment names the capture it was read from by
// SourceKey, and a comparison names two. The fields split into two classes that must not be confused:
//
//   * Process-local (source_id, derive_serial, session_id, timeline_epoch, connection_generation):
//     counters that mean something only inside the SE process that took the capture. They are written
//     for diagnosis and never used to decide whether two things are the same after a restart.
//   * Persistent (kind and content_sha256): hashes of the captured bytes, per region (or for the one
//     file). After a restart, these and the package's own files are all that bind evidence.
//
// Counters that can pass 2^53 are decimal strings in the JSON form (§5.3). The frame is "from
// FrameRecorder on a scrubbed frame, never se_frame_number's 0", so a memory capture's frame must be
// at least 1 -- the same rule CaptureSnapshot enforces.
//
// Types and validation only. Acquiring a capture is M1. No ImGui, no platform headers.
#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "AIInvestigation/Json.h"

namespace sfe
{
namespace ai
{

enum class SourceKind : uint8_t
{
    CompareSnapshot,   // an immutable Memory Compare snapshot
    PausedLiveHead,    // the live emulator's newest frame, while it is halted or paused
    RecorderFrame,     // one frame of the rewind recorder, bound by frame identity
    File,              // a file on the host
    DiscFile,          // a file inside a disc image
};

const char* SourceKindName(SourceKind kind);
bool SourceKindFromName(const std::string& name, SourceKind* out);
// True for the kinds that capture emulator memory (and so carry a frame and a write checkpoint).
bool IsMemorySource(SourceKind kind);

struct SourceKey
{
    SourceKind kind = SourceKind::CompareSnapshot;

    // Process-local and diagnostic only.
    uint64_t sourceId = 0;               // IMemoryBackend::SourceId()
    uint64_t deriveSerial = 0;           // se_derive_serial at capture
    uint64_t sessionId = 0;
    uint64_t timelineEpoch = 0;
    bool     hasConnectionGeneration = false;   // live only
    uint64_t connectionGeneration = 0;

    // Memory kinds: the frame number as a decimal string (>= 1). Empty for file kinds.
    std::string frame;

    // Persistent: lower-case hex SHA-256 of the captured bytes, keyed by region wire name (memory
    // kinds) or "file" (file kinds).
    std::map<std::string, std::string> contentSha256;

    bool operator==(const SourceKey& o) const;
    bool operator!=(const SourceKey& o) const { return !(*this == o); }
};

// The key a file or disc-file capture carries its single content hash under.
extern const char kFileContentName[];

// Empty when the key obeys its kind's rules, else the first problem:
//   * a memory kind has a frame >= 1, at least one region hash and only real region names; a compare
//     snapshot has exactly the regions Memory Compare holds (so no sound RAM);
//   * a paused live head has a connection generation; a file kind has none, no frame, and exactly one
//     "file" hash;
//   * every hash is 64 lower-case hex digits.
std::string ValidateSourceKey(const SourceKey& key);

// Same persistent identity: same kind and same content hashes. Ignores every process-local field, so
// it is the comparison that still means something after a restart.
bool SameContent(const SourceKey& a, const SourceKey& b);

JsonValue SourceKeyToJson(const SourceKey& key);
// Strict: every member must be known and well-typed, and the result must pass ValidateSourceKey.
bool SourceKeyFromJson(const JsonValue& value, SourceKey* out, std::string* error);

}  // namespace ai
}  // namespace sfe
