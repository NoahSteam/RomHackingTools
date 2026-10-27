"""cmake/GenerateBuildInfo.cmake: which release version (kBuildVersion) a build bakes in.

The in-app "Check for Updates" compares that tag with GitHub's latest release, so a release build that
bakes in nothing would call itself "not a tagged release". This runs the script against throwaway git
repositories for each way the version can arise: an exact v* tag, an untagged commit, a modified tree,
a tag that is not a version, and the SE_RELEASE_VERSION the release workflow sets (which must win even on
a tree that reads as modified, and must refuse a name that is not safe inside a C++ string literal).
The FrontEnd.vcxproj copy of the same rules (the MSBuild project has no CMake) is not run here.
Usage: check_build_info.py <cmake> <SaturnExplorer source dir>"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

cmake, src = sys.argv[1], os.path.abspath(sys.argv[2])
script = os.path.join(src, "cmake", "GenerateBuildInfo.cmake")
template = os.path.join(src, "FrontEnd", "BuildInfo.h.in")
failures = 0


def check(cond, what, got=None):
    global failures
    if not cond:
        print("FAIL:", what)
        if got is not None:
            print("  got:", got)
        failures += 1


def git(repo, *args):
    subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@example.com", "-c", "core.autocrlf=false", *args],
                   cwd=repo, check=True, capture_output=True)


def baked(repo, out_dir, env_version=None):
    """kBuildVersion that the script writes for `repo`."""
    out = os.path.join(out_dir, "BuildInfo.h")
    if os.path.exists(out):
        os.remove(out)
    env = {k: v for k, v in os.environ.items() if k != "SE_RELEASE_VERSION"}
    if env_version is not None:
        env["SE_RELEASE_VERSION"] = env_version
    subprocess.run([cmake, f"-DSRC_DIR={repo}", f"-DIN_FILE={template}", f"-DOUT_FILE={out}", "-P", script],
                   check=True, capture_output=True, env=env)
    with open(out, encoding="utf-8") as f:
        m = re.search(r'kBuildVersion = "([^"]*)"', f.read())
    return m.group(1) if m else None


work = tempfile.mkdtemp(prefix="se_buildinfo_")
try:
    repo = os.path.join(work, "repo")
    os.makedirs(repo)
    git(repo, "init", "-q")
    with open(os.path.join(repo, "a.txt"), "w") as f:
        f.write("a\n")
    git(repo, "add", "a.txt")
    git(repo, "commit", "-q", "-m", "one")

    check(baked(repo, work) == "", "an untagged commit bakes no version")
    check(baked(repo, work, "v9.9.9") == "v9.9.9", "SE_RELEASE_VERSION is baked in")
    check(baked(repo, work, "") == "", "an empty SE_RELEASE_VERSION is ignored")

    git(repo, "tag", "nightly-1")
    check(baked(repo, work) == "", "a tag that is not a v<number> version is not a release")
    git(repo, "tag", "v1.2.3")
    check(baked(repo, work) == "v1.2.3", "a v* tag exactly on HEAD is the version")
    check(baked(repo, work, "v2.0") == "v2.0", "SE_RELEASE_VERSION wins over the tag")

    with open(os.path.join(repo, "a.txt"), "a") as f:
        f.write("edited\n")
    check(baked(repo, work) == "", "a tag found on a modified tree is not the release")
    check(baked(repo, work, "v1.2.3") == "v1.2.3", "an explicit SE_RELEASE_VERSION stands on a modified tree")
    git(repo, "checkout", "-q", "--", "a.txt")

    for bad in ('v1";system("x");//', "v1 2", "v1/../x", "v1\\x", "$(evil)"):
        check(baked(repo, work, bad) == "", f"an unsafe SE_RELEASE_VERSION {bad!r} is not baked in")

    git(repo, "commit", "-q", "--allow-empty", "-m", "two")
    check(baked(repo, work) == "", "a tag on an earlier commit is not this build's version")
finally:
    shutil.rmtree(work, ignore_errors=True)

if failures:
    print(f"{failures} check(s) failed")
    sys.exit(1)
print("BuildInfo: all checks passed")
