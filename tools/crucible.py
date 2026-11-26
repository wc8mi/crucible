# SPDX-License-Identifier: MIT
# Copyright (c) 2026 David Behnke
"""Crucible's scope, read from crucible.toml at the top of the tree (CRUCIBLE.md).

Tools shared with RadioFoundry call these helpers instead of carrying their own Crucible logic, so each upstream file
changes by a line or two and merges stay clean. Without crucible.toml every helper answers as RadioFoundry would.
"""
import os

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
