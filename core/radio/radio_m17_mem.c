// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// The memory M17's voice runs in (radio_m17.h)
#include "config.h"
#if CONFIG_M17	// the whole file
#include <stdint.h>

#include "board.h"
#include "radio/radio_m17.h"
#include "m17/m17_bench.h"
#include "m17/m17_voice.h"

#if defined(HAL_VOCODER_LEND_BYTES)
_Static_assert(sizeof(m17_voice_mem_t) <= HAL_VOCODER_LEND_BYTES, "M17 voice does not fit the vocoder's lend");
_Static_assert(sizeof(m17_bench_mem_t) <= HAL_VOCODER_LEND_BYTES, "the benchmark does not fit the vocoder's lend");

void *radio_m17_voice_mem(size_t *len)
{
	*len = 0;
	return board.vocoder != NULL && board.vocoder->lend != NULL ? board.vocoder->lend(len) : NULL;
}
#elif CONFIG_P25
#include "radio/radio_p25.h"

void *radio_m17_voice_mem(size_t *len)
{
	return radio_p25_idle_mem(len);
}
#else
// No block to share (a build without P25): one of our own, for the voice path or the benchmark
static union {
	m17_voice_mem_t voice;
	m17_bench_mem_t bench;
	uint64_t u64;
} own __attribute__((aligned(8)));

void *radio_m17_voice_mem(size_t *len)
{
	*len = sizeof own;
	return &own;
}
#endif
#endif	// CONFIG_M17
