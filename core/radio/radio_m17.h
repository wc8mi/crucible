// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// M17 in the radio core (docs/spec/m17.md; docs/crucible/adr/006-m17.md). Step 1: the memory its voice path runs in
#ifndef RFOUNDRY_RADIO_M17_H
#define RFOUNDRY_RADIO_M17_H

#include <stddef.h>

#include "config.h"

#if CONFIG_M17
// The block M17's voice runs in (ADR 006 4), 8-aligned: what the board's vocoder lends where it lends (the DMR side's
// codec is stopped), else P25's memory while P25 idles, else a block of its own. Free only while no other air mode
// runs: the NXDN and YSF engines use P25's, DMR voice the vocoder's. *len: its size; NULL: none now
void *radio_m17_voice_mem(size_t *len);
#endif

#endif
