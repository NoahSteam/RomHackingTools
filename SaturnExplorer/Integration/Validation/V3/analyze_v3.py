#!/usr/bin/env python3
"""Analyze a V3 benchmark directory written by run_v3.sh (see PROCEDURE.md).

Input: <dir>/run<NN>_<cfg>.csv and .summary, cfg in {a, b, c} (se-bench-client output). Optional
<dir>/run_meta.txt (key=value: throttle=on|off, ...). Nothing is read from the emulator here.

Metrics, identical estimator in every configuration (see PROCEDURE.md "Speed measurement"):
  speed       emulated frames per host second over the window:
                (f(end) - f(start)) / (end - start), f(t) = frame_no of the newest frame row stamped at
                or before t. Meaningful only when the emulator is not throttled.
  host_ms_per_frame_cpu
              emulator-process CPU milliseconds per emulated frame between the two CPU samples
              (Linux /proc; absent elsewhere). Meaningful throttled or not.
Overhead of (b) and (c) relative to (a) is the ratio of the median extra host time per emulated frame:
  speed:  median(1/speed_x) / median(1/speed_a) - 1       cpu:  median(cpu_x) / median(cpu_a) - 1
with a percentile-bootstrap interval from resampling the runs of each configuration.

Exit status: 0 analysed, 1 unusable input, 2 analysed but a configuration has no valid run.
"""
import argparse
import csv
import json
import os
import random
import re
import statistics
import sys

# Harness validity defaults. They are NOT thresholds from the plan; the plan gives none for V3.
DEFAULT_MIN_FRAMES = 1000     # frames inside the window
DEFAULT_MIN_WINDOW_S = 5.0
DEFAULT_DRIFT_PCT = 3.0       # spread of the SAME configuration across repetitions

PLAN_QUOTES = {
    "F12": "F12. Gate bus and idle counters behind an emulate-thread flag latched at the frame boundary, "
           "only if V3 shows a cost.",
    "F13": "F13. UI caching, only if timing shows a visible cost.",
}


def read_kv(path):
    out = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if "=" in line:
                k, v = line.split("=", 1)
                if k == "invalid_reason":
                    out.setdefault("invalid_reasons", []).append(v)
                else:
                    out[k] = v
    return out


def newest_frame_at(rows, t):
    """frame_no of the newest identified frame row with arrival_ns <= t; rows sorted by arrival."""
    best = None
    for r in rows:
        if r["arrival_ns"] > t:
            break
        if r["frame_no"] >= 0 and r["kind"] != "gap":
            best = r
    return best


