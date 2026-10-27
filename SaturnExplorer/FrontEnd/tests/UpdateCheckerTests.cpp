// UpdateChecker tests: what the "Check for Updates" menu item concludes from GitHub's
// /releases/latest reply. The network call itself is not exercised; EvaluateLatestRelease takes the
// reply as data, so each outcome (no release, rate limit, a tag newer / older / equal / not
// comparable, a build that is not a tagged release) is checked with a canned one.
#include "UpdateChecker.h"

#include <cstdio>
#include <string>

using namespace sfe;
using State = UpdateChecker::State;

namespace
{
int gFail = 0;
void Check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; }
}

const char* kRepo = "NoahSteam/RomHackingTools";

IPlatform::HttpResponse Reply(long status, const std::string& body)
{
    IPlatform::HttpResponse r;
    r.ok = true;
    r.status = status;
    r.body = body;
    return r;
}

// The shape of GitHub's reply: a tag_name at the top, and others nested in the author and the assets
// (some of them before the real one, in case a real reply's key order ever changes).
std::string Release(const std::string& tag)
{
    return "{\"url\":\"https://api.github.com/x\",\"assets\":[{\"name\":\"a.zip\",\"tag_name\":\"nested-1\"}],"
           "\"author\":{\"login\":\"u\",\"tag_name\":\"nested-2\"},\"tag_name\":\"" + tag + "\","
           "\"name\":\"Saturn Explorer\",\"body\":\"notes\\r\\n- a \\\"quoted\\\" line\",\"draft\":false}";
}

void TestExtractReleaseTag()
{
    Check(ExtractReleaseTag(Release("v1.2.3")) == "v1.2.3", "the root tag_name is read, not a nested one");
    Check(ExtractReleaseTag("{ \"tag_name\" :  \"v2\" }") == "v2", "whitespace around the colon is fine");
    Check(ExtractReleaseTag("{\"assets\":[{\"tag_name\":\"v9\"}]}").empty(), "a tag_name only inside a nested value is not the release's");
    Check(ExtractReleaseTag("{\"name\":\"tag_name\",\"x\":1}").empty(), "the text tag_name as a value is not a key");
    Check(ExtractReleaseTag("{\"tag_name\":null}").empty(), "a null tag is no tag");
    Check(ExtractReleaseTag("{\"tag_name\":12}").empty(), "a number is no tag");
    Check(ExtractReleaseTag("").empty() && ExtractReleaseTag("not json").empty(), "no JSON, no tag");
    Check(ExtractReleaseTag("{\"tag_name\":\"v1.0").empty(), "a truncated reply gives no tag");
    Check(ExtractReleaseTag("{\"tag_name\":\"v1/../x\"}").empty(), "a path in the tag is refused");
    Check(ExtractReleaseTag("{\"tag_name\":\"v1 0\"}").empty(), "a space in the tag is refused");
    Check(ExtractReleaseTag("{\"tag_name\":\"v1\\\"x\"}").empty(), "a quote in the tag is refused");
    Check(ExtractReleaseTag("{\"tag_name\":\"\"}").empty(), "an empty tag is refused");
    Check(ExtractReleaseTag("{\"tag_name\":\"" + std::string(65, 'a') + "\"}").empty(), "an over-long tag is refused");
    Check(ExtractReleaseTag("{\"tag_name\":\"v1\\u002e0\"}") == "v1.0", "a \\u escape of an ASCII character is decoded");
    Check(ExtractReleaseTag("{\"tag_name\":\"v1\\u00e9\"}").empty(), "a non-ASCII tag is refused");
    Check(ExtractReleaseTag("{\"tag_name\":\"v1.0-rc.1+b5\"}") == "v1.0-rc.1+b5", "pre-release and build suffixes are accepted");
}

void TestCompareReleaseTags()
{
    Check(CompareReleaseTags("v1.0", "v1.0") == ReleaseOrder::Same, "equal tags are the same");
    Check(CompareReleaseTags("v1.9", "v1.10") == ReleaseOrder::RemoteNewer, "1.10 is newer than 1.9, as numbers");
    Check(CompareReleaseTags("v1.10", "v1.9") == ReleaseOrder::RemoteOlder, "1.9 is older than 1.10");
    Check(CompareReleaseTags("v1.2", "v1.2.1") == ReleaseOrder::RemoteNewer, "a longer tag with a higher part is newer");
    Check(CompareReleaseTags("v2.0", "v1.99.9") == ReleaseOrder::RemoteOlder, "the major number decides first");
    Check(CompareReleaseTags("1.0", "v1.1") == ReleaseOrder::RemoteNewer, "the leading v is optional");
    Check(CompareReleaseTags("v1.0-rc1", "v1.1") == ReleaseOrder::RemoteNewer, "a pre-release suffix is ignored for the number");
    Check(CompareReleaseTags("v1.0-rc1", "v1.0") == ReleaseOrder::Unknown, "the same number with different text is not comparable");
    Check(CompareReleaseTags("v1.0", "v1.0.0") == ReleaseOrder::Unknown, "v1.0 against v1.0.0 is not comparable");
    Check(CompareReleaseTags("nightly", "v1.0") == ReleaseOrder::Unknown, "a tag that is not a version is not comparable");
    Check(CompareReleaseTags("v1.0", "release-2") == ReleaseOrder::Unknown, "a word suffix is not a version");
    Check(CompareReleaseTags("v99999999999", "v1") == ReleaseOrder::Unknown, "an enormous number is not a version");
    Check(CompareReleaseTags("v", "v1") == ReleaseOrder::Unknown && CompareReleaseTags("v1.", "v2") == ReleaseOrder::Unknown,
          "a bare v and a trailing dot are not versions");
}

