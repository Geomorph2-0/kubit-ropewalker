#!/usr/bin/env python3
"""Merge CSV, lines from device_results.txt, join against results.csv, report deltas.

Usage: python3 collect_device_csv.py [--log PATH] [--static PATH] [--out PATH] [--verbose]
"""
import argparse
import csv
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent

CSV_FIELDS = [
    "tag", "stack", "model", "W", "H", "status", "arena_loc",
    "mean_ms", "min_ms", "max_ms", "arena_used_bytes", "first_out",
    "opt", "psram_mhz", "cpu_mhz", "note",
]
NUMERIC_INT = {"W", "H", "arena_used_bytes", "first_out", "psram_mhz", "cpu_mhz"}
NUMERIC_FLOAT = {"mean_ms", "min_ms", "max_ms"}
CSV_LINE_RE = re.compile(r"^CSV,")
BACKBONES = ("mobilenetv3", "custom")  # longest/most-specific first not required, no overlap


def split_backbone_size(model: str):
    for bb in BACKBONES:
        prefix = bb + "_"
        if model.startswith(prefix):
            return bb, model[len(prefix):]
    return None, model


def parse_device_log(path: Path, verbose: bool):
    rows = []
    with path.open(encoding="utf-8", errors="replace") as f:
        for lineno, raw in enumerate(f, 1):
            line = raw.rstrip("\r\n")
            if not CSV_LINE_RE.match(line):
                continue
            fields = line.split(",")
            if len(fields) != len(CSV_FIELDS):
                print(f"warn: {path}:{lineno}: expected {len(CSV_FIELDS)} fields, "
                      f"got {len(fields)} — skipping malformed line", file=sys.stderr)
                continue
            row = dict(zip(CSV_FIELDS, fields))
            try:
                for k in NUMERIC_INT:
                    row[k] = int(row[k])
                for k in NUMERIC_FLOAT:
                    row[k] = float(row[k])
            except ValueError as e:
                print(f"warn: {path}:{lineno}: numeric parse failed ({e}) — skipping",
                      file=sys.stderr)
                continue
            backbone, size = split_backbone_size(row["model"])
            if backbone is None:
                print(f"warn: {path}:{lineno}: unrecognized model prefix "
                      f"'{row['model']}' — skipping", file=sys.stderr)
                continue
            row["backbone"] = backbone
            row["size"] = size
            row["_lineno"] = lineno
            row["_raw"] = line
            rows.append(row)
    return rows


def write_device_csv_txt(rows, out_path: Path):
    seen = set()
    with out_path.open("w", encoding="utf-8") as f:
        for row in rows:
            if row["_raw"] in seen:
                continue
            seen.add(row["_raw"])
            f.write(row["_raw"] + "\n")


def canonicalize(rows, verbose: bool):
    groups = {}
    for row in rows:
        key = (row["stack"], row["backbone"], row["size"])
        groups.setdefault(key, []).append(row)

    canonical = {}
    for key, group_rows in groups.items():
        canonical[key] = group_rows[-1]
        if verbose and len(group_rows) > 1:
            superseded = len(group_rows) - 1
            stack, backbone, size = key
            print(f"note: {backbone}_{size}/{stack}: {superseded} earlier run(s) "
                  f"superseded, latest note={canonical[key]['note']}", file=sys.stderr)
    return canonical


def load_static(path: Path):
    static = {}
    with path.open(newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            static[(row["backbone"], row["size"])] = row
    return static


def pct_delta(a, b):
    """(a - b) / b * 100, or None if either side is missing/zero-baseline."""
    if a is None or b is None or b == 0:
        return None
    return (a - b) / b * 100.0


def build_merged(canonical, static):
    keys = set(static.keys())
    for (stack, backbone, size) in canonical.keys():
        keys.add((backbone, size))

    merged = []
    for backbone, size in sorted(keys, key=lambda bs: (bs[0], _pixel_count(bs[1]))):
        srow = static.get((backbone, size))
        idf = canonical.get(("idf", backbone, size))
        ard = canonical.get(("arduino", backbone, size))

        out = {
            "backbone": backbone,
            "size": size,
            "W": (srow["W"] if srow else (idf or ard or {}).get("W", "")),
            "H": (srow["H"] if srow else (idf or ard or {}).get("H", "")),
            "arena_est_kb": srow["arena_est_kb"] if srow else "",
            "val_acc": srow["val_acc"] if srow else "",
            "test_acc": srow["test_acc"] if srow else "",
            "test_auc": srow["test_auc"] if srow else "",
            "host_int8_mean_ms": srow["int8_mean_ms"] if srow else "",
            "idf_status": idf["status"] if idf else "",
            "idf_mean_ms": idf["mean_ms"] if idf else "",
            "idf_min_ms": idf["min_ms"] if idf else "",
            "idf_max_ms": idf["max_ms"] if idf else "",
            "idf_arena_used_bytes": idf["arena_used_bytes"] if idf else "",
            "idf_arena_loc": idf["arena_loc"] if idf else "",
            "arduino_status": ard["status"] if ard else "",
            "arduino_mean_ms": ard["mean_ms"] if ard else "",
            "arduino_min_ms": ard["min_ms"] if ard else "",
            "arduino_max_ms": ard["max_ms"] if ard else "",
            "arduino_arena_used_bytes": ard["arena_used_bytes"] if ard else "",
            "arduino_arena_loc": ard["arena_loc"] if ard else "",
        }

        idf_ok = idf and idf["status"] == "OK"
        ard_ok = ard and ard["status"] == "OK"
        delta_ms = None
        delta_pct = None
        if idf_ok and ard_ok:
            delta_ms = idf["mean_ms"] - ard["mean_ms"]
            delta_pct = pct_delta(idf["mean_ms"], ard["mean_ms"])
        out["delta_idf_vs_arduino_ms"] = f"{delta_ms:.2f}" if delta_ms is not None else ""
        out["delta_idf_vs_arduino_pct"] = f"{delta_pct:.1f}" if delta_pct is not None else ""

        arena_delta_pct = None
        if srow and srow.get("arena_est_kb"):
            actual_bytes = None
            if idf_ok:
                actual_bytes = idf["arena_used_bytes"]
            elif ard_ok:
                actual_bytes = ard["arena_used_bytes"]
            if actual_bytes:
                est_bytes = float(srow["arena_est_kb"]) * 1024.0
                arena_delta_pct = pct_delta(est_bytes, actual_bytes)
        out["delta_arena_est_vs_idf_actual_pct"] = (
            f"{arena_delta_pct:.1f}" if arena_delta_pct is not None else ""
        )

        merged.append(out)
    return merged


def _pixel_count(size: str) -> int:
    if "x" in size:
        w, h = size.split("x")
        return int(w) * int(h)
    return int(size) ** 2


MERGED_FIELDS = [
    "backbone", "size", "W", "H", "arena_est_kb", "val_acc", "test_acc", "test_auc",
    "host_int8_mean_ms", "idf_status", "idf_mean_ms", "idf_min_ms", "idf_max_ms",
    "idf_arena_used_bytes", "idf_arena_loc", "arduino_status", "arduino_mean_ms",
    "arduino_min_ms", "arduino_max_ms", "arduino_arena_used_bytes", "arduino_arena_loc",
    "delta_idf_vs_arduino_ms", "delta_idf_vs_arduino_pct", "delta_arena_est_vs_idf_actual_pct",
]


def write_merged_csv(merged, out_path: Path):
    with out_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=MERGED_FIELDS)
        writer.writeheader()
        for row in merged:
            writer.writerow(row)


