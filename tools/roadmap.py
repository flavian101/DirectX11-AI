#!/usr/bin/env python3
"""Tracks docs/ROADMAP.md so the roadmap cannot drift away from the code.

  tools/roadmap.py status                     where the roadmap stands, per milestone, and what is next
  tools/roadmap.py check                      validate the roadmap file itself (this is what `ctest` runs)
  tools/roadmap.py check --range origin/master..HEAD
                                              also validate commits: every status change must be declared in the
                                              commit message, and every declaration must be true
  tools/roadmap.py history                    every status change, commit by commit

Item format in docs/ROADMAP.md (one header line, then indented detail lines):

    - **H1** `[~]` Title
      - Tests: `tests/height_raster_test.cpp`
      - Acceptance: ...
      - Editor: 2026-12-01 what was seen        (required for `[x]`)

States: `[ ]` open, `[~]` code and unit tests landed, NOT yet checked in the editor, `[x]` checked in the editor (needs an
Editor line with a date), `[!]` known bug.

Commit trailer, one line per item whose state the commit changes (the message is the changelog of the roadmap):

    Roadmap: H1 [ ] -> [~]

Pure Python 3, standard library only. Run from the repository root.
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

ROADMAP = "docs/ROADMAP.md"
CMAKE = "CMakeLists.txt"
STATES = " ~x!"
ITEM = re.compile(r"^- \*\*([A-Z]+[0-9]+)\*\* `\[([ ~x!])\]` (.*)$")
MILESTONE = re.compile(r"^### (M[0-9]+)\. (.*)$")
TRAILER = re.compile(r"^Roadmap: ([A-Z]+[0-9]+) \[([ ~x!])\] -> \[([ ~x!])\]\s*$")
DATE = re.compile(r"^[0-9]{4}-[0-9]{2}-[0-9]{2}\b")


class Item:
    def __init__(self, ident, state, title, milestone, line):
        self.ident, self.state, self.title, self.milestone, self.line = ident, state, title, milestone, line
        self.tests = []
        self.editor = None


def parse(text):
    items, milestone, current = [], "-", None
    for number, raw in enumerate(text.splitlines(), 1):
        m = MILESTONE.match(raw)
        if m:
            milestone, current = m.group(1), None
            continue
        m = ITEM.match(raw)
        if m:
            current = Item(m.group(1), m.group(2), m.group(3).strip(), milestone, number)
            items.append(current)
            continue
        if current is None:
            continue
        if raw.startswith("  - "):
            body = raw[4:]
            if body.startswith("Tests:"):
                current.tests += re.findall(r"`([^`]+)`", body)
            elif body.startswith("Editor:"):
                current.editor = body[len("Editor:"):].strip()
        elif not raw.startswith("  "):
            current = None   # a blank line or any unindented line ends the item's detail lines
    return items


def git(*args):
    result = subprocess.run(["git", *args], capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError("git " + " ".join(args) + ": " + result.stderr.strip())
    return result.stdout


def roadmap_at(rev):
    try:
        text = git("show", f"{rev}:{ROADMAP}")
    except RuntimeError:
        return []   # the file did not exist yet at that commit
    # Only a file in the tracked format counts: an older roadmap (before this tool) is not parsed as if it were one.
    return parse(text) if "tools/roadmap.py" in text else []


def read_working():
    path = Path(ROADMAP)
    if not path.exists():
        sys.exit(f"{ROADMAP} not found: run from the repository root")
    return parse(path.read_text(encoding="utf-8"))


def transitions(before, after):
    old = {i.ident: i.state for i in before}
    return {i.ident: (old[i.ident], i.state) for i in after if i.ident in old and old[i.ident] != i.state}


# ------------------------------------------------------------------------------------------------ status
def cmd_status(_):
    items = read_working()
    if not items:
        sys.exit(f"no items found in {ROADMAP}")
    names = {"-": ""}
    for raw in Path(ROADMAP).read_text(encoding="utf-8").splitlines():
        m = MILESTONE.match(raw)
        if m:
            names[m.group(1)] = m.group(2)
    order = []
    for i in items:
        if i.milestone not in order:
            order.append(i.milestone)
    label = {" ": "open", "~": "awaiting editor check", "x": "done", "!": "bug"}
    total = {s: 0 for s in STATES}
    for ms in order:
        mine = [i for i in items if i.milestone == ms]
        counts = {s: sum(1 for i in mine if i.state == s) for s in STATES}
        for s in STATES:
            total[s] += counts[s]
        print(f"{ms}  {names.get(ms, '')}   [{counts['x']}/{len(mine)} done]")
        for i in mine:
            print(f"   [{i.state}] {i.ident:<4} {i.title}")
    print()
    print("total: " + "; ".join(f"{total[s]} {label[s]}" for s in STATES))
    nxt = next((i for i in items if i.state in " !"), None)
    waiting = [i.ident for i in items if i.state == "~"]
    if waiting:
        print("waiting for an editor check: " + ", ".join(waiting))
    if nxt:
        print(f"next open item: {nxt.ident}  {nxt.title}")


# ------------------------------------------------------------------------------------------------ check
def check_file(items):
    problems, seen = [], set()
    cmake = Path(CMAKE).read_text(encoding="utf-8") if Path(CMAKE).exists() else ""
    for i in items:
        where = f"{ROADMAP}:{i.line} {i.ident}"
        if i.ident in seen:
            problems.append(f"{where}: duplicate id")
        seen.add(i.ident)
        if i.state in "~x":
            if not i.tests:
                problems.append(f"{where}: state [{i.state}] needs a `Tests:` line")
            for t in i.tests:
                if not Path(t).exists():
                    problems.append(f"{where}: test file {t} does not exist")
                elif t not in cmake:
                    problems.append(f"{where}: test file {t} is not registered in {CMAKE}")
        if i.state == "x" and not (i.editor and DATE.match(i.editor)):
            problems.append(f"{where}: [x] needs `Editor: YYYY-MM-DD what was seen`")
        if i.state != "x" and i.editor:
            problems.append(f"{where}: has an Editor line but is not [x]")
    return problems


def check_commits(rng):
    problems = []
    for commit in git("rev-list", "--reverse", rng).split():
        short = commit[:8]
        parents = git("rev-list", "--parents", "-n", "1", commit).split()[1:]
        before = roadmap_at(parents[0]) if parents else []
        after = roadmap_at(commit)
        actual = transitions(before, after)
        declared = {}
        for line in git("show", "-s", "--format=%B", commit).splitlines():
            m = TRAILER.match(line)
            if m:
                declared[m.group(1)] = (m.group(2), m.group(3))
        for ident, pair in declared.items():
            if ident not in actual:
                problems.append(f"{short}: declares {ident} [{pair[0]}] -> [{pair[1]}] but {ROADMAP} does not change it")
            elif actual[ident] != pair:
                problems.append(f"{short}: declares {ident} [{pair[0]}] -> [{pair[1]}] but it went [{actual[ident][0]}] -> [{actual[ident][1]}]")
        for ident, pair in actual.items():
            if ident not in declared:
                problems.append(f"{short}: {ident} went [{pair[0]}] -> [{pair[1]}] with no `Roadmap:` line in the message")
    return problems


def cmd_check(args):
    items = read_working()
    problems = [] if items else [f"no items found in {ROADMAP}"]
    problems += check_file(items)
    if args.range:
        problems += check_commits(args.range)
    for p in problems:
        print("roadmap: " + p)
    if problems:
        sys.exit(1)
    print(f"roadmap ok: {len(items)} items" + (f", commits {args.range} consistent" if args.range else ""))


# ------------------------------------------------------------------------------------------------ history
def cmd_history(_):
    commits = git("log", "--reverse", "--format=%H%x09%h%x09%s", "--", ROADMAP).splitlines()
    previous = []
    for row in commits:
        full, short, subject = row.split("\t", 2)
        current = roadmap_at(full)
        if not current:
            continue
        lines = []
        old_ids, new_ids = {i.ident for i in previous}, {i.ident for i in current}
        for ident, (a, b) in transitions(previous, current).items():
            lines.append(f"   {ident:<4} [{a}] -> [{b}]")
        if previous:
            lines += [f"   + {i}" for i in sorted(new_ids - old_ids)] + [f"   - {i}" for i in sorted(old_ids - new_ids)]
        else:
            lines.append(f"   {len(current)} items")
        print(f"{short} {subject}")
        print("\n".join(lines) if lines else "   (no status change)")
        previous = current


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("status").set_defaults(run=cmd_status)
    check = sub.add_parser("check")
    check.add_argument("--range", help="a git revision range, e.g. origin/master..HEAD")
    check.set_defaults(run=cmd_check)
    sub.add_parser("history").set_defaults(run=cmd_history)
    args = parser.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