void TestReleasePageUrl()
{
    Check(ReleasePageUrl(kRepo, "v1.0") == "https://github.com/NoahSteam/RomHackingTools/releases/tag/v1.0",
          "the release page is on github.com under the repository");
}

void TestEvaluate()
{
    // A tagged build against what GitHub reports.
    UpdateResult r = EvaluateLatestRelease(true, Reply(200, Release("v1.0")), kRepo, "v1.0", "abc");
    Check(r.state == State::UpToDate && r.tag == "v1.0", "the same release is up to date");
    Check(r.url == "https://github.com/NoahSteam/RomHackingTools/releases/tag/v1.0", "the up-to-date result still carries the page");

    r = EvaluateLatestRelease(true, Reply(200, Release("v1.1")), kRepo, "v1.0", "abc");
    Check(r.state == State::UpdateAvailable && r.tag == "v1.1" && r.message.find("v1.1") != std::string::npos &&
          r.message.find("v1.0") != std::string::npos, "a newer release is an update, naming both");

    r = EvaluateLatestRelease(true, Reply(200, Release("v1.0")), kRepo, "v1.1", "abc");
    Check(r.state == State::UpToDate && r.message.find("newer") != std::string::npos, "a build ahead of the latest release is not out of date");

    r = EvaluateLatestRelease(true, Reply(200, Release("v1.0")), kRepo, "v1.0-rc1", "abc");
    Check(r.state == State::UpdateAvailable, "a release that cannot be ordered against this build is reported as different");

    // A build that is not a tagged release (a local or untagged CI build): no verdict, but the latest release is shown.
    r = EvaluateLatestRelease(true, Reply(200, Release("v1.0")), kRepo, "", "abc1234567");
    Check(r.state == State::UnreleasedBuild && r.tag == "v1.0" && !r.url.empty() &&
          r.message.find("abc1234567") != std::string::npos, "an untagged build is told so and shown the latest release");

    // No release yet, and the failures.
    r = EvaluateLatestRelease(true, Reply(404, "{\"message\":\"Not Found\"}"), kRepo, "v1.0", "abc");
    Check(r.state == State::NoRelease && r.url.empty(), "404 means no release has been published");
    r = EvaluateLatestRelease(true, Reply(403, "{\"message\":\"rate limit\"}"), kRepo, "v1.0", "abc");
    Check(r.state == State::Error && r.message.find("403") != std::string::npos, "a rate limit is an error that says why");
    r = EvaluateLatestRelease(true, Reply(200, "{\"name\":\"x\"}"), kRepo, "v1.0", "abc");
    Check(r.state == State::Error, "a reply with no tag is an error");
    r = EvaluateLatestRelease(true, Reply(200, Release("bad tag")), kRepo, "v1.0", "abc");
    Check(r.state == State::Error && r.url.empty(), "an unsafe tag is an error and produces no link");

    IPlatform::HttpResponse down;
    down.error = "WinHTTP: the server name could not be resolved";
    r = EvaluateLatestRelease(false, down, kRepo, "v1.0", "abc");
    Check(r.state == State::Error && r.message == down.error, "a transport failure keeps its reason");
    IPlatform::HttpResponse unsupported;
    unsupported.error = "Online update check is not available in this build.";
    r = EvaluateLatestRelease(false, unsupported, kRepo, "v1.0", "abc");
    Check(r.state == State::Unsupported, "a build without HTTPS is reported as unsupported");
    r = EvaluateLatestRelease(false, IPlatform::HttpResponse(), kRepo, "v1.0", "abc");
    Check(r.state == State::Error && !r.message.empty(), "a failure with no reason still says something");
}
}  // namespace

int main()
{
    TestExtractReleaseTag();
    TestCompareReleaseTags();
    TestReleasePageUrl();
    TestEvaluate();
    if (gFail == 0) std::printf("UpdateChecker: all checks passed\n");
    return gFail == 0 ? 0 : 1;
}
