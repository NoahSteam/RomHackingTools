// UpdateChecker — "is there a newer release on GitHub?" Portable across platforms: it
// talks only to IPlatform::HttpsGet (Seam C), so Windows (WinHTTP), a future macOS build
// (libcurl), and the browser (unsupported) all share this one orchestration. The build's
// own release tag is baked in at compile time (BuildInfo.h, generated from the tag the release
// workflow builds or a v* tag on HEAD); the check fetches the repository's latest published
// release and compares the two tags.
//
// A tagged build is compared with the latest release, not with commits: a push to the default
// branch is not an update for it. A build that is not a tagged release (built from source, or a CI run
// with no tag) has no release to compare, so it is compared with the latest commit on the default branch
// instead -- the only signal a source build has that its checkout is behind -- and is also shown the
// latest release.
//
// The HTTPS GET blocks, so it runs on a worker thread; the UI polls State()/Message()
// each frame. No ImGui, no platform code here.
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "Platform/IPlatform.h"

namespace sfe
{

class UpdateChecker
{
public:
    enum class State
    {
        Idle,             // never run this session
        Checking,         // request in flight on the worker thread
        UpToDate,         // this build is the latest release (or newer than it)
        UpdateAvailable,  // a newer (or at least different) release is published
        NoRelease,        // the repository has no published release yet
        UnreleasedBuild,  // this build is not a tagged release, so there is nothing to compare
        Unsupported,      // this platform build has no HTTPS (e.g. the browser)
        Error             // transport / parse failure
    };

    ~UpdateChecker();

    // Kick off a check (no-op while one is already running). Runs HttpsGet on a worker
    // thread against 'platform'; poll State() afterwards. 'platform' must outlive the check.
    void Start(IPlatform& platform);

    State       GetState() const;
    std::string Message() const;      // human-readable status detail
    std::string LatestTag() const;    // the latest release's tag, when known
    std::string LatestUrl() const;    // its page on GitHub, when known (safe to hand to OpenURL)
    bool        Busy() const { return GetState() == State::Checking; }

    // The build's own identity (baked in at compile time).
    static const char*  BuildCommit();        // "unknown" for a build with no git
    static const char*  BuildCommitShort();
    static const char*  BuildVersion();       // the release tag, "" when not a tagged release
    static std::string  BuildDescription();   // "v1.0 (a1b2c3d4e5)" or "a1b2c3d4e5 (not a tagged release)"

private:
    // The state the worker writes and the UI reads. Held by shared_ptr so the worker can
    // finish (or be abandoned at shutdown) without touching a destroyed UpdateChecker — that
    // is what lets the destructor detach instead of blocking the app's exit on a network call.
    struct Shared
    {
        mutable std::mutex mutex;
        State              state = State::Idle;
        std::string        message;
        std::string        latestTag;
        std::string        latestUrl;
    };

    static void Run(std::shared_ptr<Shared> shared, IPlatform* platform);

    std::shared_ptr<Shared> mShared = std::make_shared<Shared>();
    std::thread             mThread;
};

// The top-level "tag_name" of GitHub's /releases/latest JSON, or "" if it is missing or not a
// plain tag (non-empty, at most 64 characters of [A-Za-z0-9._+-]: the tag is used to build a URL
// and shown to the user, so anything else is refused). Nested objects are skipped, so a "tag_name"
// inside an asset or author cannot be mistaken for it. Free function so it is trivially testable.
std::string ExtractReleaseTag(const std::string& json);

// How the `remote` release tag relates to the `local` one. Tags are compared as dotted numbers
// after an optional leading 'v' ("v1.10" is newer than "v1.9"; v1.0, V1.0 and v1.0.0 are the same
// release). A "-rc1"-style suffix is allowed and does not count in the ordering, but equal numbers with
// different suffixes (v1.0-rc1 against v1.0) are Unknown, as is anything not of that form.
enum class ReleaseOrder { Unknown, RemoteNewer, Same, RemoteOlder };
ReleaseOrder CompareReleaseTags(const std::string& local, const std::string& remote);

// The latest commit SHA (40 hex characters) from GitHub's /commits/<ref> JSON, or "" if the reply has none.
std::string ExtractLatestCommitSha(const std::string& json);

// What the check concludes from one HttpsGet outcome. Everything Run decides lives here, apart from the
// network call, so each branch can be tested with a canned reply.
struct UpdateResult
{
    UpdateChecker::State state = UpdateChecker::State::Error;
    std::string          message;
    std::string          tag;   // the latest release's tag, when the reply gave one
    std::string          url;   // its page on GitHub, when the reply gave one
};
// `transportOk` and `resp` are HttpsGet's return value and reply for <repo>'s /releases/latest;
// `localVersion` is the build's release tag ("" when not a tagged release) and `localCommitShort` its commit.
UpdateResult EvaluateLatestRelease(bool transportOk, const IPlatform::HttpResponse& resp,
                                   const std::string& repo, const std::string& localVersion,
                                   const std::string& localCommitShort);

// For a build that is not a tagged release: the release result refined by the default branch's latest commit
// (`head` is HttpsGet's reply for /commits/HEAD). The same commit is up to date; a different one is "update
// available" (a difference, not proof that this build is older), told to pull and rebuild; the latest release's
// tag and page ride along. Returns `release` unchanged when the commit reply is unusable or this build has no
// recorded commit ("unknown"). `localCommit` may end in the "-dirty" marker, which is ignored.
UpdateResult EvaluateSourceBuild(const UpdateResult& release, bool headOk, const IPlatform::HttpResponse& head,
                                 const std::string& localCommit, const std::string& localCommitShort);

// https://github.com/<repo>/releases/tag/<tag>. Built here rather than taken from the reply, so the page
// the user is sent to is always on github.com under our repo; `tag` must already have passed
// ExtractReleaseTag's character check, which is what makes it safe in a URL path as it is.
std::string ReleasePageUrl(const std::string& repo, const std::string& tag);

}  // namespace sfe
