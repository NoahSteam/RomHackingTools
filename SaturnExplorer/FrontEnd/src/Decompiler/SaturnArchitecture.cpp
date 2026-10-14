#include "Decompiler/SaturnArchitecture.h"

#include "Decompiler/SpecBundle.h"

#include "libdecomp.hh"

#include <mutex>
#include <vector>

namespace sfe
{
namespace decomp
{

SaturnArchitecture::SaturnArchitecture(std::shared_ptr<const MemorySnapshot> snapshot, std::ostream* errors)
    : ghidra::SleighArchitecture("saturn-capture", "SuperH:BE:32:SH-2:default", errors),
      mDataImage(new CaptureLoadImage(snapshot, CaptureLoadImage::Use::Data)),
      mFetchImage(new CaptureLoadImage(snapshot, CaptureLoadImage::Use::Fetch))
{
}

void SaturnArchitecture::Initialise(ghidra::uint4 maxInstructions, std::atomic<bool>* cancel)
{
    ghidra::DocumentStorage store;
    init(store);
    // After init: init resets the options to their defaults.
    readonlypropagate = true;
    max_instructions = maxInstructions;
    // Every fetch from memory that is not captured costs one "bad data" halt, and Ghidra's
    // default allowance of four fails the whole function on the fifth. Flow is already bounded by
    // max_instructions, so the allowance is too: each such halt ends one path, it never runs on.
    max_baddata = maxInstructions;
    cancelRequested = cancel;
    // A compiler drops a load whose value is unused unless it is volatile, so one that remains is
    // there for its effect -- a dummy read that clears a flag, a FIFO pop. The engine only keeps
    // it when it can already see the address is constant; a pointer that folds later (or never)
    // lost the read. Every unused load is held until its pointer is a constant: then the address's
    // volatility decides (hardware stays, ordinary memory goes), and one that never resolves
    // stays, since it may be hardware (patches/0009-keep-unused-loads.patch).
    keepUnusedLoads = true;
}

void SaturnArchitecture::buildLoader(ghidra::DocumentStorage&)
{
    collectSpecFiles(*errorstream);
    loader = mDataImage.release();
}

// The translator decodes instructions through the loader it is built with. Building it with the
// fetch image (and restoring the data image as the Architecture's own loader) is what gives
// instruction fetches and data reads their different refusals -- see CaptureLoadImage.h.
namespace
{
void (*gBuildTranslatorHook)() = nullptr;
}  // namespace

void SaturnArchitecture::SetBuildTranslatorHookForTests(void (*hook)())
{
    gBuildTranslatorHook = hook;
}

ghidra::Translate* SaturnArchitecture::buildTranslator(ghidra::DocumentStorage& store)
{
    if (gBuildTranslatorHook) gBuildTranslatorHook();
    ghidra::LoadImage* data = loader;
    loader = mFetchImage.get();
    ghidra::Translate* t = nullptr;
    try
    {
        t = ghidra::SleighArchitecture::buildTranslator(store);
    }
    catch (...)
    {
        loader = data;
        throw;
    }
    loader = data;
    return t;
}

void SaturnArchitecture::resolveArchitecture()
{
    archid = getTarget();
    ghidra::SleighArchitecture::resolveArchitecture();
}

// The base class reads a <processor_spec> from the load image's XML, which a raw memory image
// does not have.
void SaturnArchitecture::postSpecFile()
{
    ghidra::Architecture::postSpecFile();
}

bool SaturnArchitecture::StartLibrary(const std::string& configDir, std::string& error)
{
    // The library's registries and spec path are process-global; registering the directory twice
    // would only lengthen the search, but a second, different directory would make which files
    // win depend on order. So the first outcome stands for the process.
    static std::mutex m;
    static bool started = false;
    static bool ok = false;
    static std::string firstError;
    std::lock_guard<std::mutex> lock(m);
    if (!started)
    {
        started = true;
        const SpecBundleResult bundle = MaterialiseSh2SpecBundle(configDir);
        ok = bundle.ok;
        if (ok) ghidra::startDecompilerLibrary(std::vector<std::string>{ bundle.dir });
        else firstError = bundle.error;
    }
    error = firstError;
    return ok;
}

}  // namespace decomp
}  // namespace sfe