def print_report(merged):
    print("\n=== Per-model device vs static comparison ===")
    header = (f"{'model':<22}{'W':>5}{'H':>5}{'idf_ms':>10}{'arduino_ms':>12}"
              f"{'arena_est_kb':>14}{'idf_bytes':>12}{'arena_low_%':>12}")
    print(header)
    for row in merged:
        model = f"{row['backbone']}_{row['size']}"
        idf_ms = row["idf_mean_ms"] if row["idf_mean_ms"] != "" else "-"
        ard_ms = row["arduino_mean_ms"] if row["arduino_mean_ms"] != "" else "-"
        arena_est = row["arena_est_kb"] if row["arena_est_kb"] != "" else "-"
        arena_bytes = row["idf_arena_used_bytes"] if row["idf_arena_used_bytes"] != "" else "-"
        arena_pct = row["delta_arena_est_vs_idf_actual_pct"]
        arena_pct = f"{arena_pct}%" if arena_pct != "" else "-"
        print(f"{model:<22}{row['W']:>5}{row['H']:>5}{str(idf_ms):>10}{str(ard_ms):>12}"
              f"{str(arena_est):>14}{str(arena_bytes):>12}{arena_pct:>12}")

    print("\n=== idf vs arduino ===")
    both = [r for r in merged if r["delta_idf_vs_arduino_pct"] != ""]
    total = len(merged)
    if not both:
        print(f"no arduino data yet (0/{total} models have both idf and arduino OK runs)")
    else:
        deltas_pct = [float(r["delta_idf_vs_arduino_pct"]) for r in both]
        deltas_ms = [float(r["delta_idf_vs_arduino_ms"]) for r in both]
        mean_pct = sum(deltas_pct) / len(deltas_pct)
        mean_ms = sum(deltas_ms) / len(deltas_ms)
        faster = "idf" if mean_ms < 0 else "arduino"
        print(f"{len(both)}/{total} models have both stacks. "
              f"mean delta = {mean_ms:+.2f} ms ({mean_pct:+.1f}%), "
              f"{faster} faster on average")

    print("\n=== arena estimate vs device-measured (results.csv arena_est_kb) ===")
    arena_rows = [r for r in merged if r["delta_arena_est_vs_idf_actual_pct"] != ""]
    if not arena_rows:
        print("no device arena data available to compare against static estimates")
    else:
        pcts = [float(r["delta_arena_est_vs_idf_actual_pct"]) for r in arena_rows]
        mean_pct = sum(pcts) / len(pcts)
        max_low = min(pcts)  # most negative = most underestimated
        worst = next(r for r in arena_rows
                     if float(r["delta_arena_est_vs_idf_actual_pct"]) == max_low)
        print(f"{len(arena_rows)}/{len(merged)} models compared. "
              f"mean estimate delta = {mean_pct:+.1f}%, "
              f"worst = {worst['backbone']}_{worst['size']} ({max_low:+.1f}%)")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--log", type=Path, default=HERE / "device_results.txt")
    ap.add_argument("--static", type=Path, default=HERE / "results.csv")
    ap.add_argument("--out", type=Path, default=HERE / "device_merged.csv")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if not args.log.is_file():
        print(f"error: log file not found: {args.log}", file=sys.stderr)
        return 1
    if not args.static.is_file():
        print(f"error: static csv not found: {args.static}", file=sys.stderr)
        return 1

    rows = parse_device_log(args.log, args.verbose)
    if not rows:
        print(f"error: no CSV, lines parsed from {args.log}", file=sys.stderr)
        return 1

    write_device_csv_txt(rows, HERE / "device_csv.txt")
    canonical = canonicalize(rows, args.verbose)
    static = load_static(args.static)
    merged = build_merged(canonical, static)
    write_merged_csv(merged, args.out)
    print_report(merged)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
