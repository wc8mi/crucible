<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 David Behnke -->
---
date: 2026-11-27
seq: 3
title: Version numbers, and scan settings
release: v0.1.0
---
Crucible builds now have version numbers. The first is **v0.1.0**. A fix raises the last number (0.1.1), and new
features raise the middle one (0.2.0). The radio shows the version on the boot screen and in About. Every version is a
test release for now. The older builds with dated names are no longer offered.

- **Scan delay.** Settings > Talk & listen > Scan delay sets how long the scan stays on a channel after the signal
  ends: 1 to 15 s, 5 s by default.
- **Scan resume and Scan hold.** Scan resume is Carrier, Time or Stop. With Time, the scan moves on after the Scan
  hold time (3 to 30 s, 5 s by default) even while the signal is still there.
- **From RadioFoundry:** APRS keeps the radio's own frequency the first time it sees one.
- **Satellite TLE page:** a second source, PE0SAT's amateur list, next to AMSAT.

Test releases show when you select Show test builds, and also while a radio has no full release.
