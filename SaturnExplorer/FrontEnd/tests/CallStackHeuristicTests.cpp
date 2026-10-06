// What the heuristic reconstructor claims to know, and what it does not.
//
// The stack walk recovers return addresses. A return address is inside the *caller*, four bytes
// past the call -- not at the callee's entry point -- so the reconstructor used to fill
// functionAddress with it and the UI named the frame "sub_06XXXXXX" after an address that is not
// a function start (CPU-03). The entry point is recoverable from a `bsr disp`, which encodes its
// target, and is not recoverable from a `jsr @Rn` or `bsrf Rn`, where it was in a register the
// stack image no longer holds.
#include "Debug/CallStack.h"

#include <cstdio>
#include <map>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, const char* what, int line)
{
    if (ok) return;
    std::printf("CHECK failed at line %d: %s\n", line, what);
    ++gFailures;
}

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

// A sparse big-endian memory: only what a test writes is readable, so an unwritten address
// fails the read exactly as an unmapped one does on the real backend.
class FakeMemory : public sfe::IMemoryBackend
{
public:
    bool Connected() const override { return true; }

    void PutU32(uint32_t address, uint32_t value)
    {
        for (int i = 0; i < 4; ++i)
            mBytes[address + i] = static_cast<uint8_t>(value >> (24 - 8 * i));
    }
    void PutU16(uint32_t address, uint16_t value)
    {
        mBytes[address]     = static_cast<uint8_t>(value >> 8);
        mBytes[address + 1] = static_cast<uint8_t>(value);
    }

    std::vector<sfe::MemoryReadResult> ReadMemoryBatch(
        const std::vector<sfe::MemoryReadRequest>& requests) override
    {
        std::vector<sfe::MemoryReadResult> out;
        out.reserve(requests.size());
        for (const sfe::MemoryReadRequest& r : requests)
        {
            sfe::MemoryReadResult res;
            // A batch read of a span is served with zeroes where nothing was written -- the real
            // backend returns the whole region too. A read whose *first* byte is unwritten is
            // the "unmapped" case and fails, which is how the opcode probes are made to fail.
            if (mBytes.count(r.address) == 0 && r.size <= 4)
            {
                res.success = false;
                res.error = "unmapped";
            }
            else
            {
                res.success = true;
                res.bytes.resize(r.size);
                for (uint32_t i = 0; i < r.size; ++i)
                {
                    auto it = mBytes.find(r.address + i);
                    res.bytes[i] = (it == mBytes.end()) ? 0 : it->second;
                }
            }
            out.push_back(res);
        }
        return out;
    }

private:
    std::map<uint32_t, uint8_t> mBytes;
};

const uint32_t kSp  = 0x06040000;   // HWRAM, so the walked words are plausible code addresses
const uint32_t kRet = 0x06001000;

se_sh2_regs Regs()
{
    se_sh2_regs regs{};
    regs.pc = 0x06002000;
    regs.pr = 0x06003000;
    regs.r[15] = kSp;
    return regs;
}

// `bsr disp` encodes its target: site + 4 + (sign-extended disp * 2).
void TestBsrCallSiteYieldsRealEntryPoint()
{
    FakeMemory mem;
    mem.PutU32(kSp, kRet);
    // disp = +0x10 -> target = (kRet - 4) + 4 + 0x20
    mem.PutU16(kRet - 4, static_cast<uint16_t>(0xB000u | 0x0010u));

    sfe::CallStack cs;
    cs.Reconstruct(0, Regs(), mem);
    const std::vector<sfe::CallStackFrame>& frames = cs.Frames(0);
    CHECK(frames.size() == 2);
    if (frames.size() < 2) return;

    const sfe::CallStackFrame& fr = frames[1];
    CHECK(fr.confidence == sfe::FrameConfidence::Probable);
    CHECK(fr.functionKnown);
    CHECK(fr.functionAddress == kRet + 0x20);
    CHECK(fr.returnAddress == kRet);
    CHECK(fr.currentAddress == kRet);
}

// A negative displacement -- the common shape, a call backwards to a helper.
void TestBsrBackwardsDisplacement()
{
    FakeMemory mem;
    mem.PutU32(kSp, kRet);
    mem.PutU16(kRet - 4, static_cast<uint16_t>(0xB000u | 0x0FF0u));   // disp = -16

    sfe::CallStack cs;
    cs.Reconstruct(0, Regs(), mem);
    const std::vector<sfe::CallStackFrame>& frames = cs.Frames(0);
    CHECK(frames.size() == 2);
    if (frames.size() < 2) return;
    CHECK(frames[1].functionKnown);
    CHECK(frames[1].functionAddress == kRet - 32);
}

// `jsr @Rn` called through a register. The frame is still a real frame -- the opcode confirms
// the return address -- but nothing here knows where it went, and saying kRet would be naming a
// function after an address four bytes past a call instruction in someone else's body.
void TestJsrLeavesEntryPointUnknown()
{
    FakeMemory mem;
    mem.PutU32(kSp, kRet);
    mem.PutU16(kRet - 4, 0x430Bu);   // jsr @R3

    sfe::CallStack cs;
    cs.Reconstruct(0, Regs(), mem);
    const std::vector<sfe::CallStackFrame>& frames = cs.Frames(0);
    CHECK(frames.size() == 2);
    if (frames.size() < 2) return;

    const sfe::CallStackFrame& fr = frames[1];
    CHECK(fr.confidence == sfe::FrameConfidence::Probable);
    CHECK(!fr.functionKnown);
    CHECK(fr.functionAddress == 0);
    CHECK(fr.currentAddress == kRet);   // still navigable, just not as an entry point
}

