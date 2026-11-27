// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Encoder input leveling, independent of the board and codec. One call per PCM block.
#ifndef RFOUNDRY_TX_LEVEL_H
#define RFOUNDRY_TX_LEVEL_H
#include <stdint.h>
#include <stdbool.h>

#define TX_LEVEL_UNITY		65536u	// Q16
#define TX_LEVEL_CEILING	28000	// about -1.4 dBFS
#define TX_LEVEL_MIN_GAIN	4135u	// -24 dB, Q16
#define TX_LEVEL_MAX_GAIN	1038676u	// +24 dB, Q16
#define TX_LEVEL_RMS_FLOOR	104u	// about -50 dBFS, relative to 32768
#define TX_LEVEL_RELEASE_Q8	271u	// 271/256 is +0.49 dB per block (20 ms)

typedef struct { uint32_t gain_q16; uint32_t limited_blocks; } tx_level_t;
typedef struct { uint32_t power; uint16_t peak; } tx_level_meter_t;

static inline uint32_t tx_level_sqrt(uint32_t v)
{
	uint32_t out = 0, bit = 1u << 30;
	while (bit > v) bit >>= 2;
	while (bit != 0) {
		if (v >= out + bit) { v -= out + bit; out = (out >> 1) + bit; }
		else out >>= 1;
		bit >>= 2;
	}
	return out;
}

// n is 1..1023 samples; radio audio blocks contain 160.
static inline tx_level_meter_t tx_level_measure(const int16_t *pcm, unsigned n, bool power)
{
	tx_level_meter_t m = { 0, 0 };
	uint32_t high = 0, low = 0;
	// Split the sum so even 1023 full-scale samples fit two 32-bit accumulators without losing low-level power.
	for (unsigned i = 0; i < n; i++) {
		int32_t v = pcm[i];
		uint32_t a = (uint32_t)(v < 0 ? -v : v);
		if (power) {
			uint32_t square = (uint32_t)(v * v);
			high += square >> 8;
			low += square & 255u;
		}
		if (a > m.peak) m.peak = (uint16_t)a;
	}
	m.power = (high / n) * 256u + ((high % n) * 256u + low) / n;
	return m;
}

// RMS dBFS in tenths, referenced to 32768. Zero power is displayed as below -96 dBFS.
static inline int16_t tx_level_db10(uint32_t power)
{
	static const uint16_t lg[17] = { 0, 90, 174, 254, 330, 402, 470, 536, 599, 659, 717, 773, 827, 879, 929, 977, 1024 };
	if (power == 0) return -960;
	unsigned e = 31u - (unsigned)__builtin_clz(power);
	uint32_t x = e >= 8 ? power >> (e - 8) : power << (8 - e);
	unsigned i = (x - 256u) >> 4, frac = x & 15u;
	int32_t logq10 = (int32_t)e * 1024 + lg[i] + ((lg[i + 1] - lg[i]) * frac) / 16;
	return (int16_t)(((logq10 - 30 * 1024) * 301) / 10240);
}

static inline void tx_level_reset(tx_level_t *s, uint32_t initial_gain_q16)
{
	s->gain_q16 = initial_gain_q16;
	s->limited_blocks = 0;
}

// Reset to the manual gain or unity (adaptive) at the start of a capture or transmit. target_rms == 0 selects
// manual gain. Input and output may alias. Returns the output peak and, if requested, the raw input measurement.
static inline uint16_t tx_level_process(tx_level_t *s, const int16_t *in, int16_t *out, unsigned n,
					uint32_t manual_gain_q16, uint16_t target_rms, tx_level_meter_t *input)
{
	tx_level_meter_t m = tx_level_measure(in, n, target_rms != 0 || input != NULL);
	if (input != NULL) *input = m;
	uint32_t prev = s->gain_q16, gain = manual_gain_q16;
	if (target_rms != 0) {
		// Hold gain through pauses. The peak bound still applies to an otherwise quiet block.
		gain = prev;
		if (m.power >= TX_LEVEL_RMS_FLOOR * TX_LEVEL_RMS_FLOOR) {
			gain = ((uint32_t)target_rms << 16) / tx_level_sqrt(m.power);
			if (gain < TX_LEVEL_MIN_GAIN) gain = TX_LEVEL_MIN_GAIN;
			if (gain > TX_LEVEL_MAX_GAIN) gain = TX_LEVEL_MAX_GAIN;
		}
	}
	// The block can cut gain immediately. An increase is bounded and ramped through the block to avoid a step.
	uint32_t rise = (prev * (TX_LEVEL_RELEASE_Q8 - 256u)) >> 8;
	uint32_t recovery = prev + (rise != 0 ? rise : 1u);
	if (gain > recovery) gain = recovery;
	if (m.peak != 0) {
		uint32_t allowed = (TX_LEVEL_CEILING * TX_LEVEL_UNITY) / m.peak;
		if (gain > allowed) {
			gain = allowed;
			s->limited_blocks++;
		}
	}
	s->gain_q16 = gain;

	// For a falling gain the whole block uses the cut. For a rising gain its own peak allows both ends of the ramp.
	uint32_t start = prev < gain ? prev : gain;
	uint32_t span = gain - start;
	uint16_t peak = 0;
	for (unsigned i = 0; i < n; i++) {
		uint32_t ramp = start + span * (i + 1u) / n;
		// The peak bound makes the signed product fit int32_t. Division preserves manual gain's truncation.
		int32_t v = (int32_t)in[i] * (int32_t)ramp / (int32_t)TX_LEVEL_UNITY;
		uint32_t a = (uint32_t)(v < 0 ? -v : v);
		if (a > peak) peak = (uint16_t)a;
		out[i] = (int16_t)v;
	}
	return peak;
}
// The TX level settings' choices (Settings > Talk & listen, DMR, P25, NXDN and YSF TX level): 0 Off, the manual gain with the
// peak limiter; 1..10 an RMS target of -30..-12 dBFS in 2 dB steps, relative to 32768
#define TX_LEVEL_CHOICES	11u
#define TX_LEVEL_CHOICE_DBFS(c)	(-32 + 2 * (int)(c))	// c 1..10
static inline uint16_t tx_level_choice_rms(uint8_t choice)
{
	static const uint16_t rms[TX_LEVEL_CHOICES] = { 0, 1036, 1305, 1642, 2068, 2603, 3277, 4125, 5193, 6538, 8231 };
	return choice < TX_LEVEL_CHOICES ? rms[choice] : 0u;
}

#endif
