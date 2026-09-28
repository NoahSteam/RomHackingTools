// The Seam B no-throw contract (SeHost.h, "Exceptions"). A host on the other side of the seam
// may be C, or JavaScript in the web build, so an exception that escapes an se_* function is
// undefined behaviour rather than a catchable error -- and the C++ frontend installs no handler
// either, so it is a crash there too.
//
// Testing that needs a failing allocation, and the portable way to get one is to replace the
// global operator new for this program: the flag below is switched on for exactly the duration
// of one ABI call, so only the core's allocations fail, not the harness's own.
//
// Note what this does NOT assert: that the core copes gracefully. It may well leave a partial
// snapshot behind. The contract is only that the failure comes back as a value.
//
// Only the se_result entry points are asserted on, because they are the only ones that allocate
// today -- every count-returning query answers out of state the snapshot already built. Their
// fallback is still in place for the first one that grows an allocation, which is the point of
// guarding uniformly rather than per call graph; there is just nothing here yet to trigger it.
#include "saturnexplorer/SeHost.h"

#include <cstdio>
#include <cstdlib>
#include <new>
#include <stdexcept>
#include <vector>

#include "FakeVdpSource.h"

namespace {

int gFailures = 0;

void Check(bool ok, const char* what, int line)
{
    if (ok) return;
    std::printf("CHECK failed at line %d: %s\n", line, what);
    ++gFailures;
}

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

bool gStarveAllocations = false;

// Counts what the guarded call attempted, so a test can tell "returned the no-memory code
// because the guard caught a throw" from "returned it without ever allocating", which would
// make the test pass for the wrong reason.
int gRefusedAllocations = 0;

// Switches allocation failure on for one call and off again however that call ends.
struct Starve
{
    Starve()
    {
        gRefusedAllocations = 0;
        gStarveAllocations = true;
    }
    ~Starve() { gStarveAllocations = false; }
};

}  // namespace

// Replacing these is well-defined: they are the standard's replaceable allocation functions.
// The throwing forms are the ones the core reaches through std::vector.
void* operator new(std::size_t n)
{
    if (gStarveAllocations)
    {
        ++gRefusedAllocations;
        throw std::bad_alloc();
    }
    void* p = std::malloc(n != 0 ? n : 1);
    if (!p) throw std::bad_alloc();
    return p;
}

