// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// M17 voice (m17_voice.h)
#include <string.h>

#include "m17_voice.h"

_Static_assert((M17_VOICE_QUEUE & (M17_VOICE_QUEUE - 1)) == 0, "the receive queue is a power of two");
_Static_assert((M17_VOICE_TX_QUEUE & (M17_VOICE_TX_QUEUE - 1)) == 0, "the transmit queue is a power of two");

// Each counter has one writer; the other side reads it with acquire, after the writer's release
#define LOAD(x)		__atomic_load_n(&(x), __ATOMIC_ACQUIRE)
#define STORE(x, v)	__atomic_store_n(&(x), (v), __ATOMIC_RELEASE)

enum { CODEC_NONE, CODEC_DEC, CODEC_ENC };

static m17_voice_mem_t *mem;
static m17_voice_stats_t stats;
// Provider's side
static uint32_t q_in, tq_out;
static uint8_t rx_call, rx_ended;	// the call frames come in for; the call that ended (rx_call + 1 while none)
// Callback's side
static uint32_t q_out, tq_in;
static uint8_t codec, play_call;
static bool playing, repeat_ok;
// Radio task's
static bool tx_on;

void m17_voice_begin(m17_voice_mem_t *m)
{
	q_in = q_out = tq_in = tq_out = 0;
	rx_call = 0;
	rx_ended = 1;
	codec = CODEC_NONE;
	playing = repeat_ok = tx_on = false;
	STORE(mem, m);
}

void m17_voice_end(void)
{
	STORE(mem, NULL);
}

void m17_voice_stats(m17_voice_stats_t *out)
{
	*out = stats;
}

// ── Receive ──
void m17_voice_rx(const uint8_t payload[M17_STREAM_BYTES], bool new_call)
{
	m17_voice_mem_t *m = LOAD(mem);

	if (m == NULL)
		return;
	if (new_call) {
		rx_call++;
		STORE(rx_ended, (uint8_t)(rx_call + 1u));
	}
	for (int i = 0; i < 2; i++) {
		uint32_t in = q_in;
		stats.rx_in++;
		if (in - LOAD(q_out) >= M17_VOICE_QUEUE) {
			stats.rx_overflows++;
			continue;
		}
		m17_voice_entry_t *e = &m->q[in & (M17_VOICE_QUEUE - 1)];
		e->call = rx_call;
		memcpy(e->bits, &payload[i * M17_VOICE_BYTES], M17_VOICE_BYTES);
		STORE(q_in, in + 1u);
	}
}

void m17_voice_rx_end(void)
{
	STORE(rx_ended, rx_call);
}

static void decode(m17_voice_mem_t *m, const uint8_t *bits, int16_t *spk)
{
	if (codec != CODEC_DEC) {
		c2fxp_decoder_init(&m->c.dec);
		codec = CODEC_DEC;
	}
	c2fxp_decode(&m->c.dec, spk, bits);
}

static void rx_block(m17_voice_mem_t *m, int16_t *spk)
{
	uint32_t in = LOAD(q_in);
	uint8_t ended = LOAD(rx_ended);

	if (q_out != in) {
		// Another call's frames came in: what is left of the call before is dropped, and the new one plays once
		// primed, or at once if it already ended
		uint8_t newest = m->q[(in - 1u) & (M17_VOICE_QUEUE - 1)].call;
		if (playing && newest != play_call)
			playing = false;
		if (!playing) {
			while (m->q[q_out & (M17_VOICE_QUEUE - 1)].call != newest)
				STORE(q_out, q_out + 1u);
			if (in - q_out >= M17_VOICE_PRIME || ended == newest) {
				playing = true;
				repeat_ok = false;
				play_call = newest;
				codec = CODEC_NONE;	// a fresh decoder for each call
				stats.rx_calls++;
			}
		}
	}
	if (playing) {
		if (q_out != in) {
			memcpy(m->last, m->q[q_out & (M17_VOICE_QUEUE - 1)].bits, M17_VOICE_BYTES);
			STORE(q_out, q_out + 1u);
			decode(m, m->last, spk);
			repeat_ok = true;
			stats.rx_frames++;
			return;
		}
		if (ended == play_call) {
			playing = false;	// played out
		} else {
			stats.rx_underruns++;
			stats.rx_concealed++;
			if (repeat_ok) {
				repeat_ok = false;
				decode(m, m->last, spk);
				return;
			}
		}
	}
	memset(spk, 0, M17_VOICE_SAMPLES * sizeof spk[0]);
}

// ── Transmit ──
void m17_voice_tx(bool on)
{
	STORE(tx_on, on);
}

bool m17_voice_tx_take(uint8_t payload[M17_STREAM_BYTES])
{
	m17_voice_mem_t *m = LOAD(mem);
	uint32_t out = tq_out;

	if (m == NULL || LOAD(tq_in) - out < 2u)
		return false;
	for (int i = 0; i < 2; i++)
		memcpy(&payload[i * M17_VOICE_BYTES], m->tq[(out + (uint32_t)i) & (M17_VOICE_TX_QUEUE - 1)], M17_VOICE_BYTES);
	STORE(tq_out, out + 2u);
	stats.tx_frames += 2;
	return true;
}

static void tx_block(m17_voice_mem_t *m, const int16_t *mic)
{
	static const int16_t silence[M17_VOICE_SAMPLES];
	uint8_t bits[M17_VOICE_BYTES];

	if (codec != CODEC_ENC) {
		c2fxp_encoder_init(&m->c.enc);
		codec = CODEC_ENC;
		playing = false;
	}
	c2fxp_encode(&m->c.enc, bits, mic ? mic : silence);
	stats.tx_encoded++;
	uint32_t in = tq_in;
	if (in - LOAD(tq_out) >= M17_VOICE_TX_QUEUE) {
		stats.tx_dropped++;
		return;
	}
	memcpy(m->tq[in & (M17_VOICE_TX_QUEUE - 1)], bits, M17_VOICE_BYTES);
	STORE(tq_in, in + 1u);
}

void m17_voice_block(const int16_t *mic, int16_t *spk)
{
	m17_voice_mem_t *m = LOAD(mem);

	if (m == NULL) {
		if (spk)
			memset(spk, 0, M17_VOICE_SAMPLES * sizeof spk[0]);
		return;
	}
	if (LOAD(tx_on)) {
		tx_block(m, mic);
		if (spk)
			memset(spk, 0, M17_VOICE_SAMPLES * sizeof spk[0]);
	} else if (spk) {
		rx_block(m, spk);
	}
}
