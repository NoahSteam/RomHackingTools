// SourceKey and EvidenceCapture tests (PLAN.md §5.1): the identity rules per kind, JSON round trips,
// and that a capture's key really identifies its bytes.
#include "AIInvestigation/EvidenceCapture.h"

#include <cstdio>
#include <memory>
#include <string>

#include "AIInvestigation/CanonicalTarget.h"
#include "AIInvestigation/Encoding.h"
#include "Sha256.h"

using namespace sfe;
using namespace sfe::ai;

namespace
{
int gFail = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; }
}
bool Has(const std::string& s, const char* needle) { return s.find(needle) != std::string::npos; }

std::string H(char c) { return std::string(64, c); }

SourceKey CompareKey()
{
    SourceKey k;
    k.kind = SourceKind::CompareSnapshot;
    k.sourceId = 5; k.deriveSerial = 12; k.sessionId = 1; k.timelineEpoch = 2;
    k.frame = "9007199254740993";   // 2^53+1: exact only as a string
    for (size_t i = 0; i < kRegionCount; ++i)
        if (InCompare(static_cast<RegionId>(i))) k.contentSha256[RegionApiName(static_cast<RegionId>(i))] = H('a');
    return k;
}

SourceKey LiveKey()
{
    SourceKey k;
    k.kind = SourceKind::PausedLiveHead;
    k.frame = "120";
    k.hasConnectionGeneration = true;
    k.connectionGeneration = 3;
    k.contentSha256["lwram"] = H('b');
    k.contentSha256["hwram"] = H('c');
    return k;
}

SourceKey FileKey()
{
    SourceKey k;
    k.kind = SourceKind::File;
    k.contentSha256[kFileContentName] = H('d');
    return k;
}

void TestKeyRules()
{
    Check(ValidateSourceKey(CompareKey()).empty(), "a compare snapshot key");
    Check(ValidateSourceKey(LiveKey()).empty(), "a paused live head key");
    Check(ValidateSourceKey(FileKey()).empty(), "a file key");

    SourceKey k = CompareKey();
    k.frame = "0";
    Check(Has(ValidateSourceKey(k), "frame is 0"), "frame 0 is refused (se_frame_number's 'unknown')");
    k.frame = "";
    Check(!ValidateSourceKey(k).empty(), "an empty frame is refused");
    k.frame = "12x";
    Check(!ValidateSourceKey(k).empty(), "a non-decimal frame is refused");
    k.frame = "01";
    Check(!ValidateSourceKey(k).empty(), "a leading-zero frame is refused");

    k = CompareKey();
    k.contentSha256["soundram"] = H('a');
    Check(Has(ValidateSourceKey(k), "soundram"), "a compare snapshot holds no sound RAM");
    k = CompareKey();
    k.contentSha256.erase("vdp2cram");
    Check(Has(ValidateSourceKey(k), "vdp2cram"), "a compare snapshot must account for every compared region");
    k = CompareKey();
    k.contentSha256["bios"] = H('a');
    Check(Has(ValidateSourceKey(k), "unknown region"), "an unknown region name");
    k = CompareKey();
    k.contentSha256["lwram"] = H('A');
    Check(Has(ValidateSourceKey(k), "lower-case hex"), "hashes are lower case");
    k.contentSha256["lwram"] = std::string(63, 'a');
    Check(!ValidateSourceKey(k).empty(), "and 64 digits");

    k = LiveKey();
    k.hasConnectionGeneration = false;
    Check(Has(ValidateSourceKey(k), "connection_generation"), "a live head needs its connection generation");
    k = LiveKey();
    k.contentSha256["soundram"] = H('e');
    Check(ValidateSourceKey(k).empty(), "a live head may hold sound RAM (it is only left out of comparisons)");
    k = LiveKey();
    k.contentSha256.clear();
    Check(!ValidateSourceKey(k).empty(), "a memory capture needs a region hash");
    k = LiveKey();
    k.kind = SourceKind::RecorderFrame;
    Check(ValidateSourceKey(k).empty(), "a recorder frame may carry a connection generation");
    k.hasConnectionGeneration = false;
    Check(ValidateSourceKey(k).empty(), "or not");

    k = FileKey();
    k.frame = "1";
    Check(Has(ValidateSourceKey(k), "no frame"), "a file has no frame");
    k = FileKey();
    k.hasConnectionGeneration = true;
    Check(!ValidateSourceKey(k).empty(), "a file has no connection generation");
    k = FileKey();
    k.contentSha256["extra"] = H('e');
    Check(!ValidateSourceKey(k).empty(), "a file has exactly one hash");
    k = FileKey();
    k.contentSha256.clear();
    k.contentSha256["lwram"] = H('d');
    Check(!ValidateSourceKey(k).empty(), "named \"file\"");
    k = FileKey();
    k.kind = SourceKind::DiscFile;
    Check(ValidateSourceKey(k).empty(), "a disc file follows the same rules");
}

