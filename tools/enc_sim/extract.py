#!/usr/bin/env python3
"""Extract the encoder decoder slice from the real sle_client.c for host simulation.

Usage: extract.py <sle_client.c> <out.inc>

The slice runs from the pin defines up to (excluding) the task function, i.e. it holds
every line of decoder logic that actually ships in the firmware. Nothing is copied by
hand: if the firmware source changes, re-run this and the simulation tests the new code.
"""
import sys

src, dst = sys.argv[1], sys.argv[2]
lines = open(src, encoding="utf-8").read().split("\n")

start = next(i for i, l in enumerate(lines) if l.startswith("#define AIM_ENC_A_PIN"))
end = next(i for i, l in enumerate(lines) if l.startswith("static int aim_enc_task(void *arg)"))
assert start < end, (start, end)

open(dst, "w", encoding="utf-8").write("\n".join(lines[start:end]) + "\n")
print(f"extracted lines {start + 1}..{end} ({end - start} lines) -> {dst}")
