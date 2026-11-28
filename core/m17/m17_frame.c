// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// M17 frames (m17.h; docs/spec/m17.md 2): the CRC, the bit pipeline from type 1 bits to a frame and back, the LSF,
// the LICH and the stream frame
#include <string.h>

#include "m17.h"
#include "air/air_syms.h"
#include "fec/conv.h"
#include "fec/golay.h"
#include "hal_air.h"

// G1 = 1 + D^3 + D^4, G2 = 1 + D + D^2 + D^4 (spec, appendix C: Convolutional Encoder)
static const fec_conv_code_t CODE = { .g1 = 0x19, .g2 = 0x17 };

// The randomizer's 368 bits (spec, appendix B: Randomizer Sequence): XORed onto the type 4 bits, MSB first, the same
// for every frame
static const uint8_t RAND[46] = {
	0xD6, 0xB5, 0xE2, 0x30, 0x82, 0xFF, 0x84, 0x62, 0xBA, 0x4E, 0x96, 0x90, 0xD8, 0x98, 0xDD, 0x5D,
	0x0C, 0xC8, 0x52, 0x43, 0x91, 0x1D, 0xF8, 0x6E, 0x68, 0x2F, 0x35, 0xDA, 0x14, 0xEA, 0xCD, 0x76,
	0x19, 0x8D, 0xD5, 0x80, 0xD1, 0x33, 0x87, 0x13, 0x57, 0x18, 0x2D, 0x29, 0x78, 0xC3,
};

// Puncture patterns (spec, appendix E: Code Puncturing), repeating: whether type 2 bit i is kept. P1 the LSF's (488
// to 368 bits), P2 the stream payload's (296 to 272)
#define P1_LEN	61
#define P2_LEN	12
static uint8_t p1_keep(uint16_t i)
{
	// a leading 1, then 1,0,1,1 fifteen times
	i = (uint16_t)(i % P1_LEN);
	return i == 0 || ((i - 1u) & 3u) != 1u;
}
static uint8_t p2_keep(uint16_t i) { return i % P2_LEN != P2_LEN - 1u; }

#define LSF_T1_BITS	(M17_LSF_BYTES * 8 + 4)	// 244 with the tail
#define LSF_T2_BITS	(2 * LSF_T1_BITS)	// 488
#define STR_T1_BITS	(16 + M17_STREAM_BYTES * 8 + 4)	// 148: FN, payload, tail
#define STR_T2_BITS	(2 * STR_T1_BITS)	// 296
#define LICH_BITS	96

_Static_assert(sizeof(((m17_fec_work_t *)0)->soft) == LSF_T2_BITS, "the LSF's coded bits");
_Static_assert(sizeof(((m17_fec_work_t *)0)->dec) / sizeof(uint16_t) == LSF_T1_BITS, "the LSF's input bits");

static uint8_t bit_get(const uint8_t *p, uint16_t i)
{
	return (uint8_t)((p[i >> 3] >> (7 - (i & 7))) & 1);
}

static void bit_put(uint8_t *p, uint16_t i, uint8_t b)
{
	uint8_t m = (uint8_t)(0x80u >> (i & 7));

	p[i >> 3] = (uint8_t)(b ? (p[i >> 3] | m) : (p[i >> 3] & ~m));
}

// The interleaver (spec, appendix F: Interleaving): type 4 bit k is type 3 bit pi(k) = (45k + 92k^2) mod 368. pi is its
// own inverse
static uint16_t qpp(uint16_t k)
{
	return (uint16_t)((45u * k + 92u * (uint32_t)k * k) % M17_PAYLOAD_BITS);
}

// ── CRC ──
uint16_t m17_crc(const uint8_t *p, size_t n)
{
	uint16_t crc = 0xFFFF;

	while (n-- > 0) {
		crc ^= (uint16_t)(*p++ << 8);
		for (int b = 0; b < 8; b++) {
			uint16_t sh = (uint16_t)(crc << 1);
			crc = (crc & 0x8000u) ? (uint16_t)(sh ^ 0x5935u) : sh;
		}
	}
	return crc;
}

