# Review tools

Scripts the review rounds use to find and check bugs. None of them is built or shipped; the
lasting checks are the tests in `FrontEnd/tests/`.

Kept here:

* `derive_handler_cases.py` and `audit_hardware_map.py`: regenerate `FrontEnd/tests/MednafenHandlerCases.txt`
  and the hardware register audit.
* The latest round of each area (`a2_round17/`, `a3_round14/`, `a8_round2/`, `a9_round1/`), which the next round starts from.
  `a2_round17/`: `dagen17.py` (families 100 to 111: narrow compares, hardware-register addresses as numbers, and the mixed-width memory families, on top of
  `a2_round16/dagen16.py`), `fam17.sh` (the printed-C differential per family; `BE=1` reads the memory families on a big-endian arena), `oldnew17.sh` and `oldnewmem17.sh`
  (the same words through two engines, `diff17.py` counts the texts that differ), `mkrc17.sh` and `relink17.sh` and `famrc17.sh` (the in-situ rule checks on the tree with patch
  0075), `cls17.py` (a log's verdicts with their texts), `fix17.py`, `genranges17.sh`.
  `a9_round1/`: `run_pieces.sh` (a long run of `SaturnExplorerLoaderFuzzTests` in pieces of at most 20 minutes) and
  `wasm32_yss_wrap.sh` with `yss_wrap32.cpp` (the 32-bit repro of A9-7; needs an emsdk and node).
* What those still call: the `dagen` generator chain (`a2_round10/dagen10.py`, `gen_range.sh`,
  `a2_round11/dagen11.py`, `dagen11b.py`, `a2_round12/dagen12.py`, `a2_round13/dagen13.py`) and
  `a3_round13/textdiff.py`.
* The mask checkers `a2_round8/nzm_check.patch` and `a2_round9/nzm_check_r9.patch` (`VENDOR.md` names
  the second as the check to rerun when Ghidra is bumped).
* `a7_round2/`: area 7 round 2 (the mapping review). `corpus.sh` runs a corpus image with `SE_GAME_TOKEN_DUMP` (the tokens with their
  instructions, written by `MappingOracle.h`); `token_semantics.py` cross-tabulates token text against mnemonic (`--cross`, `--slots`,
  `--suspect`); `slot_lines.py` (A7-10), `frame_at_call.py` and `frame_piece_at_load.py` (A7-11) count the lines whose address names an
  instruction that did not make them; `showfn.py` prints one function with the instruction behind each token. The perturbation
  instrument (A7-8) is `SE_GAME_PERTURB=<n>` in `Sh2DecompilerTests.cpp`.

Everything else that earlier rounds committed (their drivers, fuzzers, fixtures and summaries) was
removed after commit `215d303`. The record (`FunctionalityVerification/15_DECOMPILER_STEP5.md`) still
names those files; check out `215d303` to rerun them.

Rules for new rounds:

* Commit scripts, not their output: a fixture a script generates (such as a 99,000-line `.src`) is
  produced again by the script, not checked in.
* When a round supersedes the previous one's tools, the coordinator removes the older directory,
  except for files the newer tools still call.
