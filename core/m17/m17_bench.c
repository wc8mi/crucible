// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// The Codec 2 CPU benchmark (m17_bench.h)
#include <string.h>

#include "m17_bench.h"
// codec2-fixed-point's deterministic test speech and its hash (static functions, LGPL-2.1-only like the codec)
#include "../tests/signal.h"

_Static_assert(M17_BENCH_ENC_FRAMES == SIG_FRAMES, "the benchmark runs the codec's whole test signal");

static void timed(const m17_bench_io_t *io, uint32_t t0, uint32_t *total, uint32_t *max)
{
	uint32_t d = io->micros() - t0;

	*total += d;
	if (d > *max)
		*max = d;
	if (io->pause)
		io->pause();
}

void m17_bench_run(m17_bench_mem_t *m, const m17_bench_io_t *io, m17_bench_result_t *r)
{
	sig_t s;
	uint8_t b[C2FXP_BYTES_PER_FRAME];

	memset(r, 0, sizeof *r);
	r->enc_hash = r->dec_hash = FNV_INIT;
	c2fxp_encoder_init(&m->c.enc);
	sig_init(&s);
	for (unsigned f = 0; f < M17_BENCH_ENC_FRAMES; f++) {
		sig_frame(&s, (int)f, m->pcm);
		uint32_t t0 = io->micros();
		c2fxp_encode(&m->c.enc, b, m->pcm);
		timed(io, t0, &r->enc_us, &r->enc_max_us);
		r->enc_hash = fnv1a(r->enc_hash, b, sizeof b);
		if (f < M17_BENCH_DEC_FRAMES)
			memcpy(m->bits[f], b, sizeof b);
		r->enc_frames = (uint16_t)(f + 1);
	}
	c2fxp_decoder_init(&m->c.dec);
	for (unsigned f = 0; f < M17_BENCH_DEC_FRAMES; f++) {
		uint32_t t0 = io->micros();
		c2fxp_decode(&m->c.dec, m->pcm, m->bits[f]);
		timed(io, t0, &r->dec_us, &r->dec_max_us);
		r->dec_hash = fnv1a(r->dec_hash, m->pcm, sizeof m->pcm);
		r->dec_frames = (uint16_t)(f + 1);
	}
}