void TestJson()
{
    for (const SourceKey& k : { CompareKey(), LiveKey(), FileKey() })
    {
        const JsonValue j = SourceKeyToJson(k);
        SourceKey back;
        std::string why;
        Check(SourceKeyFromJson(j, &back, &why) && back == k, "a key round-trips through JSON");
        // And through text, with the process-local counters as decimal strings.
        JsonValue parsed;
        Check(ParseJson(ToCanonicalJson(j), JsonLimits(), &parsed, nullptr) && SourceKeyFromJson(parsed, &back, nullptr) && back == k,
              "and through text");
    }
    const JsonValue j = SourceKeyToJson(CompareKey());
    Check(j.Find("frame") && j.Find("frame")->AsString() == "9007199254740993", "a frame past 2^53 is a decimal string");
    Check(j.Find("kind")->AsString() == "compare_snapshot" && SourceKeyToJson(LiveKey()).Find("kind")->AsString() == "paused_live_head" &&
              SourceKeyToJson(FileKey()).Find("kind")->AsString() == "file",
          "kind names are the plan's");
    Check(!SourceKeyToJson(FileKey()).Has("frame") && !SourceKeyToJson(FileKey()).Has("connection_generation"), "a file writes no frame");

    auto bad = [](const char* text) {
        JsonValue v;
        if (!ParseJson(text, JsonLimits(), &v, nullptr)) return false;
        SourceKey k;
        std::string why;
        return !SourceKeyFromJson(v, &k, &why) && !why.empty();
    };
    Check(bad("[]"), "not an object");
    Check(bad("{\"kind\":\"file\",\"source_id\":\"0\",\"derive_serial\":\"0\",\"session_id\":\"0\",\"timeline_epoch\":\"0\","
              "\"content_sha256\":{\"file\":\"" "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd" "\"},\"extra\":1}"),
          "an unknown field");
    Check(bad("{\"kind\":\"file\",\"source_id\":0,\"derive_serial\":\"0\",\"session_id\":\"0\",\"timeline_epoch\":\"0\","
              "\"content_sha256\":{\"file\":\"dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd\"}}"),
          "a counter as a JSON number");
    Check(bad("{\"kind\":\"nope\",\"source_id\":\"0\",\"derive_serial\":\"0\",\"session_id\":\"0\",\"timeline_epoch\":\"0\",\"content_sha256\":{}}"),
          "an unknown kind");
    Check(bad("{\"kind\":\"file\",\"source_id\":\"0\",\"derive_serial\":\"0\",\"session_id\":\"0\",\"timeline_epoch\":\"0\",\"content_sha256\":{\"file\":\"zz\"}}"),
          "a bad hash");
}

void TestSameContent()
{
    SourceKey a = CompareKey(), b = CompareKey();
    b.sourceId = 99; b.deriveSerial = 1; b.sessionId = 7; b.timelineEpoch = 8; b.frame = "55";
    Check(a != b && SameContent(a, b), "process-local fields do not change the persistent identity");
    b.contentSha256["lwram"] = H('f');
    Check(!SameContent(a, b), "different bytes are a different source");
    SourceKey l = LiveKey();
    l.kind = SourceKind::RecorderFrame;
    Check(!SameContent(LiveKey(), l), "a different kind is a different source");
}

