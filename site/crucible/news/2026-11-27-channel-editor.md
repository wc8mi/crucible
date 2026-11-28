<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (c) 2026 David Behnke -->
---
date: 2026-11-27
title: One channel file for every radio
seq: 3
---
The new **Channels** page edits channels, zones, contacts and the DMR ID database in the browser. One file works on
all five radios.

- **Made for** picks the radios the file is for. The Problems tab names each radio that cannot use a channel: a
  frequency outside its range, or a mode it does not have.
- A channel stores its power as a step on a scale of 10. Each radio maps the step to its own levels: on 2 m, High
  gives 5W on the {{boards/dm1701}} and 50W on the {{boards/md9600}}. The channel form shows the level on each radio.
- CSV export and import keep the same power scale, so a CSV list also moves between radios.
- Build the DMR ID database, or private-call contacts, from a `user.csv` file from RadioID.net.

Nothing leaves your computer. Read and write the radio over USB, or save a file.
