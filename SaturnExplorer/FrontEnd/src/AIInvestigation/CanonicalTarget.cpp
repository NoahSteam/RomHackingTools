#include "AIInvestigation/CanonicalTarget.h"

#include <cctype>
#include <vector>

#include "AIInvestigation/Encoding.h"
#include "Sh2Address.h"

namespace sfe
{
namespace ai
{
namespace
{

std::string Lowered(const char* name)
{
    std::string out;
    for (const char* p = name; *p; ++p)
        if (*p != ' ') out += static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
    return out;
}

const std::string& ApiName(size_t i)
{
    static const std::vector<std::string> names = [] {
        std::vector<std::string> n;
        for (size_t k = 0; k < kRegionCount; ++k) n.push_back(Lowered(Traits(static_cast<RegionId>(k)).name));
        return n;
    }();
    return names[i];
}

TargetResult Fail(TargetError e, std::string message)
{
    TargetResult r;
    r.error = e;
    r.message = std::move(message);
    return r;
}

TargetResult Ok(const CanonicalTarget& t)
{
    TargetResult r;
    r.ok = true;
    r.target = t;
    return r;
}

}  // namespace

const char* RegionApiName(RegionId id)
{
    return ApiName(static_cast<size_t>(id)).c_str();
}

bool RegionFromApiName(const std::string& name, RegionId* out)
{
    for (size_t i = 0; i < kRegionCount; ++i)
    {
        if (ApiName(i) == name)
        {
            if (out) *out = static_cast<RegionId>(i);
            return true;
        }
    }
    return false;
}

TargetResult CanonicalizeBusAddress(uint32_t busAddress)
{
    uint32_t canonical = 0;
    std::string why;
    if (!CanonicalAddress(busAddress, canonical, &why)) return Fail(TargetError::NotMemory, why);

    for (size_t i = 0; i < kRegionCount; ++i)
    {
        const RegionTraits& t = Traits(static_cast<RegionId>(i));
        if (canonical < t.busBase || canonical - t.busBase >= t.size) continue;
        if (!HasBusAddress(t))
            return Fail(TargetError::NoBusAddress,
                        FormatAddress(busAddress) + " is in the " + RegionApiName(t.id) +
                        " window, which is an app-derived image with no bus address; use {\"region\": \"" +
                        RegionApiName(t.id) + "\", \"offset\": ...}");
        CanonicalTarget c;
        c.region = t.id;
        c.offset = canonical - t.busBase;
        c.hasBus = true;
        c.bus = canonical;
        return Ok(c);
    }
    return Fail(TargetError::Unmapped, FormatAddress(busAddress) + " is not inside any captured region");
}

TargetResult CanonicalizeRegionOffset(RegionId region, uint32_t offset)
{
    const RegionTraits& t = Traits(region);
    if (offset >= t.size)
        return Fail(TargetError::OffsetOutOfRange,
                    "offset " + FormatHex(offset) + " is outside " + RegionApiName(region) +
                    " (size " + FormatHex(t.size) + ")");
    CanonicalTarget c;
    c.region = region;
    c.offset = offset;
    c.hasBus = HasBusAddress(t);
    c.bus = c.hasBus ? t.busBase + offset : 0u;
    return Ok(c);
}

TargetResult ResolveTarget(const TargetInput& in)
{
    if (in.hasRegion != in.hasOffset)
        return Fail(TargetError::Malformed, "\"region\" and \"offset\" go together");
    if (!in.hasBus && !in.hasRegion)
        return Fail(TargetError::Missing, "a target needs \"bus_address\" or \"region\" and \"offset\"");

    TargetResult fromBus, fromRegion;
    if (in.hasBus)
    {
        fromBus = CanonicalizeBusAddress(in.bus);
        if (!fromBus.ok) return fromBus;
    }
    if (in.hasRegion)
    {
        fromRegion = CanonicalizeRegionOffset(in.region, in.offset);
        if (!fromRegion.ok) return fromRegion;
    }
    if (in.hasBus && in.hasRegion && fromBus.target != fromRegion.target)
        return Fail(TargetError::Mismatch,
                    "bus_address " + FormatAddress(in.bus) + " is " + TargetToString(fromBus.target) +
                    " but region/offset is " + TargetToString(fromRegion.target));
    return in.hasBus ? fromBus : fromRegion;
}

TargetResult ResolveTargetJson(const JsonValue& target)
{
    if (!target.IsObject()) return Fail(TargetError::Malformed, "a target must be an object");
    TargetInput in;
    for (const JsonValue::Member& m : target.members())
    {
        const std::string& key = m.first;
        if (key != "bus_address" && key != "region" && key != "offset")
            return Fail(TargetError::Malformed, "unknown target field \"" + key + "\"");
        if (!m.second.IsString())
            return Fail(TargetError::Malformed, "\"" + key + "\" must be a string");
    }
    std::string why;
    if (const JsonValue* v = target.Find("bus_address"))
    {
        if (!ParseHex32(v->AsString(), &in.bus, &why))
            return Fail(TargetError::BadEncoding, "bus_address: " + why);
        in.hasBus = true;
    }
    if (const JsonValue* v = target.Find("offset"))
    {
        if (!ParseHex32(v->AsString(), &in.offset, &why))
            return Fail(TargetError::BadEncoding, "offset: " + why);
        in.hasOffset = true;
    }
    if (const JsonValue* v = target.Find("region"))
    {
        if (!RegionFromApiName(v->AsString(), &in.region))
            return Fail(TargetError::UnknownRegion, "unknown region \"" + v->AsString() +
                        "\" (regions are lower case: lwram, hwram, soundram, vdp1ram, vdp1fb, vdp1regs, vdp2ram, vdp2cram, vdp2regs)");
        in.hasRegion = true;
    }
    return ResolveTarget(in);
}

bool RangeFits(const CanonicalTarget& target, uint32_t length)
{
    if (length == 0) return false;
    const uint32_t size = Traits(target.region).size;
    return target.offset < size && length <= size - target.offset;
}

bool HasCaps(const CanonicalTarget& target, uint8_t caps)
{
    return (Traits(target.region).caps & caps) == caps;
}

JsonValue TargetToJson(const CanonicalTarget& t)
{
    JsonValue o = JsonValue::Object();
    o.Set("region", JsonValue::String(RegionApiName(t.region)));
    o.Set("offset", JsonValue::String(FormatHex(t.offset)));
    if (t.hasBus) o.Set("bus_address", JsonValue::String(FormatAddress(t.bus)));
    return o;
}

std::string TargetToString(const CanonicalTarget& t)
{
    std::string s = std::string(RegionApiName(t.region)) + "+" + FormatHex(t.offset);
    if (t.hasBus) s += " (" + FormatAddress(t.bus) + ")";
    return s;
}

}  // namespace ai
}  // namespace sfe
