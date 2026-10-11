// SpecBundle — puts the decompiler's language files where the engine can open them.
//
// Ghidra's engine reads .ldefs/.pspec/.cspec/.sla by path; there is no in-memory hook. The
// files are embedded in the executable (Sh2SpecData.h) so that the Windows build stays one
// .exe and the macOS bundle needs no extra resources, and this module writes them out on
// first use (PLAN.md A5):
//
//   <configDir>/decompiler/<first 16 hex of the bundle's SHA-256>/
//       superh.ldefs  superh.pspec  superh.cspec  sh-2.sla  manifest.json
//
// The directory is named by content, so any change to the embedded files -- a Ghidra bump, a
// SLEIGH patch -- lands in a new directory and can never be served stale bytes from an older
// one. It is written as <dir>.tmp-<pid>, verified, then renamed into place, so a crash never
// leaves a half-written directory under the real name. Every open re-verifies the files
// against the embedded bytes: a mismatch or a missing file deletes the directory and extracts
// once more, and if that also fails the decompiler is reported unavailable, naming the path,
// rather than run on bad specs. Verification, repair and publication happen under an
// exclusive lock on <hash>.lock, so two instances cannot repair the same directory at once
// and delete each other's freshly verified copy. A link found where the directory should be
// is removed, never followed.
//
// Directories for other hashes (other builds sharing the config dir) are never touched.
//
// Filesystem only: no Ghidra header, no ImGui, no global state beyond the test hook below,
// callable from any thread.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Sha256.h"

namespace sfe
{
namespace decomp
{

struct SpecFileData
{
    std::string name;            // a plain file name, no separators
    const unsigned char* data;
    size_t size;
};

struct SpecBundleResult
{
    bool ok = false;
    std::string dir;             // the verified directory to hand the engine (when ok)
    std::string error;           // why the decompiler is unavailable, with the path (when !ok)
    bool extracted = false;      // the files were (re)written on this call
    bool repaired = false;       // a directory was present but failed verification
};

// The embedded SH-2 bundle under 'configDir' (normally Settings::EnsureConfigDir()).
SpecBundleResult MaterialiseSh2SpecBundle(const std::string& configDir);

// The embedded SH-2 files, in the order they are written.
const std::vector<SpecFileData>& Sh2SpecFiles();

// The directory name for a file set: the first 16 hex digits of the SHA-256 of every file's
// name, size and bytes, in order.
std::string SpecBundleHash(const std::vector<SpecFileData>& files);

// Tests only: called with the staging directory after its files are written and before it is
// verified and published, so a test can damage the staged copy and check nothing unverified is
// published. Null, the default, everywhere else.
void SetSpecBundleStagingHookForTests(void (*hook)(const std::string& stagingDir));

// Lower-case hex SHA-256 (FIPS 180-4). Names the directory and fills the manifest. The
// implementation moved to Sha256.h so it builds without the decompiler; the name stays here for
// the known-answer test.
using sfe::Sha256Hex;

}  // namespace decomp
}  // namespace sfe