void m17_lsf_crc_set(uint8_t lsf[M17_LSF_BYTES])
{
	uint16_t c = m17_crc(lsf, M17_LSF_CRC);

	lsf[M17_LSF_CRC] = (uint8_t)(c >> 8);
	lsf[M17_LSF_CRC + 1] = (uint8_t)c;
}

bool m17_lsf_crc_ok(const uint8_t lsf[M17_LSF_BYTES])
{
	return m17_crc(lsf, M17_LSF_BYTES) == 0;
}

// ── Sending: type 3 bits to the frame ──
// t3: the 368 type 3 bits. Interleaves, randomizes and writes them behind the sync
static void frame_put(uint8_t frame[M17_FRAME_BYTES], uint16_t sync, const uint8_t t3[M17_PAYLOAD_BITS / 8])
{
	frame[0] = (uint8_t)(sync >> 8);
	frame[1] = (uint8_t)sync;
	for (uint16_t k = 0; k < M17_PAYLOAD_BITS; k++)
		bit_put(frame, (uint16_t)(16 + k), bit_get(t3, qpp(k)) ^ bit_get(RAND, k));
}

// Coded bits t2[0..n2) through a puncture pattern into t3 from bit at; returns the next free bit
static uint16_t puncture(const uint8_t *t2, uint16_t n2, uint8_t (*keep)(uint16_t), uint8_t *t3, uint16_t at)
{
	for (uint16_t i = 0; i < n2; i++)
		if (keep(i))
			bit_put(t3, at++, bit_get(t2, i));
	return at;
}

void m17_lsf_put(uint8_t frame[M17_FRAME_BYTES], const uint8_t lsf[M17_LSF_BYTES])
{
	uint8_t t1[(LSF_T1_BITS + 7) / 8] = { 0 }, t2[LSF_T2_BITS / 8], t3[M17_PAYLOAD_BITS / 8];

	memcpy(t1, lsf, M17_LSF_BYTES);	// the tail bits stay 0
	fec_conv_encode(&CODE, t1, LSF_T1_BITS, t2);
	puncture(t2, LSF_T2_BITS, p1_keep, t3, 0);
	frame_put(frame, M17_SYNC_LSF, t3);
}

void m17_stream_put(uint8_t frame[M17_FRAME_BYTES], const uint8_t lich[M17_LICH_BYTES], uint16_t fn,
		    const uint8_t payload[M17_STREAM_BYTES])
{
	uint8_t t1[(STR_T1_BITS + 7) / 8] = { 0 }, t2[STR_T2_BITS / 8], t3[M17_PAYLOAD_BITS / 8];

	// The LICH: 12 bits in each Golay word, data in its top 12 bits
	for (int w = 0; w < 4; w++) {
		const uint8_t *p = &lich[(w / 2) * 3];
		uint16_t d = (w & 1) ? (uint16_t)((p[1] & 0x0Fu) << 8 | p[2]) : (uint16_t)(p[0] << 4 | p[1] >> 4);
		uint32_t cw = fec_golay24_encode(d);
		t3[w * 3] = (uint8_t)(cw >> 16);
		t3[w * 3 + 1] = (uint8_t)(cw >> 8);
		t3[w * 3 + 2] = (uint8_t)cw;
	}
	t1[0] = (uint8_t)(fn >> 8);
	t1[1] = (uint8_t)fn;
	memcpy(&t1[2], payload, M17_STREAM_BYTES);
	fec_conv_encode(&CODE, t1, STR_T1_BITS, t2);
	puncture(t2, STR_T2_BITS, p2_keep, t3, LICH_BITS);
	frame_put(frame, M17_SYNC_STREAM, t3);
}

// ── Receiving ──
// Payload bit k (type 4, randomized as on air) as a soft value, conv.h's sign (positive 1), derandomized: the high bit
// of a symbol from its sign, the low bit from its size (outer symbols are 1). Without soft values, every bit the same
// size (hard decisions)
static int8_t t4_soft(const uint8_t *hard, const int8_t *soft, uint16_t k)
{
	uint16_t j = (uint16_t)(16 + k);
	int v;

	if (!soft) {
		v = (air_sym_get(hard, (uint16_t)(j >> 1)) >> ((j & 1) ? 0 : 1)) & 1 ? 32 : -32;
	} else {
		v = soft[j >> 1];
		v = (j & 1) ? (v < 0 ? -v : v) - 2 * AIR_SOFT_OUTER / 3 : -v;
	}
	if (bit_get(RAND, k))
		v = -v;
	return (int8_t)(v > 127 ? 127 : v < -127 ? -127 : v);
}