// ---- Captures -----------------------------------------------------------------------------------

ByteBlob Bytes(size_t n, uint8_t seed)
{
    auto v = std::make_shared<std::vector<uint8_t>>(n);
    for (size_t i = 0; i < n; ++i) (*v)[i] = static_cast<uint8_t>(seed + i * 7);
    return v;
}

// A live head holding LWRAM and HWRAM, hashed into its key.
CaptureData LiveCapture()
{
    CaptureData d;
    d.key = LiveKey();
    d.key.sessionId = 1; d.key.timelineEpoch = 1;
    for (RegionId id : { RegionId::Lwram, RegionId::Hwram })
    {
        ByteBlob b = Bytes(Traits(id).size, static_cast<uint8_t>(id) + 1);
        d.regions[static_cast<size_t>(id)] = b;
        d.key.contentSha256[RegionApiName(id)] = Sha256Hex(b->data(), b->size());
    }
    d.tracking.valid = true;
    d.tracking.captureSerial = 4;
    d.tracking.sessionId = 1; d.tracking.timelineEpoch = 1; d.tracking.connectionGeneration = 3;
    d.registers.available = true;
    d.registers.cpu[0].valid = true;
    d.registers.cpu[0].pc = 0x06004000;
    d.stop.available = true;
    d.stop.stopReason = 1;
    d.stop.pc = 0x06004000;
    d.stop.stopSeq = 17;
    d.callStacks.available = true;
    d.callStacks.cpu[0].push_back({ 0x06004100, 0x06004000, 0x06004102, 0x06FFF000, FrameConfidence::Confirmed });
    return d;
}

std::string Validate(CaptureData d) { return ValidateCapture(OwnedCapture(std::move(d))); }

void TestCaptureValidation()
{
    Check(Validate(LiveCapture()).empty(), "a consistent live capture validates");

    CaptureData d = LiveCapture();
    d.regions[static_cast<size_t>(RegionId::Hwram)] = Bytes(Traits(RegionId::Hwram).size, 99);
    Check(Has(Validate(d), "hwram does not hash"), "bytes that do not match the key's hash");
    d = LiveCapture();
    d.regions[static_cast<size_t>(RegionId::Hwram)] = nullptr;
    Check(Has(Validate(d), "no bytes for it"), "a region the key names but the capture lacks");
    d = LiveCapture();
    d.regions[static_cast<size_t>(RegionId::Vdp2Ram)] = Bytes(Traits(RegionId::Vdp2Ram).size, 1);
    Check(Has(Validate(d), "does not name it"), "bytes the key does not account for");
    d = LiveCapture();
    d.regions[static_cast<size_t>(RegionId::Lwram)] = Bytes(100, 1);
    Check(Has(Validate(d), "not the region's size"), "a short region");
    d = LiveCapture();
    d.key.frame = "0";
    Check(!Validate(d).empty(), "an invalid key fails the capture");

    d = LiveCapture();
    d.tracking.sessionId = 2;
    Check(Has(Validate(d), "session"), "tracking from another session");
    d = LiveCapture();
    d.tracking.connectionGeneration = 4;
    Check(Has(Validate(d), "connection"), "tracking from another connection");
    d = LiveCapture();
    d.tracking.captureSerial = 0;
    Check(Has(Validate(d), "serial"), "valid tracking without a serial");
    d = LiveCapture();
    d.tracking = WriteCheckpoint();
    Check(Validate(d).empty(), "a capture with no tracking is allowed; it just classifies as unknown");

    // Unavailable items say why and carry nothing.
    d = LiveCapture();
    d.registers = CaptureRegisters();
    Check(Has(Validate(d), "registers give no reason"), "unavailable registers need a reason");
    d.registers.reason = "the snapshot did not retain registers";
    Check(Validate(d).empty(), "with one they validate");
    d.registers.cpu[1].valid = true;
    Check(Has(Validate(d), "carry data"), "unavailable registers must not carry values");
    d = LiveCapture();
    d.registers.cpu[0].valid = false;
    Check(Has(Validate(d), "neither CPU"), "available registers need a valid CPU");
    d = LiveCapture();
    d.callStacks = CaptureCallStacks();
    Check(Has(Validate(d), "call stacks give no reason"), "unavailable call stacks need a reason");
    d = LiveCapture();
    d.stop = CaptureStop();
    Check(Has(Validate(d), "stop gives no reason"), "an unavailable stop needs a reason");
}

