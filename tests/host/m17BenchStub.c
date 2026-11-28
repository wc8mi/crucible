// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// The Codec 2 benchmark op for the host's radio core tests: the firmware's (core/debuglink/debuglink_m17_bench.c)
// runs in an RTOS task of its own; m17VoiceTest runs the benchmark itself
#include "config.h"
#include "debuglink/debuglink.h"
#include "debuglink/debuglink_proto.h"

uint8_t debuglink_m17_bench(void)
{
	return DL_ST_UNAVAILABLE;
}

void debuglink_view_m17_bench(dl_text_t *t)
{
	dl_text(t, "m17.bench=idle\n");
}
