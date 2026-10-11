// Are the three copies of the SH-2 language spec the same spec (PLAN.md A5)?
//
// The .slaspec/.sinc sources are what reviewers read and what a SLEIGH patch changes; the
// checked-in sh-2.sla is what se-sleighc made of them; Sh2SpecData.h is what the app actually
// embeds and materialises. Nothing at build time keeps them in step -- regeneration is a
// deliberate act (se-regen-sla) -- so this compiles the vendored .slaspec afresh and requires
// byte equality with both the checked-in .sla and the embedded one, and requires the embedded
// .ldefs/.pspec/.cspec to equal the vendored files. A Ghidra bump or a patch to the spec that
// was not followed by se-regen-sla fails here instead of shipping a stale engine.
//
// usage: SaturnExplorerSlaFreshnessTests <se-sleighc> <scratch dir>
#include "Decompiler/Sh2SpecData.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifndef SE_SPEC_SOURCE_DIR
#error "SE_SPEC_SOURCE_DIR must name FrontEnd/third_party/ghidra-decompiler/processors/SuperH"
#endif

namespace {

int gFailures = 0;

void Check(bool ok, const char* what, int line)
{
    if (ok) return;
    std::printf("CHECK failed at line %d: %s\n", line, what);
    ++gFailures;
}

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

bool ReadFile(const std::string& path, std::vector<unsigned char>& out)
{
    out.clear();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    unsigned char buf[16384];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
    std::fclose(f);
    return true;
}

const sfe::decomp::EmbeddedSpecFile* Embedded(const char* name)
{
    for (size_t i = 0; i < sfe::decomp::kSh2SpecFileCount; ++i)
        if (std::strcmp(sfe::decomp::kSh2SpecFiles[i].name, name) == 0) return &sfe::decomp::kSh2SpecFiles[i];
    return nullptr;
}

bool SameAsEmbedded(const std::vector<unsigned char>& bytes, const char* name)
{
    const sfe::decomp::EmbeddedSpecFile* e = Embedded(name);
    return e && e->size == bytes.size() && std::memcmp(e->data, bytes.data(), bytes.size()) == 0;
}

// Where the first difference is, so a failure says more than "differs".
void ReportDifference(const char* what, const std::vector<unsigned char>& a, const std::vector<unsigned char>& b)
{
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
    std::printf("  %s: sizes %zu vs %zu, first difference at byte %zu\n", what, a.size(), b.size(), i);
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::printf("usage: %s <se-sleighc> <scratch dir>\n", argv[0]);
        return 2;
    }
    const std::string compiler = argv[1];
    const std::string scratch = argv[2];
    const std::string specDir = SE_SPEC_SOURCE_DIR;
    const std::string fresh = scratch + "/sh-2.fresh.sla";
    std::remove(fresh.c_str());

    // The embedded set is exactly the four files the engine opens, nothing more or less.
    CHECK(sfe::decomp::kSh2SpecFileCount == 4);
    CHECK(Embedded("superh.ldefs") && Embedded("superh.pspec") && Embedded("superh.cspec") && Embedded("sh-2.sla"));

    std::string cmd = "\"" + compiler + "\" \"" + specDir + "/sh-2.slaspec\" \"" + fresh + "\"";
#ifdef _WIN32
    // cmd.exe drops the first and last quote of a line that starts with one; wrap the whole
    // line so the quoting around each path survives.
    cmd = "\"" + cmd + "\"";
#endif
    const auto t0 = std::chrono::steady_clock::now();
    const int rc = std::system(cmd.c_str());
    const auto t1 = std::chrono::steady_clock::now();
    std::printf("se-sleighc: exit %d in %.1f ms\n", rc,
                std::chrono::duration<double, std::milli>(t1 - t0).count());
    CHECK(rc == 0);

    std::vector<unsigned char> compiled, checkedIn;
    CHECK(ReadFile(fresh, compiled));
    CHECK(!compiled.empty());
    CHECK(ReadFile(specDir + "/sh-2.sla", checkedIn));

    if (compiled != checkedIn)
    {
        ReportDifference("fresh .sla vs checked-in sh-2.sla", compiled, checkedIn);
        std::printf("  the checked-in sh-2.sla is stale: build the se-regen-sla target and commit the result\n");
        ++gFailures;
    }
    if (!SameAsEmbedded(compiled, "sh-2.sla"))
    {
        std::printf("  the sh-2.sla embedded in Sh2SpecData.h is stale: build se-regen-sla and commit the result\n");
        ++gFailures;
    }
    for (const char* name : { "superh.ldefs", "superh.pspec", "superh.cspec" })
    {
        std::vector<unsigned char> vendored;
        const bool read = ReadFile(specDir + "/" + name, vendored);
        CHECK(read);
        if (read && !SameAsEmbedded(vendored, name))
        {
            std::printf("  the %s embedded in Sh2SpecData.h differs from the vendored file: "
                        "build se-regen-sla and commit the result\n", name);
            ++gFailures;
        }
    }

    std::remove(fresh.c_str());
    if (gFailures)
    {
        std::printf("%d failure(s)\n", gFailures);
        return 1;
    }
    std::printf("SlaFreshnessTests: .slaspec, checked-in .sla and embedded spec agree (%zu bytes of .sla)\n",
                compiled.size());
    return 0;
}
