<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 David Behnke -->
---
date: 2026-11-27
seq: 1
title: A fifth radio and the NXDN radio default
release: crucible-4e24a5e1-8a658d0d-261127-2
---
- **A fifth radio:** the Radioddity mobile radio in the radio list on this page. It runs RadioFoundry's image with
  Crucible's features on. Crucible's own features (Digital AGC, the NXDN radio default) have not run on it yet. Only
  radios of the 909E.DBB batch are tested.
- **NXDN radio default.** Settings > Talk & listen > NXDN sets Rate, RX RAN, TX RAN and Talkgroup for the VFO and for
  channels without their own NXDN settings. The default rate is now 4800 bit/s. Contacts > Talk to now works in the
  NXDN VFO, and quick settings tile 8 switches between 4800 and 9600.
- **Mic input moved** into Crucible's own settings block, because RadioFoundry took the byte it used. Radios on
  earlier Crucible releases keep their setting.
- **From RadioFoundry 4e24a5e1:** an APRS custom frequency, a received call alert that rings, Up/Down stepping the
  talk target in dual watch, and smaller fixes.

The settings record grows to 436 bytes. If you go back to RadioFoundry later, reset the settings.