void TestFileCapture()
{
    CaptureData d;
    d.file = Bytes(100, 3);
    d.key = FileKey();
    d.key.contentSha256[kFileContentName] = Sha256Hex(d.file->data(), d.file->size());
    d.registers.reason = d.stop.reason = d.callStacks.reason = "a file has none";
    Check(Validate(d).empty(), "a file capture validates");

    CaptureData bad = d;
    bad.file = Bytes(100, 4);
    Check(Has(Validate(bad), "do not hash"), "file bytes that do not match the key");
    bad = d;
    bad.file = nullptr;
    Check(Has(Validate(bad), "no bytes"), "a file capture without bytes");
    bad = d;
    bad.regions[0] = Bytes(Traits(RegionId::Lwram).size, 1);
    Check(Has(Validate(bad), "no emulator regions"), "a file capture holds no emulator regions");
    bad = d;
    bad.registers.available = true;
    Check(Has(Validate(bad), "no registers"), "a file capture has no registers");

    OwnedCapture c(d);
    Check(!Availability(c, CaptureItem::Memory).available && Availability(c, CaptureItem::FileBytes).available &&
              !Availability(c, CaptureItem::Registers).available && !Availability(c, CaptureItem::WriteTracking).available,
          "item availability for a file");
}

void TestAvailability()
{
    CaptureData d = LiveCapture();
    d.registers = CaptureRegisters();
    d.registers.reason = "recorded before register capture existed";
    OwnedCapture c(d);
    const ItemAvailability regs = Availability(c, CaptureItem::Registers);
    Check(!regs.available && regs.reason == "recorded before register capture existed",
          "a capture without registers reports them unavailable, with its reason");
    Check(Availability(c, CaptureItem::Memory).available && Availability(c, CaptureItem::Stop).available &&
              Availability(c, CaptureItem::CallStacks).available && Availability(c, CaptureItem::WriteTracking).available,
          "and the rest are available");
    Check(!Availability(c, CaptureItem::FileBytes).available, "a memory capture has no file bytes");
    // Never back-filled: asking again returns the same thing.
    Check(c.Registers().available == false && !c.Registers().cpu[0].valid && c.RegionBytes(RegionId::Lwram) != nullptr &&
              c.RegionBytes(RegionId::Vdp1Ram) == nullptr, "an absent region stays absent");
}

void TestGuard()
{
    CaptureGuardSample a;
    a.sourceId = 1; a.deriveSerial = 5; a.connectionGeneration = 2; a.frame = 100;
    CaptureGuardSample b = a;
    Check(GuardUnchanged(a, b), "identical samples");
    b.deriveSerial = 6;
    Check(!GuardUnchanged(a, b), "an edit during capture (derive serial moved) rejects it");
    b = a; b.sourceId = 2;
    Check(!GuardUnchanged(a, b), "a different source");
    b = a; b.connectionGeneration = 3;
    Check(!GuardUnchanged(a, b), "a reconnect");
    b = a; b.frame = 101;
    Check(!GuardUnchanged(a, b), "the frame advanced");
}
}  // namespace

int main()
{
    TestKeyRules();
    TestJson();
    TestSameContent();
    TestCaptureValidation();
    TestFileCapture();
    TestAvailability();
    TestGuard();
    if (gFail == 0) std::printf("SourceKeyTests: all passed\n");
    return gFail == 0 ? 0 : 1;
}
