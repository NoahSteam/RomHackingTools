#!/usr/bin/env python3
"""analyze_v3.py on synthetic runs with known answers (no emulator, no BIOS)."""
import io
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze_v3 as A  # noqa: E402

WARM = 2.0
DUR = 10.0
HZ = 100


def write_run(d, rep, cfg, fps, cpu_ms_per_frame=None, gap_at=None, mode=None, summary_valid=True,
              duration=DUR, timeline_switch=False, hdr_extra=None):
    """One run: frames at 'fps' for warm-up + window, stream rows (a: two probe rows)."""
    base = os.path.join(d, "run%02d_%s" % (rep, cfg))
    want_mode = 1 if cfg == "c" else 0
    ws = int(WARM * 1e9)
    we = ws + int(duration * 1e9)
    rows = []
    if cfg == "a":
        for t in (ws, we):
            rows.append(("probe", 0, int(t / 1e9 * fps), 1, 0, 0, 0, t, 0))
    else:
        n = int((WARM + duration) * fps) + 1
        for i in range(n):
            t = int(i * 1e9 / fps)
            gc = 1 if (gap_at is not None and i == gap_at) else 0
            tl = 2 if (timeline_switch and i > n // 2) else 1
            m = want_mode if mode is None else mode
            rows.append(("frame", i + 1, i, tl, m, gc, 0, t, 0))
    with open(base + ".csv", "w") as f:
        f.write("kind,sample_seq,frame_no,timeline_id,mode,gap_count,evict_count,arrival_ns,newest_seq\n")
        for r in rows:
            f.write(",".join(str(x) for x in r) + "\n")
    kv = [("tag", cfg), ("mode", "bracket" if cfg == "a" else "stream"),
          ("attribution_requested", 1 if cfg == "c" else 0), ("window_start_ns", ws), ("window_end_ns", we),
          ("valid", 1 if summary_valid else 0)]
    if cpu_ms_per_frame is not None:
        # 1000 ticks/s, cpu burned = per-frame cost * frames in the window
        frames = int(duration * fps)
        kv += [("cpu_ticks_per_s", 1000), ("cpu_start_at_ns", ws), ("cpu_start_ticks", 500),
               ("cpu_end_at_ns", we), ("cpu_end_ticks", 500 + int(round(cpu_ms_per_frame * frames)))]
    with open(base + ".summary", "w") as f:
        for k, v in kv:
            f.write("%s=%s\n" % (k, v))


def make(d, a, b, c, **kw):
    for i, (fa, fb, fc) in enumerate(zip(a, b, c)):
        write_run(d, i + 1, "a", fa)
        write_run(d, i + 1, "b", fb)
        write_run(d, i + 1, "c", fc)


class Analyze(unittest.TestCase):
    def run_analysis(self, d, **kw):
        kw.setdefault("min_frames", 1000)
        return A.analyse(d, boots=2000, **kw)

    def test_known_overheads(self):
        with tempfile.TemporaryDirectory() as d:
            # medians 600 / 540 / 300 fps: extra time per frame (b) 600/540-1 = 11.11%, (c) 100%.
            make(d, [600, 606, 594, 600, 603], [540, 543, 537, 540, 546], [300, 303, 297, 300, 306])
            res = self.run_analysis(d)
            self.assertEqual([res["configs"][c]["valid"] for c in "abc"], [5, 5, 5])
            self.assertAlmostEqual(res["configs"]["a"]["speed"]["median"], 600.0, delta=0.5)
            ob = res["overhead"][("b", "speed")]
            oc = res["overhead"][("c", "speed")]
            self.assertAlmostEqual(ob["point"], 600 / 540 - 1, delta=0.002)
            self.assertAlmostEqual(oc["point"], 1.0, delta=0.01)
            self.assertTrue(ob["lo"] <= ob["point"] <= ob["hi"])
            self.assertTrue(0.0 < ob["lo"], "a clear 11%% cost must exclude zero: %r" % ob)
            self.assertTrue(oc["lo"] > 0.5)
            self.assertEqual(res["warnings"], [])

    def test_cpu_metric(self):
        with tempfile.TemporaryDirectory() as d:
            for i in range(4):
                write_run(d, i + 1, "a", 500, cpu_ms_per_frame=2.0)
                write_run(d, i + 1, "b", 500, cpu_ms_per_frame=2.5)
                write_run(d, i + 1, "c", 500, cpu_ms_per_frame=6.0)
            res = self.run_analysis(d)
            self.assertAlmostEqual(res["configs"]["a"]["cpu_ms_per_frame"]["median"], 2.0, delta=0.01)
            self.assertAlmostEqual(res["overhead"][("b", "cpu_ms_per_frame")]["point"], 0.25, delta=0.01)
            self.assertAlmostEqual(res["overhead"][("c", "cpu_ms_per_frame")]["point"], 2.0, delta=0.02)
            # same speed everywhere: the speed overhead is zero and its interval says so
            sb = res["overhead"][("b", "speed")]
            self.assertAlmostEqual(sb["point"], 0.0, delta=1e-6)
            self.assertTrue(sb["lo"] <= 0.0 <= sb["hi"])

    def test_invalid_runs_are_flagged_and_excluded(self):
        with tempfile.TemporaryDirectory() as d:
            make(d, [600] * 4, [540] * 4, [300] * 4)
            # run 2 b: a lost sample the client did not notice (summary says valid)
            write_run(d, 2, "b", 540, gap_at=3000)
            # run 3 b: client itself says invalid
            write_run(d, 3, "b", 540, summary_valid=False)
            # run 4 c: ran in the wrong attribution mode
            write_run(d, 4, "c", 300, mode=0)
            # run 1 c: timeline changed mid-window
            write_run(d, 1, "c", 300, timeline_switch=True)
            # run 2 c: far too few frames
            write_run(d, 2, "c", 50)
            res = self.run_analysis(d)
            why = {(r["rep"], r["cfg"]): r["why"] for r in res["runs"]}
            self.assertTrue(any("lost samples" in w for w in why[(2, "b")]))
            self.assertTrue(any("marked the run invalid" in w for w in why[(3, "b")]))
            self.assertTrue(any("attribution mode" in w for w in why[(4, "c")]))
            self.assertTrue(any("timeline" in w for w in why[(1, "c")]))
            self.assertTrue(any("frames in the window" in w for w in why[(2, "c")]))
            for key in ((1, "a"), (1, "b"), (3, "c")):
                self.assertEqual(why[key], [], key)
            self.assertEqual(res["configs"]["b"]["valid"], 2)
            self.assertEqual(res["configs"]["c"]["valid"], 1)
            # the lost-sample run's speed must not be in the statistics
            self.assertEqual(len(res["configs"]["b"]["speed"]["values"]), 2)

    def test_drift_between_repetitions_is_flagged(self):
        with tempfile.TemporaryDirectory() as d:
            # a degrades 600 -> 540 over the session (thermal): the three configs interleave, b/c steady
            a = [600, 596, 590, 580, 565, 550, 540]
            make(d, a, [540] * 7, [300] * 7)
            res = self.run_analysis(d)
            self.assertTrue(any("config a speed" in w and "differ" in w for w in res["warnings"]), res["warnings"])
            self.assertTrue(any("config a speed" in w and "third" in w for w in res["warnings"]), res["warnings"])

    def test_report_quotes_plan_and_invents_no_threshold(self):
        with tempfile.TemporaryDirectory() as d:
            make(d, [600] * 3, [540] * 3, [300] * 3)
            res = self.run_analysis(d)
            buf = io.StringIO()
            A.report(res, {"throttle": "off"}, out=buf)
            text = buf.getvalue()
            self.assertIn("only if V3 shows a cost", text)
            self.assertIn("only if timing shows a visible cost", text)
            self.assertIn("NO numeric threshold", text)

    def test_cli_exit_codes(self):
        with tempfile.TemporaryDirectory() as d:
            make(d, [600] * 3, [540] * 3, [300] * 3)
            out = io.StringIO()
            old, sys.stdout = sys.stdout, out
            try:
                self.assertEqual(A.main([d, "--bootstrap", "200"]), 0)
                for i in (1, 2, 3):
                    write_run(d, i, "c", 300, summary_valid=False)
                self.assertEqual(A.main([d, "--bootstrap", "200"]), 2)
            finally:
                sys.stdout = old
        with tempfile.TemporaryDirectory() as e:
            self.assertEqual(A.main([e]), 1)

    def test_aborted_directory_counts_nothing(self):
        with tempfile.TemporaryDirectory() as d:
            make(d, [600] * 3, [540] * 3, [300] * 3)
            self.assertEqual(A.analyse(d, boots=100)["configs"]["a"]["valid"], 3)
            with open(os.path.join(d, "ABORTED"), "w") as f:
                f.write("ABORTED: run02_b: client error (exit 1)\n")
            res = A.analyse(d, boots=100)
            for cfg in "abc":
                self.assertEqual(res["configs"][cfg]["valid"], 0)
            self.assertTrue(all("run aborted" in " ".join(r["why"]) for r in res["runs"]))


if __name__ == "__main__":
    unittest.main()
