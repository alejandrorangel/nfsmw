#!/usr/bin/env python3
"""Compile every game unit natively and summarise what breaks.

Usage (from repo root):
    python3 port/tools/port_report.py            # configure + build + report
    python3 port/tools/port_report.py --no-build # re-summarise last log

Writes build/port/port_report.md and prints a short summary. The goal is a
shrinking, categorised list of portability problems rather than a green build.
"""

import argparse
import collections
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BUILD = os.path.join(ROOT, "build", "port")
LOG = os.path.join(BUILD, "build.log")
REPORT = os.path.join(BUILD, "port_report.md")

DIAG_RE = re.compile(r"^(?P<file>[^:\s][^:]*):(?P<line>\d+):(?P<col>\d+): (?P<kind>error|fatal error): (?P<msg>.*)$")
UNIT_RE = re.compile(r"SourceLists/(z\w+)\.cpp")

# Rough buckets so the report shows *kinds* of work, not just counts.
CATEGORIES = [
    ("missing-header", re.compile(r"file not found")),
    ("64-bit/pointer-size", re.compile(r"loses precision|cast from pointer|static.assert|size of array|narrowing")),
    ("ms-extension", re.compile(r"__declspec|__asm|_asm|__int|__forceinline|__cdecl|__stdcall")),
    ("undeclared", re.compile(r"undeclared identifier|unknown type name|no member named|no type named|use of undeclared")),
    ("redefinition", re.compile(r"redefinition|conflicting types|typedef redefinition")),
    ("template/c++", re.compile(r"template|dependent|typename|ambiguous")),
    ("type-mismatch", re.compile(r"cannot initialize|no matching|no viable|incompatible|cannot convert|invalid operands")),
]


def categorise(msg):
    for name, rx in CATEGORIES:
        if rx.search(msg):
            return name
    return "other"


def run_build():
    os.makedirs(BUILD, exist_ok=True)
    subprocess.run(["cmake", "-S", os.path.join(ROOT, "port"), "-B", BUILD, "-G", "Ninja"], check=True,
                   stdout=subprocess.DEVNULL)
    with open(LOG, "w") as log:
        # -k 0: keep going so every unit reports its errors.
        subprocess.run(["ninja", "-C", BUILD, "-k", "0", "nfsmw_game", "nfsmw"], stdout=log, stderr=subprocess.STDOUT)


def summarise():
    units = collections.OrderedDict()
    current = None
    diags = []
    with open(LOG, errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("FAILED:"):
                m = UNIT_RE.search(line)
                current = m.group(1) if m else line.split()[-1]
                units.setdefault(current, [])
                continue
            m = DIAG_RE.match(line)
            if m and current:
                d = m.groupdict()
                d["file"] = os.path.relpath(os.path.abspath(os.path.join(BUILD, d["file"])), ROOT) \
                    if not os.path.isabs(d["file"]) else os.path.relpath(d["file"], ROOT)
                d["unit"] = current
                d["cat"] = categorise(d["msg"])
                units[current].append(d)
                diags.append(d)

    by_cat = collections.Counter(d["cat"] for d in diags)
    by_file = collections.Counter(d["file"] for d in diags)
    by_msg = collections.Counter(re.sub(r"'[^']*'", "'…'", d["msg"]) for d in diags)

    out = []
    out.append("# Native port: portability report\n")
    out.append(f"Failed targets: **{len(units)}**, total errors: **{len(diags)}**\n")
    out.append("## Errors per unit\n")
    out.append("| unit | errors | first error |")
    out.append("|---|---|---|")
    for u, ds in units.items():
        first = f"`{ds[0]['file']}:{ds[0]['line']}` {ds[0]['msg']}" if ds else "(link/other failure)"
        out.append(f"| {u} | {len(ds)} | {first[:160]} |")
    out.append("\n## By category\n")
    for c, n in by_cat.most_common():
        out.append(f"- {c}: {n}")
    out.append("\n## Hottest files\n")
    for fpath, n in by_file.most_common(25):
        out.append(f"- `{fpath}`: {n}")
    out.append("\n## Most common messages\n")
    for msg, n in by_msg.most_common(30):
        out.append(f"- ({n}) {msg}")
    with open(REPORT, "w") as f:
        f.write("\n".join(out) + "\n")

    print(f"failed targets: {len(units)}  errors: {len(diags)}")
    for c, n in by_cat.most_common():
        print(f"  {c:22} {n}")
    print(f"report: {REPORT}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-build", action="store_true")
    args = ap.parse_args()
    if not args.no_build:
        run_build()
    summarise()


if __name__ == "__main__":
    sys.exit(main())
