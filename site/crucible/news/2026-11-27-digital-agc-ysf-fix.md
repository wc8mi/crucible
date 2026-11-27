<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 David Behnke -->
---
date: 2026-11-27
title: Digital AGC and a YSF receive fix
release: crucible-3ebdbe50-62445a3e-261127-1
---
- **Digital AGC.** Two new switches in Settings > Talk & listen: Digital AGC RX and Digital AGC TX. Both start On and
  use each radio's own default levels. Off, the per-mode rows apply (DMR, P25, NXDN and YSF). Your earlier DMR and
  YSF values are kept as the manual values.
- **YSF receive fix.** After loud audio the CTCSS filter could ring, and the radio lost sync until you changed
  channel. It no longer does.
- **From RadioFoundry 3ebdbe50:** VFO digits typed on B in dual watch go to the frequency entry, a second speaker
  output for beeps while DMR transmits, and debug link tuning over the air.

The settings record grows by 4 bytes. If you go back to RadioFoundry later, reset the settings.
