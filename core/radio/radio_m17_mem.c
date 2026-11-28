// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// The memory M17's voice and engine run in (radio_m17.h)
#include "config.h"
#if CONFIG_M17	// the whole file
#include <stdint.h>

#include "board.h"
#include "radio/radio_m17.h"
#include "m17/m17_bench.h"
#include "m17/m17_voice.h"
#include "m17/m17_engine.h"

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

// The engine. Where the voice takes P25's idle memory, the engine can't share it: YSF's own block where VW is built (idle
// while M17 runs), else one of M17's own. Elsewhere the block the NXDN and YSF engines share, which holds M17's too
// (radio_air_mem.c), or one of M17's own in a build with neither
#if CONFIG_P25 && !defined(HAL_VOCODER_LEND_BYTES) && CONFIG_YSF_VW
#include "radio/radio_air.h"
#include "ysf/ysf_engine.h"
_Static_assert(sizeof(m17_engine_t) <= sizeof(ysf_engine_t), "the M17 engine does not fit YSF's block");

void *radio_m17_engine_mem(size_t *len)
{
	return radio_air_ysf_mem(len);
}
#elif (CONFIG_NXDN || CONFIG_YSF) && !(CONFIG_P25 && !defined(HAL_VOCODER_LEND_BYTES))
#include "radio/radio_air.h"

void *radio_m17_engine_mem(size_t *len)
{
	return radio_air_engine_mem(len);
}
#else
static m17_engine_t eng_mem __attribute__((aligned(8)));

void *radio_m17_engine_mem(size_t *len)
{
	*len = sizeof eng_mem;
	return &eng_mem;
}
#endif
#endif	// CONFIG_M17
