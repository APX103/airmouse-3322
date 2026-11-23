#!/usr/bin/env python3
"""Extract the motion pipeline slice from the real sle_client.c for host simulation.

Usage: extract.py <sle_client.c> <out.inc>

The slice holds the tuning macros, the sub-pixel accumulators and aim_ekf_step(), i.e. the
code that turns gyro samples into per-tick cursor deltas. Nothing is copied by hand: if the
firmware source changes, re-run this and the simulation tests the new code.
"""
import sys

src, dst = sys.argv[1], sys.argv[2]
lines = open(src, encoding="utf-8").read().split("\n")

gain = next(i for i, l in enumerate(lines) if l.startswith("#define AIM_GAIN_DEFAULT"))
start = next(i for i, l in enumerate(lines) if l.startswith("#define AIM_EKF_FS"))
end = next(i for i, l in enumerate(lines) if l.startswith("static osal_task *g_aim_task"))
assert gain < start < end, (gain, start, end)

out = [lines[gain], ""] + lines[start:end]
open(dst, "w", encoding="utf-8").write("\n".join(out) + "\n")
print(f"extracted gain line {gain + 1} and lines {start + 1}..{end} ({len(out)} lines) -> {dst}")
