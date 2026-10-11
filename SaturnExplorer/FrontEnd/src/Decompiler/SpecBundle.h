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
// one. It is written as <dir>.tmp-<pid>, every file re-read and hashed, then renamed into
// place, so a crash or a second instance mid-write never leaves a half-written directory
// under the real name. Every open re-verifies the files against the embedded hashes: a
// mismatch or a missing file deletes the directory and extracts once more, and if that also
// fails the decompiler is reported unavailable, naming the path, rather than run on bad specs.
//
// Directories for other hashes (other builds sharing the config dir) are never touched.
//
// Filesystem only: no Ghidra header, no ImGui, no global state, callable from any thread.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

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

// The same for an arbitrary file set; what the tests drive.
SpecBundleResult MaterialiseSpecBundle(const std::string& configDir,
                                       const std::vector<SpecFileData>& files);

// The embedded SH-2 files, in the order they are written.
std::vector<SpecFileData> Sh2SpecFiles();

// The directory name for a file set: the first 16 hex digits of the SHA-256 of every file's
// name, size and bytes, in order.
std::string SpecBundleHash(const std::vector<SpecFileData>& files);

// Lower-case hex SHA-256 (FIPS 180-4). Exposed for the tests' known-answer check.
std::string Sha256Hex(const void* data, size_t size);

}  // namespace decomp
}  // namespace sfe
