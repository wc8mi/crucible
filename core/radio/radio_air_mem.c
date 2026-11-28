// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 David Behnke
// The memory the NXDN and YSF engines run in (radio_air.h)
#include "config.h"
#if CONFIG_NXDN || CONFIG_YSF	// the whole file
#include "board.h"
#include "radio/radio_air.h"
#if CONFIG_P25 && !defined(HAL_VOCODER_LEND_BYTES)
#include "radio/radio_p25.h"

// P25's memory is idle while another air mode runs: no block of our own
void *radio_air_engine_mem(size_t *len)
{
	return radio_p25_idle_mem(len);
}
#if CONFIG_YSF_VW
#include "ysf/ysf_engine.h"

// A VW call's voice runs in P25's memory, which is too small to hold the YSF engine beside it: the engine's own block,
// in the board's CPU RAM where it has one (only the CPU touches the engine: the provider copies the symbols it queues).
// It is taken from the RTOS heap there; the link fails below the heap's floor
static ysf_engine_t ysf_mem RFOUNDRY_CPU_RAM __attribute__((aligned(8)));

void *radio_air_ysf_mem(size_t *len)
{
	*len = sizeof ysf_mem;
	return &ysf_mem;
}

void *radio_air_imbe_mem(size_t *len)
{
	return radio_p25_idle_mem(len);
}
#endif
#else
#if CONFIG_NXDN
#include "nxdn/nxdn_engine.h"
#endif
#if CONFIG_YSF
#include "ysf/ysf_engine.h"
#endif
#if CONFIG_M17
#include "m17/m17_engine.h"
#endif

// One block for whichever of them runs, 8-aligned as their users check (a uint64_t member is 4-aligned on some
// cores: C-SKY's ABI, where the engines then refused the block)
typedef union {
#if CONFIG_NXDN
	nxdn_engine_t nxdn;
#endif
#if CONFIG_YSF
	ysf_engine_t ysf;
#endif
#if CONFIG_M17
	m17_engine_t m17;	// radio_m17_mem.c
#endif
	uint64_t u64;
} engine_mem_t;
static engine_mem_t mem __attribute__((aligned(8)));
_Static_assert(_Alignof(engine_mem_t) <= 8, "the block is aligned to 8 below its members' needs");

void *radio_air_engine_mem(size_t *len)
{
	*len = sizeof mem;
	return &mem;
}
#if CONFIG_YSF_VW
void *radio_air_ysf_mem(size_t *len)
{
	return radio_air_engine_mem(len);
}

// The DMR side's codec is stopped for the call: its working memory is free (hal_vocoder_t.lend)
void *radio_air_imbe_mem(size_t *len)
{
	*len = 0;
	return board.vocoder != NULL && board.vocoder->lend != NULL ? board.vocoder->lend(len) : NULL;
}
#endif
#endif
#endif	// CONFIG_NXDN || CONFIG_YSF
