// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// M17 voice on a channel (docs/spec/m17.md 6): the call layer (m17_call.h) on an air interface session
// (core/air/air_session.h), through the shared voice engine (air_voice.h). The radio core begins it on an M17 channel
// and ends it on leaving; in between it receives calls and hands the voice of those that pass the CAN squelch to a
// sink. Receive only so far (ADR 006 step 2): transmit comes with step 4. No RTOS call: the radio core's task and the
// provider's task use it as air_session.h describes.
#ifndef RFOUNDRY_M17_ENGINE_H
#define RFOUNDRY_M17_ENGINE_H

#include "m17_call.h"
#include "../air/air_voice.h"

// Why the call in hand is not played (m17_engine_view_t.deny)
enum {
	M17_DENY_NONE,
	M17_DENY_CAN,		// another CAN, and the channel hears only its own
	M17_DENY_ENC,		// encrypted
	M17_DENY_1600,		// voice and data (Codec 2 1600): not decoded
	M17_DENY_DATA,		// a stream without voice
};

typedef struct {
	// Receive, from the provider's task. start: a call we play began (its fields may still change: update). voice:
	// a stream frame's two Codec 2 3200 frames; first: the call's first frame played (the voice path drops what it
	// still held of the call before)
	void (*rx_start)(const m17_call_t *c, void *ctx);
	void (*rx_update)(const m17_call_t *c, void *ctx);
	void (*rx_voice)(const uint8_t payload[M17_STREAM_BYTES], bool first, void *ctx);
	void (*rx_end)(void *ctx);
} m17_engine_hooks_t;

typedef struct {
	uint8_t can;		// 0-15: the channel's CAN
	bool hear_any;		// play every CAN, else only can
} m17_engine_cfg_t;

typedef struct {
	bool rx_active;		// a call is being received (whether or not it is played)
	bool rx_played;
	uint8_t deny;		// M17_DENY_xxx of the call in hand, or the last one
	m17_call_t rx;		// its fields; kept after it ends (the last call heard)
	uint8_t tx;		// air_tx_state_t
	uint32_t rx_calls, tx_calls;
} m17_engine_view_t;

typedef struct {
	air_voice_t av;			// the session, the call in hand, the view (air_voice.h)
	m17_call_rx_t rx;
	m17_engine_cfg_t cfg;
	m17_engine_hooks_t hooks;
	void *ctx;
	bool first;			// the next voice frame played is the call's first
	volatile uint8_t deny;
	m17_call_t heard;		// the call heard as published with av's view
	uint8_t frame[M17_FRAME_BYTES];	// the framer's frame buffer ...
	int8_t soft[M17_FRAME_SYMS];	// ... and its soft values, for the Viterbi decoder
} m17_engine_t;

// Whether a call passes the squelch, and why not (M17_DENY_xxx)
uint8_t m17_engine_deny(const m17_engine_cfg_t *cfg, const m17_call_t *c);
// What the board's provider gives M17: AIR_FIT_xxx, 0 = this board can't
uint32_t m17_engine_fit(const hal_air_t *air);
// Start on the tuned channel (the board in its digital mode, its DMR stack stopped). False: no provider, no fit
bool m17_engine_begin(m17_engine_t *e, const hal_air_t *air, const m17_engine_cfg_t *cfg,
		      const m17_engine_hooks_t *hooks, void *ctx);
void m17_engine_end(m17_engine_t *e);
// The receiver is on a new frequency: what it held is dropped
void m17_engine_retuned(m17_engine_t *e);
// Change what is played while running (a CAN change)
void m17_engine_set(m17_engine_t *e, const m17_engine_cfg_t *cfg);
air_tx_state_t m17_engine_tx_state(const m17_engine_t *e);
void m17_engine_view(const m17_engine_t *e, m17_engine_view_t *out);

#endif
