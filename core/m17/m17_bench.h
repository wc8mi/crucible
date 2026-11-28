// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// The Codec 2 CPU benchmark (docs/spec/debuglink.md, M17_BENCH; ADR 006 5, the CPU gate): codec2-fixed-point's own
// test speech (300 frames of 20 ms, tests/signal.h) through the encoder, then the first M17_BENCH_DEC_FRAMES of its
// bits through the decoder, each frame timed. The hashes of what they made say the codec computed on this CPU what it
// computes on a host (it is integer-only: the same everywhere). No RTOS or board call: the caller gives the clock, the
// pause between frames and the memory
#ifndef RFOUNDRY_M17_BENCH_H
#define RFOUNDRY_M17_BENCH_H

#include <stdbool.h>
#include <stdint.h>

#include <codec2/codec2_fxp.h>

#define M17_BENCH_ENC_FRAMES	300u	// the whole test signal
#define M17_BENCH_DEC_FRAMES	64u	// the first frames' bits, kept for the decoder (the memory fits P25's idle block)
#define M17_BENCH_FRAME_US	20000u	// a frame's time: the share of the CPU is the time per frame over this
// FNV-1a of the bits of all 300 frames: codec2-fixed-point's reference (tests/test_api.c EXP_BITS)
#define M17_BENCH_ENC_HASH	0x9959853fu
// FNV-1a of the samples decoded from the first 64 frames' bits (tests/host/m17VoiceTest.c checks it on the host)
#define M17_BENCH_DEC_HASH	0x22ef3d78u

// The encoder and the decoder share their memory, as on a call (radio_m17_voice_mem lends it on the radio)
typedef struct {
	union {
		c2fxp_encoder_t enc;
		c2fxp_decoder_t dec;
	} c;
	uint8_t bits[M17_BENCH_DEC_FRAMES][C2FXP_BYTES_PER_FRAME];
	int16_t pcm[C2FXP_SAMPLES_PER_FRAME];
} m17_bench_mem_t;

typedef struct {
	uint16_t enc_frames, dec_frames;	// frames run so far
	uint32_t enc_us, dec_us;		// their total time
	uint32_t enc_max_us, dec_max_us;	// the slowest frame
	uint32_t enc_hash, dec_hash;		// FNV-1a of the bits and of the samples
} m17_bench_result_t;

typedef struct {
	uint32_t (*micros)(void);	// a microsecond clock (hal_micros)
	void (*pause)(void);		// between frames, outside the timing: lets other tasks run. NULL: none
} m17_bench_io_t;

// Runs the whole benchmark into *r (cleared first; enc_frames and dec_frames count up as it goes, for a reader in
// another task)
void m17_bench_run(m17_bench_mem_t *m, const m17_bench_io_t *io, m17_bench_result_t *r);
// Both hashes as expected: the codec computed what it should
static inline bool m17_bench_ok(const m17_bench_result_t *r)
{
	return r->enc_frames == M17_BENCH_ENC_FRAMES && r->dec_frames == M17_BENCH_DEC_FRAMES &&
	       r->enc_hash == M17_BENCH_ENC_HASH && r->dec_hash == M17_BENCH_DEC_HASH;
}

#endif
