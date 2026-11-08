#!/usr/bin/env python3
"""Prints the commit a perf JSON came from; exits non-zero if it is not from this run."""
import json
import sys

d = json.load(open(sys.argv[1]))
if d.get("run_id") != sys.argv[2]:
    sys.exit(f"PERF RUN FAILED: {sys.argv[1]}: run id {d.get('run_id')}, expected {sys.argv[2]}")
print(d.get("commit"))