void* operator new[](std::size_t n) { return operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {

// se_begin_frame snapshots every region the source offers, which is the largest allocation the
// core makes on any single call -- and the one a host hits first.
void TestBeginFrameReportsAllocationFailure()
{
    se_test::State state(0x1000);
    se_test::WriteSystemClip(state, 320, 224);
    se_context* ctx = se_test::CreateContext(state);
    CHECK(ctx != nullptr);
    if (!ctx) return;

    se_result r;
    {
        Starve starve;
        r = se_begin_frame(ctx);
    }
    CHECK(gRefusedAllocations > 0);      // it really did try to allocate
    CHECK(r == SE_ERR_NO_MEMORY);

    // And the context is still usable afterwards: the guard returns, it does not poison state.
    CHECK(se_begin_frame(ctx) == SE_OK);
    se_destroy(ctx);
}

// se_render_frame composites into a caller-owned buffer, but the rasterizer still allocates
// scratch of its own on the way -- so this covers the second shape of the contract: a failure
// after the call has already begun producing output, rather than one at the first allocation.
//
// Passing pixels == NULL is the sizing call and allocates nothing, which is why this hands it a
// real buffer. An earlier version of this test did not, and passed while reaching no allocation
// at all; gRefusedAllocations is checked to keep that from happening again silently.
void TestRenderFrameReportsAllocationFailure()
{
    se_test::State state(0x1000);
    se_test::WriteSystemClip(state, 320, 224);
    se_context* ctx = se_test::CreateContext(state);
    CHECK(ctx != nullptr);
    if (!ctx) return;
    CHECK(se_begin_frame(ctx) == SE_OK);

    se_render_opts opts;
    se_default_render_opts(&opts);
    se_image image = {};
    size_t needed = 0;
    CHECK(se_render_frame(ctx, &opts, &image, &needed) == SE_OK);
    CHECK(needed > 0);

    std::vector<uint8_t> pixels(needed);
    image.pixels = pixels.data();
    image.capacity = pixels.size();

    se_result rendered;
    {
        Starve starve;
        rendered = se_render_frame(ctx, &opts, &image, &needed);
    }
    CHECK(gRefusedAllocations > 0);
    CHECK(rendered == SE_ERR_NO_MEMORY);

    se_destroy(ctx);
}

// se_create's object comes from nothrow new, but se::Context's constructor allocates, and a
// throw from there propagates out of the new-expression rather than yielding null.
void TestCreateReturnsNullOnAllocationFailure()
{
    se_test::State state(0x1000);
    se_data_source source = se_test::MakeSource(state);
    se_config config = {};
    config.abi_version = SE_ABI_VERSION;

    se_context* ctx;
    {
        Starve starve;
        ctx = se_create(&source, &config);
    }
    CHECK(gRefusedAllocations > 0);
    CHECK(ctx == nullptr);
    se_destroy(ctx);   // and null is safe to pass back in
}

// --- A throwing driver callback, which is the other half of the contract ----------------------
//
// Allocation failure is not the only way an exception reaches the seam: the driver on the far side
// of Seam A is foreign C++ code, and its callbacks can throw on their own. The review that caught
// this was right that testing only operator new left the blanket claim unpinned -- and it found two
// entry points (se_get_system_status, se_frame_number) that called a callback from inside an `if`
// and then returned a constant, so a sweep that wrapped only each function's final return left the
// callback outside the guard.

struct Thrower
{
    static int Status(void*, se_system_status*) { throw std::runtime_error("driver"); }
    static uint64_t FrameNumber(void*) { throw std::runtime_error("driver"); }
    static int FramePause(void*) { throw std::runtime_error("driver"); }
    static int FrameStep(void*, int32_t) { throw std::runtime_error("driver"); }
    static int LoadState(void*, uint64_t, const void*, size_t, const void*, size_t)
    { throw std::runtime_error("driver"); }
    static void Close(void*) { throw std::runtime_error("driver"); }
};

// se_get_system_status and se_frame_number reach their callback before any wrapped return.
void TestThrowingStatusAndFrameCallbacks()
{
    se_test::State state(0x1000);
    se_test::WriteSystemClip(state, 320, 224);
    se_data_source source = se_test::MakeSource(state);
    source.capabilities |= SE_CAP_SYSTEM_STATUS | SE_CAP_FRAME_STEP;
    source.get_system_status = &Thrower::Status;
    source.frame_number = &Thrower::FrameNumber;
    source.frame_pause = &Thrower::FramePause;
    source.frame_step = &Thrower::FrameStep;

    se_context* ctx = se_test::CreateContext(source);
    CHECK(ctx != nullptr);
    if (!ctx) return;

    se_system_status status = {};
    CHECK(se_get_system_status(ctx, &status) == SE_ERR_NO_MEMORY);
    CHECK(se_frame_number(ctx) == 0);
    CHECK(se_frame_pause(ctx) == SE_ERR_NO_MEMORY);
    CHECK(se_frame_resume(ctx) == SE_ERR_NO_MEMORY);
    CHECK(se_frame_step(ctx, 1) == SE_ERR_NO_MEMORY);

    se_destroy(ctx);
}

// The rewind path, which reaches its callback the same way.
void TestThrowingLoadStateCallback()
{
    se_test::State state(0x1000);
    se_test::WriteSystemClip(state, 320, 224);
    se_data_source source = se_test::MakeSource(state);
    source.capabilities |= SE_CAP_STATE_REWIND;
    source.load_state = &Thrower::LoadState;

    se_context* ctx = se_test::CreateContext(source);
    CHECK(ctx != nullptr);
    if (!ctx) return;
    CHECK(se_supports_state_rewind(ctx) == 1);
    const uint8_t blob[4] = { 1, 2, 3, 4 };
    CHECK(se_load_state(ctx, 0, blob, sizeof(blob), nullptr, 0) == SE_ERR_NO_MEMORY);
    se_destroy(ctx);
}

// The one that terminates rather than returning if it is unguarded: ~Context calls the driver's
// close, and a destructor is implicitly noexcept, so a throw out of it aborts the process before
// any handler in HostAbi.cpp exists to catch it. Reaching the line after se_destroy is the whole
// assertion -- the live driver's close joins a thread, and std::thread::join throws
// std::system_error when the OS refuses, so this is a reachable path.
void TestThrowingCloseCallbackDoesNotTerminate()
{
    se_test::State state(0x1000);
    se_test::WriteSystemClip(state, 320, 224);
    se_data_source source = se_test::MakeSource(state);
    source.close = &Thrower::Close;

    se_context* ctx = se_test::CreateContext(source);
    CHECK(ctx != nullptr);
    if (!ctx) return;
    CHECK(se_begin_frame(ctx) == SE_OK);
    se_destroy(ctx);
    CHECK(true && "se_destroy returned instead of terminating");
}

}  // namespace

int main()
{
    TestThrowingStatusAndFrameCallbacks();
    TestThrowingLoadStateCallback();
    TestThrowingCloseCallbackDoesNotTerminate();
    TestBeginFrameReportsAllocationFailure();
    TestRenderFrameReportsAllocationFailure();
    TestCreateReturnsNullOnAllocationFailure();
    if (gFailures == 0) std::printf("AbiNoThrowTests: all checks passed\n");
    return gFailures == 0 ? 0 : 1;
}
