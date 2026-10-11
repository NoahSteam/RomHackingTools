#include "AIInvestigation/EvidenceCapture.h"

#include "AIInvestigation/CanonicalTarget.h"   // RegionApiName
#include "Sha256.h"

namespace sfe
{
namespace ai
{

ItemAvailability Availability(const EvidenceCapture& c, CaptureItem item)
{
    ItemAvailability a;
    auto from = [&](bool available, const std::string& reason) {
        a.available = available;
        if (!available) a.reason = reason.empty() ? "this capture did not take it" : reason;
        return a;
    };
    const SourceKey& key = c.Key();
    switch (item)
    {
    case CaptureItem::Memory:
        return from(IsMemorySource(key.kind), "this source is a file, not emulator memory");
    case CaptureItem::Registers:
        return from(c.Registers().available, c.Registers().reason);
    case CaptureItem::Stop:
        return from(c.Stop().available, c.Stop().reason);
    case CaptureItem::CallStacks:
        return from(c.CallStacks().available, c.CallStacks().reason);
    case CaptureItem::WriteTracking:
        return from(c.Tracking().valid, "this capture recorded no write tracking");
    case CaptureItem::FileBytes:
        return from(!IsMemorySource(key.kind) && c.FileBytes() != nullptr, "this source has no file bytes");
    }
    return a;
}

std::string ValidateCapture(const EvidenceCapture& c)
{
    const SourceKey& key = c.Key();
    const std::string keyProblem = ValidateSourceKey(key);
    if (!keyProblem.empty()) return keyProblem;

    if (IsMemorySource(key.kind))
    {
        for (size_t i = 0; i < kRegionCount; ++i)
        {
            const RegionId id = static_cast<RegionId>(i);
            const char* name = RegionApiName(id);
            const std::vector<uint8_t>* bytes = c.RegionBytes(id);
            const auto hashed = key.contentSha256.find(name);
            if (hashed == key.contentSha256.end())
            {
                if (bytes) return std::string("the capture holds ") + name + " but its key does not name it";
                continue;
            }
            if (!bytes) return std::string("the key names ") + name + " but the capture holds no bytes for it";
            if (bytes->size() != Traits(id).size) return std::string(name) + " is not the region's size";
            if (Sha256Hex(bytes->data(), bytes->size()) != hashed->second)
                return std::string(name) + " does not hash to the key's content_sha256";
        }
        if (c.FileBytes()) return "a memory capture has no file bytes";

        const WriteCheckpoint& t = c.Tracking();
        if (t.valid)
        {
            if (t.captureSerial == 0) return "tracking is valid but has no capture serial";
            if (t.sessionId != key.sessionId || t.timelineEpoch != key.timelineEpoch)
                return "tracking and key disagree on session or timeline epoch";
            if (key.hasConnectionGeneration && t.connectionGeneration != key.connectionGeneration)
                return "tracking and key disagree on the connection generation";
        }
    }
    else
    {
        const std::vector<uint8_t>* bytes = c.FileBytes();
        if (!bytes) return "a file capture holds no bytes";
        if (Sha256Hex(bytes->data(), bytes->size()) != key.contentSha256.at(kFileContentName))
            return "the file bytes do not hash to the key's content_sha256";
        for (size_t i = 0; i < kRegionCount; ++i)
            if (c.RegionBytes(static_cast<RegionId>(i))) return "a file capture has no emulator regions";
        if (c.Registers().available || c.Stop().available || c.CallStacks().available || c.Tracking().valid)
            return "a file capture has no registers, stop, call stacks or write tracking";
    }

    // An unavailable item must say why, and must not carry data it claims not to have.
    const CaptureRegisters& r = c.Registers();
    if (!r.available)
    {
        if (r.reason.empty()) return "unavailable registers give no reason";
        if (r.cpu[0].valid || r.cpu[1].valid) return "unavailable registers carry data";
    }
    else if (!r.cpu[0].valid && !r.cpu[1].valid)
    {
        return "registers are available but neither CPU's is valid";
    }
    const CaptureStop& s = c.Stop();
    if (!s.available && s.reason.empty()) return "an unavailable stop gives no reason";
    const CaptureCallStacks& cs = c.CallStacks();
    if (!cs.available)
    {
        if (cs.reason.empty()) return "unavailable call stacks give no reason";
        if (!cs.cpu[0].empty() || !cs.cpu[1].empty()) return "unavailable call stacks carry frames";
    }
    return std::string();
}

bool GuardUnchanged(const CaptureGuardSample& a, const CaptureGuardSample& b)
{
    return a.sourceId == b.sourceId && a.deriveSerial == b.deriveSerial &&
           a.connectionGeneration == b.connectionGeneration && a.frame == b.frame;
}

}  // namespace ai
}  // namespace sfe
