# SPDX-License-Identifier: MIT
# Copyright (c) 2026 David Behnke
"""Crucible's scope, read from crucible.toml at the top of the tree (CRUCIBLE.md).

    tools/crucible.py boards [--within a,b,...]   the boards Crucible ships, comma separated (those of the list given)
    tools/crucible.py matrix                      the switches CI builds off, space separated
    tools/crucible.py version                     the next release, crucible.toml [release] version (0.1.0)
    tools/crucible.py tag TAG                     exit 0 for a Crucible release tag (v0.1.0, v0.1.0-beta.1), else 1
    tools/crucible.py prerelease TAG              exit 0 when TAG is a pre-release (GitHub marks it so)
    tools/crucible.py --self-test

Tools shared with RadioFoundry call these helpers instead of carrying their own Crucible logic, so each upstream file
changes by a line or two and merges stay clean. Without crucible.toml every helper answers as RadioFoundry would.
"""
import os
import re
import sys

try:
    import tomllib
except ImportError:		# Python before 3.11: the callers that need it say so
    tomllib = None

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def config(root=ROOT):
    """crucible.toml as a dict, or None when the tree has none (upstream)."""
    path = os.path.join(root, "crucible.toml")
    if not os.path.exists(path) or tomllib is None:
        return None
    with open(path, "rb") as f:
        return tomllib.load(f)


def board_ids(root=ROOT):
    """The board ids Crucible ships, or None for every board."""
    c = config(root)
    return set(c["radios"]["boards"]) if c else None


def shown(boards, root=ROOT):
    """boards ({id: board}) narrowed to the ones Crucible ships; all of them upstream."""
    ids = board_ids(root)
    if ids is None:
        return boards
    missing = ids - set(boards)
    if missing:
        raise ValueError("crucible.toml names boards not in boards/: %s" % ", ".join(sorted(missing)))
    return {k: v for k, v in boards.items() if k in ids}


def active(root=ROOT):
    return config(root) is not None


# Crucible's release names are semantic versions (semver.org): vMAJOR.MINOR.PATCH for a release, and
# vMAJOR.MINOR.PATCH-beta.N for a test build on the way to it, which GitHub marks a pre-release and the flasher lists
# under "Show test versions". Upstream's calendar names (tools/release_version.py: v26.11) never match, so the two
# kinds of tag can't be confused. Build metadata (+...) stays out: the radio shows the name, and build.json carries
# the commits.
VERSION_RE = re.compile(r"(0|[1-9][0-9]{0,3})\.(0|[1-9][0-9]{0,3})\.(0|[1-9][0-9]{0,3})")
TAG_RE = re.compile(r"v(0|[1-9][0-9]{0,3})\.(0|[1-9][0-9]{0,3})\.(0|[1-9][0-9]{0,3})(?:-beta\.([1-9][0-9]{0,3}))?")


def release_version(root=ROOT):
    """crucible.toml [release] version: the release the next builds lead to, as MAJOR.MINOR.PATCH."""
    c = config(root) or {}
    version = str(c.get("release", {}).get("version", ""))
    if VERSION_RE.fullmatch(version) is None:
        raise ValueError("crucible.toml [release] version must be MAJOR.MINOR.PATCH, not %r" % version)
    return version


def parse_tag(tag):
    """The sort key of a Crucible release tag (newer is larger), or None when TAG isn't one. A beta sorts before
    its release: v0.1.0-beta.9 < v0.1.0 < v0.1.1-beta.1."""
    m = TAG_RE.fullmatch(tag or "")
    if m is None:
        return None
    major, minor, patch, beta = m.groups()
    return (int(major), int(minor), int(patch), (0, int(beta)) if beta else (1, 0))


def is_prerelease(tag):
    return parse_tag(tag) is not None and "-" in tag


def last_beta(version, tags):
    """The highest N among TAGS of the form v<version>-beta.N, 0 when there is none."""
    found = [parse_tag(t) for t in tags if t.startswith("v%s-beta." % version)]
    return max([k[3][1] for k in found if k is not None] or [0])


def self_test():
    for good in ("v0.1.0", "v0.1.0-beta.1", "v1.20.3", "v0.1.12-beta.40"):
        assert parse_tag(good) is not None, good
    for bad in ("0.1.0", "v0.1", "v01.1.0", "v0.1.0-beta.0", "v0.1.0-beta", "v0.1.0-rc.1", "v0.1.0+abc",
                "v0.1.0-beta.1+abc", "v26.11", "v26.12b1", "crucible-3ebdbe50-62445a3e-261127-1", ""):
        assert parse_tag(bad) is None, bad
    order = ["v0.1.0-beta.1", "v0.1.0-beta.2", "v0.1.0-beta.10", "v0.1.0", "v0.1.1-beta.1", "v0.2.0", "v1.0.0"]
    assert sorted(reversed(order), key=parse_tag) == order
    assert is_prerelease("v0.1.0-beta.3") and not is_prerelease("v0.1.0") and not is_prerelease("v26.12b1")
    assert last_beta("0.1.0", ["v0.1.0-beta.2", "v0.1.0-beta.11", "v0.1.1-beta.40", "v0.1.0", "v26.11"]) == 11
    assert last_beta("0.2.0", ["v0.1.0-beta.2"]) == 0
    assert VERSION_RE.fullmatch("0.1.0") and not VERSION_RE.fullmatch("v0.1.0") and not VERSION_RE.fullmatch("0.1")
    if config() is not None:
        release_version()
    print("crucible: release tag checks passed")


def main(argv):
    if argv[:1] == ["--self-test"]:
        self_test()
        return 0
    if argv[:1] == ["tag"] and len(argv) == 2:
        return 0 if parse_tag(argv[1]) is not None else 1
    if argv[:1] == ["prerelease"] and len(argv) == 2:
        return 0 if is_prerelease(argv[1]) else 1
    c = config()
    if c is None:
        print("crucible: no crucible.toml", file=sys.stderr)
        return 2
    if argv[:1] == ["boards"]:
        ids = sorted(c["radios"]["boards"])
        if argv[1:2] == ["--within"]:
            within = set(filter(None, argv[2].split(","))) if len(argv) > 2 else set()
            ids = [b for b in ids if b in within]
        print(",".join(ids))
        return 0
    if argv[:1] == ["matrix"]:
        print(" ".join(c.get("ci", {}).get("matrix", [])))
        return 0
    if argv[:1] == ["version"]:
        print(release_version())
        return 0
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
