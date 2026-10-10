// PosixSpawn — start an auxiliary program on macOS/Linux with an argument VECTOR, and find out whether
// it actually started.
//
// The launcher that was here handed one command line to /bin/sh. That broke two ways: a path holding
// `$`, a backtick or a quote was reinterpreted by the shell (config$X/A became config/A), and because
// the tool was started by a grandchild of a double fork, the parent only learned that the FIRST fork
// worked -- a missing program, a permission error or an unusable working directory still reported
// success. Here the program is exec'd directly with argv exactly as given, and the grandchild reports
// a failed chdir/exec back over a close-on-exec pipe (an exec that succeeds closes the pipe, which the
// parent reads as end of file).
//
// Free of ImGui and the platform layer so it can be tested; compiles to nothing off POSIX.
#pragma once

#include <string>
#include <vector>

namespace sfe
{

// Run 'path' with 'args' (argv[1..]) in 'workingDir' (empty = leave it), detached: new session, stdio
// on /dev/null, reparented to init so it is never a zombie and outlives this program. A bare name is
// looked up on PATH. A macOS application bundle (Foo.app) is handed to `open -a` with `--args`.
// True once the program has started (for a bundle: once `open` has accepted it); false with 'error'
// filled -- naming the program and the reason -- when it did not.
bool SpawnDetached(const std::string& path, const std::vector<std::string>& args,
                   const std::string& workingDir, std::string& error);

}  // namespace sfe
