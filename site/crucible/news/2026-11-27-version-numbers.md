<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 David Behnke -->
---
date: 2026-11-27
seq: 3
title: Version numbers, and scan settings
release: v0.1.0-beta.1
---
Crucible builds now have version numbers. This one is **v0.1.0-beta.1**. Test builds are named
`v0.1.0-beta.<n>` until 0.1.0 is stable. The radio shows the version on the boot screen and in About. The older
builds with dated names are no longer offered.

- **Scan delay.** Settings > Talk & listen > Scan delay sets how long the scan stays on a channel after the signal
  ends: 1 to 15 s, 5 s by default.
- **Scan resume and Scan hold.** Scan resume is Carrier, Time or Stop. With Time, the scan moves on after the Scan
  hold time (3 to 30 s, 5 s by default) even while the signal is still there.
- **From RadioFoundry:** APRS keeps the radio's own frequency the first time it sees one.

Test builds show when you select Show test builds, and also while a radio has no full release.
