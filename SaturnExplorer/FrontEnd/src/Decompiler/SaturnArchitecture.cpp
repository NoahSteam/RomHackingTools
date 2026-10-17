#include "Decompiler/SaturnArchitecture.h"

#include "Decompiler/SpecBundle.h"

#include "libdecomp.hh"

#include <mutex>
#include <new>
#include <string>
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
    // ...and a load is read once in the output, as in the code: the engine prints a cheap
    // expression at each of its uses, so a sample taken in a poll loop and then returned would
    // print as a second dereference after the loop (patches/0010-load-not-duplicated.patch).
    loadsNotDuplicated = true;
    // An SH-2 instruction starts at an even address. The specification says alignment 1 (one value for every SuperH), so the translator decodes
    // at an odd address, and a jump table entry that is no address (a word of data the table's bound does not exclude) sent flow into the middle of
    // the instructions around it: an "overlaps instruction" warning, odd token addresses, text made of the instruction that is not there. Flow
    // stops at such an address as it does at bytes that do not decode (patches/0052-flow-instruction-alignment.patch; A7-6).
    instructionAlignment = 2;
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
    // win depend on order. So the first outcome stands for the process -- once it is an outcome.
    //
    // Fresh        nothing has touched an engine global. Spec materialisation is filesystem work, so a
    //              failure in it is a failure of this request only, whatever its kind -- an allocation, a
    //              directory that cannot be made, a file that cannot be written, a lock that cannot be
    //              taken, a rename a scanner holds -- and the next request starts again from here (A6-30,
    //              A6-40). Each of those has a cause that can be gone one request later (a full disk, a
    //              permission the user fixes), and none has written anything the engine reads: the stage
    //              stays Fresh, the request is answered Unavailable with the reason, and the cost of
    //              trying again is one directory check, or one verified extraction, on a request the user
    //              made. Nothing is rate-limited: a clock would make which request is retried depend on
    //              timing, and a refusal text that is not retried is the one that must say so.
    // Ready        the library is started. The directory it was started from is not trusted for good: every
    //              later call verifies it again (the cost is a read of the five small files), restores
    //              it when it is not what the build embeds, and answers Unavailable, to be tried again on
    //              the next call, when it cannot. The engine reads the .pspec and .cspec of that directory
    //              for every Architecture, so a cache directory cleaned or quarantined under the running
    //              process used to fail every later request until a restart, with an XML message that
    //              named neither the cause nor the cure (A6-44).
    // Interrupted  startDecompilerLibrary threw part-way. It fills process-global tables (attribute and
    //              element ids, the capability lists, the spec search path) by appending, with no way to
    //              see how far it got or to undo it, so running it again would register some twice and a
    //              skipped step would be missing for good (what the second request used to find was an
    //              empty print-language list: "No print languages registered"). The process refuses the
    //              engine from then on, as it does after an allocation failure inside it, and says why,
    //              restart included: it is the one refusal a retry cannot clear.
    enum class Stage { Fresh, Ready, Interrupted };
    static std::mutex m;
    static Stage stage = Stage::Fresh;
    static std::string interruptedText;               // Interrupted: made before the first global write
    static std::string readyConfigDir, readyDir;      // Ready: what the library was started from
    std::lock_guard<std::mutex> lock(m);
    // What the call that moved the stage hands back, swapped into 'error' (no allocation), so that the request that
    // met the failure says what the next ones will say even when the heap is gone (A6-32): the texts are all made
    // before the stage changes, and moved (not copied) into place after it.
    std::string firstReport;
    bool startedNow = false;
    if (stage == Stage::Fresh)
    {
        // An exception from here to the vector below leaves the stage Fresh: nothing global was written.
        SpecBundleResult bundle = MaterialiseSh2SpecBundle(configDir);
        if (!bundle.ok)
        {
            // Unavailable for this request and Fresh still: the stage is not committed, so the next request retries.
            // (A move assignment: no allocation.)
            error = bundle.error.empty() ? std::string("Decompiler unavailable: the language files could not be prepared")
                                         : std::move(bundle.error);
            return false;
        }
        const std::vector<std::string> paths{ bundle.dir };
        const auto text = [](const char* by) {
            return std::string("Decompiler unavailable: the engine's library did not finish starting (") + by +
                   "); restart Saturn Explorer to decompile again";
        };
        std::string oomText = text("out of memory"), otherText = text("an unexpected exception");
        std::string oomKeep = oomText, otherKeep = otherText;
        std::string configKeep = configDir, dirKeep = bundle.dir;
        stage = Stage::Interrupted;   // from the first global write; Ready only once the last has been made
        try
        {
            ghidra::startDecompilerLibrary(paths);
            readyConfigDir = std::move(configKeep);   // moves: no allocation
            readyDir = std::move(dirKeep);
            stage = Stage::Ready;
            startedNow = true;
        }
        catch (std::bad_alloc&)
        {
            interruptedText = std::move(oomKeep);
            firstReport = std::move(oomText);
        }
        catch (...)
        {
            interruptedText = std::move(otherKeep);
            firstReport = std::move(otherText);
        }
    }
    switch (stage)
    {
    case Stage::Ready:
    {
        if (!startedNow)
        {
            // Verified and, when damaged, repaired under the lock, from the configuration directory the library
            // was started from: the engine reads that one, whatever this caller's is. May throw (std::bad_alloc):
            // the stage is Ready and stays so, and the next call looks again.
            SpecBundleResult again = MaterialiseSh2SpecBundle(readyConfigDir);
            if (!again.ok || again.dir != readyDir)
            {
                error = again.error.empty() ? std::string("Decompiler unavailable: the language files could not be prepared")
                                            : std::move(again.error);
                return false;
            }
        }
        error.clear();
        return true;
    }
    default:
        // Every call says it, the first included: the request that met the failure is told what the next
        // ones will be told, and neither is "out of memory" with an engine that looks usable.
        if (!firstReport.empty()) error.swap(firstReport);
        else error = interruptedText;
        return false;
    }
}

}  // namespace decomp
}  // namespace sfe