// `bsrf Rn` adds a register to PC, so it is in the same position as jsr.
void TestBsrfLeavesEntryPointUnknown()
{
    FakeMemory mem;
    mem.PutU32(kSp, kRet);
    mem.PutU16(kRet - 4, 0x0203u);   // bsrf R2

    sfe::CallStack cs;
    cs.Reconstruct(0, Regs(), mem);
    const std::vector<sfe::CallStackFrame>& frames = cs.Frames(0);
    CHECK(frames.size() == 2);
    if (frames.size() < 2) return;
    CHECK(!frames[1].functionKnown);
}

// Frame #0 is where execution stopped, which is not where its function began either. PC is
// reported as the address the frame is at; the entry point is left unclaimed.
void TestFrameZeroDoesNotClaimAnEntryPoint()
{
    FakeMemory mem;
    sfe::CallStack cs;
    const se_sh2_regs regs = Regs();
    cs.Reconstruct(0, regs, mem);
    const std::vector<sfe::CallStackFrame>& frames = cs.Frames(0);
    CHECK(!frames.empty());
    if (frames.empty()) return;
    CHECK(frames[0].confidence == sfe::FrameConfidence::Confirmed);
    CHECK(!frames[0].functionKnown);
    CHECK(frames[0].currentAddress == regs.pc);
    CHECK(frames[0].returnAddress == regs.pr);
}

}  // namespace

// ---- Step Out ----------------------------------------------------------------------------------
// PR holds the return address only until the function makes a call of its own. The target has to
// come from the recorded frame, and say so when there is none.

sfe::CallStackFrame ConfirmedFrame(uint32_t ret, uint32_t sp)
{
    sfe::CallStackFrame f;
    f.confidence = sfe::FrameConfidence::Confirmed;
    f.returnAddress = ret;
    f.stackPointer = sp;
    return f;
}

se_sh2_regs RegsAt(uint32_t pc, uint32_t pr, uint32_t r15)
{
    se_sh2_regs r{};
    r.pc = pc;
    r.pr = pr;
    r.r[15] = r15;
    return r;
}

void TestStepOutUsesTheRecordedFrameNotPr()
{
    // In a non-leaf function after a nested call returned: PR now holds the nested call's return
    // address, which is INSIDE this function (even the current PC); the real return was recorded.
    const uint32_t ret = 0x06001010u, sp = 0x06080000u;
    const std::vector<sfe::CallStackFrame> frames = { ConfirmedFrame(ret, sp) };
    const se_sh2_regs regs = RegsAt(/*pc*/ 0x06002040u, /*pr*/ 0x06002040u, /*r15*/ 0x0607FFE0u);

    const sfe::StepOutTarget t = sfe::ChooseStepOutTarget(frames, regs);
    CHECK(t.ok);
    CHECK(t.returnAddress == ret);          // not PR (== the current PC here)
}

void TestStepOutRefusesWhatItCannotRecover()
{
    const se_sh2_regs regs = RegsAt(0x06002040u, 0x06001010u, 0x0607FFE0u);

    // Nothing recorded for this CPU.
    CHECK(!sfe::ChooseStepOutTarget({}, regs).ok);

    // A heuristic frame #0 returns to PR by construction: exactly the guess this must not make.
    sfe::CallStackFrame heur;
    heur.confidence = sfe::FrameConfidence::Probable;
    heur.returnAddress = regs.pr;
    heur.stackPointer = 0x06080000u;
    const sfe::StepOutTarget h = sfe::ChooseStepOutTarget({ heur }, regs);
    CHECK(!h.ok);
    CHECK(h.why[0] != '\0');               // and it says why

    // A recorded return that is the current instruction would halt at once.
    CHECK(!sfe::ChooseStepOutTarget({ ConfirmedFrame(regs.pc, 0x06080000u) }, regs).ok);
    // Not code.
    CHECK(!sfe::ChooseStepOutTarget({ ConfirmedFrame(0x00000010u, 0x06080000u) }, regs).ok);
    CHECK(!sfe::ChooseStepOutTarget({ ConfirmedFrame(0x06001011u, 0x06080000u) }, regs).ok);   // odd
    // R15 above the stack the call was made at: the callee runs BELOW it, so this recording is
    // not of this frame.
    const se_sh2_regs high = RegsAt(0x06002040u, 0x06001010u, 0x06081000u);
    CHECK(!sfe::ChooseStepOutTarget({ ConfirmedFrame(0x06001010u, 0x06080000u) }, high).ok);
    // A call's delay slot can pop (`jsr @r1 / add #4,r15`), putting the callee a few bytes ABOVE the
    // recorded stack. That is still this frame.
    const se_sh2_regs popped = RegsAt(0x06002040u, 0x06001010u, 0x06080008u);
    CHECK(sfe::ChooseStepOutTarget({ ConfirmedFrame(0x06001010u, 0x06080000u) }, popped).ok);
    // At the same depth (a leaf that never pushed) is fine.
    const se_sh2_regs same = RegsAt(0x06002040u, 0x06001010u, 0x06080000u);
    CHECK(sfe::ChooseStepOutTarget({ ConfirmedFrame(0x06001010u, 0x06080000u) }, same).ok);
}

int main()
{
    TestStepOutUsesTheRecordedFrameNotPr();
    TestStepOutRefusesWhatItCannotRecover();
    TestBsrCallSiteYieldsRealEntryPoint();
    TestBsrBackwardsDisplacement();
    TestJsrLeavesEntryPointUnknown();
    TestBsrfLeavesEntryPointUnknown();
    TestFrameZeroDoesNotClaimAnEntryPoint();
    if (gFailures)
    {
        std::printf("CallStackHeuristicTests: %d check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("CallStackHeuristicTests: all checks passed\n");
    return 0;
}
