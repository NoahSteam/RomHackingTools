# Review tools

Scripts the review rounds use to find and check bugs. None of them is built or shipped; the
lasting checks are the tests in `FrontEnd/tests/`.

Kept here:

* `derive_handler_cases.py` and `audit_hardware_map.py`: regenerate `FrontEnd/tests/MednafenHandlerCases.txt`
  and the hardware register audit.
* The latest round of each area (`a2_round14/`, `a3_round14/`, `a8_round2/`), which the next round starts from.
* What those still call: the `dagen` generator chain (`a2_round10/dagen10.py`, `gen_range.sh`,
  `a2_round11/dagen11.py`, `dagen11b.py`, `a2_round12/dagen12.py`, `a2_round13/dagen13.py`) and
  `a3_round13/textdiff.py`.
* The mask checkers `a2_round8/nzm_check.patch` and `a2_round9/nzm_check_r9.patch` (`VENDOR.md` names
  the second as the check to rerun when Ghidra is bumped).

Everything else that earlier rounds committed (their drivers, fuzzers, fixtures and summaries) was
removed after commit `215d303`. The record (`FunctionalityVerification/15_DECOMPILER_STEP5.md`) still
names those files; check out `215d303` to rerun them.

Rules for new rounds:

* Commit scripts, not their output: a fixture a script generates (such as a 99,000-line `.src`) is
  produced again by the script, not checked in.
* When a round supersedes the previous one's tools, the coordinator removes the older directory,
  except for files the newer tools still call.