// Type 3 bit i as a soft value: the type 4 bit it was interleaved to (pi is its own inverse)
static int8_t t3_soft(const uint8_t *hard, const int8_t *soft, uint16_t i)
{
	return t4_soft(hard, soft, qpp(i));
}

// The type 2 soft values of n2 coded bits from type 3 bit at on, 0 where the pattern punctured one
static void depuncture(const uint8_t *hard, const int8_t *soft, uint16_t at, uint16_t n2, uint8_t (*keep)(uint16_t),
		       int8_t *out)
{
	for (uint16_t i = 0; i < n2; i++)
		out[i] = keep(i) ? t3_soft(hard, soft, at++) : 0;
}

int m17_lsf_get(const uint8_t *hard, const int8_t *soft, m17_fec_work_t *w, uint8_t lsf[M17_LSF_BYTES])
{
	uint8_t t1[(LSF_T1_BITS + 7) / 8];

	depuncture(hard, soft, 0, LSF_T2_BITS, p1_keep, w->soft);
	int fixed = fec_conv_decode(&CODE, w->soft, LSF_T1_BITS, w->dec, t1);
	memcpy(lsf, t1, M17_LSF_BYTES);
	return m17_lsf_crc_ok(lsf) ? fixed : -1;
}

int m17_lich_get(const uint8_t *hard, const int8_t *soft, uint8_t lich[M17_LICH_BYTES])
{
	int fixed = 0;

	for (int w = 0; w < 4; w++) {
		uint32_t cw = 0;
		uint16_t d;
		for (uint16_t i = 0; i < 24; i++)
			cw = cw << 1 | (t3_soft(hard, soft, (uint16_t)(w * 24 + i)) > 0);
		int e = fec_golay24_decode(cw, &d);
		if (e < 0)
			return -1;
		fixed += e;
		uint8_t *p = &lich[(w / 2) * 3];
		if (w & 1) {
			p[1] = (uint8_t)((p[1] & 0xF0u) | (d >> 8));
			p[2] = (uint8_t)d;
		} else {
			p[0] = (uint8_t)(d >> 4);
			p[1] = (uint8_t)((d & 0x0Fu) << 4);
		}
	}
	return fixed;
}

int m17_stream_get(const uint8_t *hard, const int8_t *soft, m17_fec_work_t *w, uint16_t *fn,
		   uint8_t payload[M17_STREAM_BYTES])
{
	uint8_t t1[(STR_T1_BITS + 7) / 8];

	depuncture(hard, soft, LICH_BITS, STR_T2_BITS, p2_keep, w->soft);
	int fixed = fec_conv_decode(&CODE, w->soft, STR_T1_BITS, w->dec, t1);
	*fn = (uint16_t)(t1[0] << 8 | t1[1]);
	memcpy(payload, &t1[2], M17_STREAM_BYTES);
	return fixed;
}

// ── The LSF in LICH chunks ──
void m17_lich_make(const uint8_t lsf[M17_LSF_BYTES], uint8_t n, uint8_t lich[M17_LICH_BYTES])
{
	memcpy(lich, &lsf[n * 5u], 5);
	lich[5] = (uint8_t)(n << 5);
}

bool m17_lsf_asm_add(m17_lsf_asm_t *a, const uint8_t lich[M17_LICH_BYTES])
{
	uint8_t n = m17_lich_count(lich);

	if (n >= M17_LICH_CHUNKS)
		return false;
	uint8_t *at = &a->lsf[n * 5u];
	if ((a->have & (1u << n)) && memcmp(at, lich, 5) != 0)
		a->have = 0;	// another stream's LSF
	memcpy(at, lich, 5);
	a->have |= (uint8_t)(1u << n);
	return a->have == (1u << M17_LICH_CHUNKS) - 1u && m17_lsf_crc_ok(a->lsf);
}