def load_run(csv_path, summary_path):
    s = read_kv(summary_path)
    rows = []
    with open(csv_path, newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            rows.append({
                "kind": r["kind"], "sample_seq": int(r["sample_seq"]), "frame_no": int(r["frame_no"]),
                "timeline_id": int(r["timeline_id"]), "mode": int(r["mode"]),
                "gap_count": int(r["gap_count"]), "evict_count": int(r["evict_count"]),
                "arrival_ns": int(r["arrival_ns"]),
            })
    rows.sort(key=lambda r: r["arrival_ns"])
    return s, rows


def evaluate_run(summary, rows, min_frames, min_window_s):
    """Returns (metrics dict, list of invalid reasons). Re-derives validity from the CSV; the
    client's own verdict is one more input, never the only one."""
    why = []
    if summary.get("valid") != "1":
        why.append("client marked the run invalid: " + "; ".join(summary.get("invalid_reasons", ["no reason given"])))
    ws, we = int(summary["window_start_ns"]), int(summary["window_end_ns"])
    if ws < 0 or we <= ws:
        return {}, why + ["no measured window"]
    win = [r for r in rows if ws <= r["arrival_ns"] <= we]
    lost_srv = sum(r["gap_count"] for r in win)
    lost_cli = sum(r["evict_count"] for r in win)
    if lost_srv or lost_cli:
        why.append("lost samples in window: server ring %d, client queue %d" % (lost_srv, lost_cli))
    timelines = {r["timeline_id"] for r in win if r["kind"] != "gap"}
    if len(timelines) > 1:
        why.append("timeline changed inside the window (%d timelines)" % len(timelines))
    if summary.get("mode") == "stream":
        want = 1 if summary.get("attribution_requested") == "1" else 0
        bad = sum(1 for r in win if r["kind"] == "frame" and r["mode"] != want)
        if bad:
            why.append("%d window frames ran in an attribution mode other than the requested %d" % (bad, want))
        prev = None
        miss = 0
        for r in win:
            if r["kind"] != "frame":
                continue
            if prev is not None and r["sample_seq"] != prev + 1:
                miss += abs(r["sample_seq"] - prev - 1) or 1
            prev = r["sample_seq"]
        if miss:
            why.append("sample_seq not consecutive inside the window (%d missing)" % miss)
    f0 = newest_frame_at(rows, ws)
    f1 = newest_frame_at(rows, we)
    if f0 is None or f1 is None:
        return {}, why + ["no frame at a window edge"]
    frames = f1["frame_no"] - f0["frame_no"]
    secs = (we - ws) / 1e9
    if frames < min_frames:
        why.append("only %d frames in the window (< %d): measure more frames (run_v3.sh --frames N, N large enough "
                   "that the SLOWEST configuration reaches %d); the floor is not lowered to fit" % (frames, min_frames, min_frames))
    if secs < min_window_s:
        why.append("window %.2f s shorter than %.2f s: measure more frames (run_v3.sh --frames N, N large enough "
                   "that the FASTEST configuration takes %.2f s)" % (secs, min_window_s, min_window_s))
    m = {"frames": frames, "window_s": secs}
    if frames > 0:
        m["speed"] = frames / secs
    # CPU time per emulated frame between the two CPU samples.
    try:
        c0, c1 = int(summary["cpu_start_ticks"]), int(summary["cpu_end_ticks"])
        t0, t1 = int(summary["cpu_start_at_ns"]), int(summary["cpu_end_at_ns"])
        hz = int(summary.get("cpu_ticks_per_s", "0"))
    except (KeyError, ValueError):
        c0 = c1 = t0 = t1 = hz = -1
    if c0 >= 0 and c1 > c0 and hz > 0 and t1 > t0:
        g0, g1 = newest_frame_at(rows, t0), newest_frame_at(rows, t1)
        if g0 and g1 and g1["frame_no"] > g0["frame_no"]:
            m["cpu_ms_per_frame"] = ((c1 - c0) / hz * 1000.0) / (g1["frame_no"] - g0["frame_no"])
    return m, why


def tpf(values):
    return [1.0 / v for v in values]


def median(v):
    return statistics.median(v)


def boot_ratio(x, a, n, rng):
    """95% percentile interval of median(x)/median(a) - 1, resampling runs of each configuration."""
    out = []
    for _ in range(n):
        xs = [rng.choice(x) for _ in x]
        as_ = [rng.choice(a) for _ in a]
        out.append(median(xs) / median(as_) - 1.0)
    out.sort()
    lo = out[int(0.025 * (n - 1))]
    hi = out[int(round(0.975 * (n - 1)))]
    return lo, hi


def spread(vals):
    if len(vals) < 2:
        return {"min": min(vals), "max": max(vals), "range_pct": 0.0, "cv_pct": 0.0}
    med = median(vals)
    return {"min": min(vals), "max": max(vals),
            "range_pct": 100.0 * (max(vals) - min(vals)) / med,
            "cv_pct": 100.0 * statistics.stdev(vals) / statistics.mean(vals)}


def collect(directory, min_frames, min_window_s):
    pat = re.compile(r"^run(\d+)_([abc])\.csv$")
    runs = []
    for name in sorted(os.listdir(directory)):
        m = pat.match(name)
        if not m:
            continue
        base = os.path.join(directory, name[:-4])
        if not os.path.exists(base + ".summary"):
            runs.append({"rep": int(m.group(1)), "cfg": m.group(2), "name": name, "metrics": {},
                         "why": ["no .summary next to the CSV"]})
            continue
        summary, rows = load_run(base + ".csv", base + ".summary")
        metrics, why = evaluate_run(summary, rows, min_frames, min_window_s)
        runs.append({"rep": int(m.group(1)), "cfg": m.group(2), "name": name, "metrics": metrics, "why": why})
    return runs


def analyse(directory, min_frames=DEFAULT_MIN_FRAMES, min_window_s=DEFAULT_MIN_WINDOW_S,
            drift_pct=DEFAULT_DRIFT_PCT, boots=4000, seed=1):
    runs = collect(directory, min_frames, min_window_s)
    # run_v3.sh leaves this marker when a slot load or the client failed: the set is incomplete and
    # the interleaving is broken, so none of it counts.
    aborted = os.path.join(directory, "ABORTED")
    if os.path.exists(aborted):
        why = "run aborted: " + open(aborted).read().strip()
        for r in runs:
            r["why"] = list(r["why"]) + [why]
    result = {"runs": runs, "configs": {}, "overhead": {}, "warnings": []}
    for cfg in "abc":
        mine = [r for r in runs if r["cfg"] == cfg]
        valid = [r for r in mine if not r["why"] and r["metrics"]]
        entry = {"total": len(mine), "valid": len(valid)}
        for key in ("speed", "cpu_ms_per_frame"):
            vals = [r["metrics"][key] for r in valid if key in r["metrics"]]
            if vals:
                entry[key] = dict(spread(vals), median=median(vals), n=len(vals), values=vals)
                if len(vals) >= 3 and entry[key]["range_pct"] > drift_pct:
                    result["warnings"].append(
                        "config %s %s: repetitions differ by %.1f%% (range / median) > %.1f%%: "
                        "machine state drifted or the scene is not deterministic; do not trust small overheads"
                        % (cfg, key, entry[key]["range_pct"], drift_pct))
                # Trend across repetitions (first third vs last third), a drift the range can hide.
                ordered = [r["metrics"][key] for r in sorted(valid, key=lambda r: r["rep"]) if key in r["metrics"]]
                k = len(ordered) // 3
                if k >= 1:
                    head, tail = median(ordered[:k]), median(ordered[-k:])
                    trend = 100.0 * (tail - head) / head
                    entry[key]["trend_pct"] = trend
                    if abs(trend) > drift_pct:
                        result["warnings"].append(
                            "config %s %s: last third of the repetitions is %+.1f%% from the first third (thermal or background drift)"
                            % (cfg, key, trend))
        result["configs"][cfg] = entry
    rng = random.Random(seed)
    for cfg in "bc":
        for key in ("speed", "cpu_ms_per_frame"):
            a = result["configs"]["a"].get(key)
            x = result["configs"][cfg].get(key)
            if not a or not x:
                continue
            if key == "speed":
                av, xv = tpf(a["values"]), tpf(x["values"])
            else:
                av, xv = a["values"], x["values"]
            point = median(xv) / median(av) - 1.0
            lo, hi = boot_ratio(xv, av, boots, rng)
            ov = {"point": point, "lo": lo, "hi": hi, "n_a": len(av), "n_x": len(xv)}
            if min(len(av), len(xv)) < 3:
                result["warnings"].append(
                    "overhead %s vs a (%s): fewer than 3 valid runs in a configuration, the interval is not informative" % (cfg, key))
            result["overhead"][(cfg, key)] = ov
    return result


def fmt_pct(x):
    return "%+.2f%%" % (100.0 * x)


def report(res, meta, out=sys.stdout):
    p = lambda *a: print(*a, file=out)
    names = {"a": "(a) no client", "b": "(b) client, attribution off", "c": "(c) client, attribution on"}
    p("V3 benchmark analysis")
    for k, v in sorted(meta.items()):
        p("  meta %s = %s" % (k, v))
    p("")
    p("Runs")
    for r in sorted(res["runs"], key=lambda r: (r["rep"], r["cfg"])):
        m = r["metrics"]
        txt = "valid" if not r["why"] else "INVALID: " + "; ".join(r["why"])
        sp = "%.1f fps" % m["speed"] if "speed" in m else "-"
        cp = "%.4f ms cpu/frame" % m["cpu_ms_per_frame"] if "cpu_ms_per_frame" in m else "-"
        p("  rep %02d %s  %s  %s  %s" % (r["rep"], r["cfg"], sp, cp, txt))
    p("")
    p("Per configuration (valid runs only)")
    for cfg in "abc":
        e = res["configs"][cfg]
        p("  %s: %d of %d runs valid" % (names[cfg], e["valid"], e["total"]))
        for key, unit in (("speed", "fps"), ("cpu_ms_per_frame", "ms cpu/frame")):
            s = e.get(key)
            if s:
                p("      %-17s median %.4f  min %.4f  max %.4f  range %.2f%%  cv %.2f%%  n=%d  %s"
                  % (key, s["median"], s["min"], s["max"], s["range_pct"], s["cv_pct"], s["n"], unit))
    p("")
    p("Overhead relative to (a): extra host time per emulated frame, median ratio, 95% bootstrap interval of the median ratio")
    if not res["overhead"]:
        p("  (none computable)")
    for (cfg, key), ov in sorted(res["overhead"].items()):
        p("  %s via %-17s %s  [%s, %s]  (n_a=%d n=%d)" % (names[cfg], key, fmt_pct(ov["point"]),
                                                         fmt_pct(ov["lo"]), fmt_pct(ov["hi"]), ov["n_a"], ov["n_x"]))
    if meta.get("throttle") == "on":
        p("  note: the emulator was throttled; 'speed' is pinned at real time and carries no overhead information. Read cpu_ms_per_frame.")
    p("  note: (a) is measured with two short connections at the window edges, not with no client at all; see PROCEDURE.md.")
    p("")
    p("Decision inputs for the plan's conditional items (Docs/ProfilerFixPlan.md)")
    p("  " + PLAN_QUOTES["F12"])
    p("  " + PLAN_QUOTES["F13"])
    p("  The plan gives NO numeric threshold for either; the numbers above are the input to a human decision.")
    b = [res["overhead"].get(("b", k)) for k in ("speed", "cpu_ms_per_frame")]
    for key, ov in zip(("speed", "cpu_ms_per_frame"), b):
        if ov:
            inc = "includes 0" if ov["lo"] <= 0.0 <= ov["hi"] else "excludes 0"
            p("  F12 input, (b) vs (a) by %s: %s, interval %s [%s, %s]" % (key, fmt_pct(ov["point"]), inc, fmt_pct(ov["lo"]), fmt_pct(ov["hi"])))
    p("  F13 input: this harness has no UI in it; 'visible cost' of the panels needs a separate timing of the UI frame (not measured here).")
    p("  Not covered by this harness (needs other builds or counters): base 6491e73 speed baseline, patched build with")
    p("  instrumentation forced on versus gated off while disconnected (use --vs on a second run directory), guest counters off vs on.")
    if res["warnings"]:
        p("")
        p("Warnings")
        for w in res["warnings"]:
            p("  - " + w)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dir", nargs="?")
    ap.add_argument("--min-frames", type=int, default=DEFAULT_MIN_FRAMES)
    ap.add_argument("--min-window-s", type=float, default=DEFAULT_MIN_WINDOW_S)
    ap.add_argument("--drift-pct", type=float, default=DEFAULT_DRIFT_PCT)
    ap.add_argument("--bootstrap", type=int, default=4000)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--print-floors", action="store_true", help="print the default frame and seconds floors and exit (used by run_v3.sh)")
    ap.add_argument("--vs", metavar="DIR", help="another run directory (another build): compares its (a) runs with this one's")
    ap.add_argument("--json", metavar="FILE", help="also write the numbers as JSON")
    args = ap.parse_args(argv)
    if args.print_floors:
        print("%d %g" % (DEFAULT_MIN_FRAMES, DEFAULT_MIN_WINDOW_S))
        return 0
    if not args.dir:
        ap.error("the following arguments are required: dir")
    if not os.path.isdir(args.dir):
        print("not a directory: " + args.dir, file=sys.stderr)
        return 1
    meta = {}
    mp = os.path.join(args.dir, "run_meta.txt")
    if os.path.exists(mp):
        meta = read_kv(mp)
    res = analyse(args.dir, args.min_frames, args.min_window_s, args.drift_pct, args.bootstrap, args.seed)
    if not res["runs"]:
        print("no run*_?.csv files in " + args.dir, file=sys.stderr)
        return 1
    report(res, meta)
    if args.vs:
        other = analyse(args.vs, args.min_frames, args.min_window_s, args.drift_pct, args.bootstrap, args.seed)
        print("\nBuild comparison, configuration (a) only: this directory versus " + args.vs)
        rng = random.Random(args.seed)
        for key in ("speed", "cpu_ms_per_frame"):
            mine, theirs = res["configs"]["a"].get(key), other["configs"]["a"].get(key)
            if not mine or not theirs:
                print("  %s: not computable" % key)
                continue
            xv = tpf(mine["values"]) if key == "speed" else mine["values"]
            av = tpf(theirs["values"]) if key == "speed" else theirs["values"]
            lo, hi = boot_ratio(xv, av, args.bootstrap, rng)
            print("  %-17s this/other extra host time per frame: %s  [%s, %s]"
                  % (key, fmt_pct(median(xv) / median(av) - 1.0), fmt_pct(lo), fmt_pct(hi)))
    if args.json:
        ser = {"configs": {c: {k: v for k, v in e.items()} for c, e in res["configs"].items()},
               "overhead": {"%s:%s" % k: v for k, v in res["overhead"].items()},
               "warnings": res["warnings"],
               "runs": [{"rep": r["rep"], "cfg": r["cfg"], "valid": not r["why"], "why": r["why"], "metrics": r["metrics"]} for r in res["runs"]]}
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(ser, f, indent=1)
    return 0 if all(res["configs"][c]["valid"] > 0 for c in "abc") else 2


if __name__ == "__main__":
    sys.exit(main())
