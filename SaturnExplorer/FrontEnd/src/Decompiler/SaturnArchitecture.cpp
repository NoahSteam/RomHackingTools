#include "Decompiler/SaturnArchitecture.h"

#include "Decompiler/SpecBundle.h"

#include "libdecomp.hh"

#include <mutex>
#include <vector>

namespace sfe
{
namespace decomp
{

SaturnArchitecture::SaturnArchitecture(CaptureLoadImage* image, std::ostream* errors)
    : ghidra::SleighArchitecture("saturn-capture", "SuperH:BE:32:SH-2:default", errors), mImage(image)
{
}

void SaturnArchitecture::Initialise(ghidra::uint4 maxInstructions, std::atomic<bool>* cancel)
{
    ghidra::DocumentStorage store;
    init(store);
    // After init: init resets the options to their defaults.
    readonlypropagate = true;
    max_instructions = maxInstructions;
    cancelRequested = cancel;
}

void SaturnArchitecture::buildLoader(ghidra::DocumentStorage&)
{
    collectSpecFiles(*errorstream);
    loader = mImage;
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
