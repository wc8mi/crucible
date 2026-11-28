// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// RADIO op 45, the Codec 2 benchmark (docs/spec/debuglink.md 3.8; ADR 006 5, the CPU gate), and its keys in the m17
// section (debuglink_view_m17.c).
// The codec needs more stack than the debug link's task has, so the op starts a task of its own (its stack from the
// RTOS heap, freed when the run ends); the reply doesn't wait. The codec runs in the block M17's voice will run in
// (radio_m17_voice_mem), so it is timed in that memory; the block is free only on an FM channel, without a scan. The
// run pauses a tick between frames, so the radio goes on; the timing is hal_micros (the CPU's cycle counter on both
// platforms).
#include "config.h"
#include "debuglink/debuglink.h"
#include "debuglink/debuglink_proto.h"
#if CONFIG_M17
#include "FreeRTOS.h"
#include "task.h"
#include "hal_time.h"
#include "debuglink/dlog.h"
#include "m17/m17_bench.h"
#include "hostlink/hostlink_view.h"
#include "radio/radio.h"
#include "radio/radio_m17.h"

// make stack-check: 1,460 B deep on Cortex-M4 (the decoder), with room for the port and the margin
#define M17_BENCH_STACK		512u	// words
// Above the UI, input and host link tasks, so their work doesn't count in a frame's time; the radio task and the
// audio task still run at their own priority, and everything runs in the pause between frames
#define M17_BENCH_PRIORITY	(tskIDLE_PRIORITY + 4)

enum { BENCH_IDLE, BENCH_RUNNING, BENCH_DONE, BENCH_NOMEM, BENCH_NOT_FM };
static const char *const bench_names[] = { "idle", "running", "done", "nomem", "not_fm" };

static volatile uint8_t bench_state;
static uint16_t bench_runs;
static m17_bench_result_t bench_res;

static void bench_pause(void)
{
	vTaskDelay(1);
}

static void bench_task(void *arg)
{
	static const m17_bench_io_t io = { hal_micros, bench_pause };
	m17_bench_run(arg, &io, &bench_res);	// the block goes back as it is: its next user sets it up again
	bench_runs++;
	bench_state = BENCH_DONE;
	vTaskDelete(NULL);
}

uint8_t debuglink_m17_bench(void)
{
	if (bench_state == BENCH_RUNNING) {
		return DL_ST_BUSY;
	}
	const radio_view_t *v = &hostlink_view;	// the debug link's own copy: large, and refreshed here
	size_t len = 0;

	radio_get_view(&hostlink_view);
	if (v->ch.mode != RADIO_MODE_FM || v->scan != 0 || v->hotspot) {
		bench_state = BENCH_NOT_FM;	// another mode, or one a scan may switch to, holds the block
		return DL_ST_REFUSED;
	}
	void *m = radio_m17_voice_mem(&len);
	if (m == NULL || len < sizeof(m17_bench_mem_t) || ((uintptr_t)m & 7u) != 0) {
		dlog("debuglink: m17 bench, voice block %u of %u bytes", (unsigned)len, (unsigned)sizeof(m17_bench_mem_t));
		bench_state = BENCH_NOMEM;
		return DL_ST_UNAVAILABLE;
	}
	bench_state = BENCH_RUNNING;	// before the task: it runs at once, above this one
	if (xTaskCreate(bench_task, "m17b", M17_BENCH_STACK, m, M17_BENCH_PRIORITY, NULL) != pdPASS) {
		dlog("debuglink: m17 bench, no heap for its stack");
		bench_state = BENCH_NOMEM;
		return DL_ST_BUSY;
	}
	return DL_ST_OK;
}

// The share of a 20 ms frame, in percent
static unsigned pct(uint32_t us)
{
	return (unsigned)((us * 100u + M17_BENCH_FRAME_US / 2u) / M17_BENCH_FRAME_US);
}

void debuglink_view_m17_bench(dl_text_t *t)
{
	m17_bench_result_t r = bench_res;	// a run in progress may tear a counter: its frames say so
	uint32_t ea = r.enc_frames ? r.enc_us / r.enc_frames : 0, da = r.dec_frames ? r.dec_us / r.dec_frames : 0;

	dl_text(t, "m17.bench=%s\nm17.bench_runs=%u\nm17.bench_ok=%u\n", bench_names[bench_state], bench_runs,
		bench_state == BENCH_DONE && m17_bench_ok(&r));
	dl_text(t, "m17.enc_frames=%u\nm17.enc_us=%lu\nm17.enc_max_us=%lu\nm17.enc_pct=%u\nm17.enc_max_pct=%u\n",
		r.enc_frames, (unsigned long)ea, (unsigned long)r.enc_max_us, pct(ea), pct(r.enc_max_us));
	dl_text(t, "m17.dec_frames=%u\nm17.dec_us=%lu\nm17.dec_max_us=%lu\nm17.dec_pct=%u\nm17.dec_max_pct=%u\n",
		r.dec_frames, (unsigned long)da, (unsigned long)r.dec_max_us, pct(da), pct(r.dec_max_us));
	dl_text(t, "m17.enc_hash=%08lx\nm17.dec_hash=%08lx\n", (unsigned long)r.enc_hash, (unsigned long)r.dec_hash);
}
#endif	// CONFIG_M17
