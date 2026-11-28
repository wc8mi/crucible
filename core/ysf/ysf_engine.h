// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 David Behnke
// YSF DN voice on a channel (docs/spec/ysf.md 5): the call layer (ysf.h) on an air interface session
// (core/air/air_session.h). The radio core begins it on a YSF channel and ends it on leaving; in between it receives
// calls, hands their voice frames to a sink, and transmits when the radio core says so. No RTOS call: the radio
// core's task and the provider's task use it as air_session.h describes.
#ifndef RFOUNDRY_YSF_ENGINE_H
#define RFOUNDRY_YSF_ENGINE_H

#include "ysf.h"
#include "../air/air_voice.h"
#include "../air/air_softmodem.h"

// The voice path a call's frames go to (ysf.md 5.5): the DMR side's (AMBE+2: DN, V/D mode 1) or P25's full-rate
// one (IMBE: VW, CONFIG_YSF_VW). NONE while the radio core switches between them
#define YSF_PATH_NONE		0
#define YSF_PATH_AMBE		1
#define YSF_PATH_IMBE		2

typedef struct {
	// Receive, from the provider's task. start: a call we play began (its fields may still fill in: update). voice:
	// a voice frame of the DMR side's voice path (ysf.h)
	void (*rx_start)(const ysf_call_t *c, void *ctx);
	void (*rx_update)(const ysf_call_t *c, void *ctx);
	void (*rx_voice)(const uint8_t v[YSF_VOICE_BYTES], void *ctx);
	void (*rx_end)(void *ctx);
	// Transmit, from the provider's task: the next voice frame, false when none is ready. NULL: every frame is
	// `silence`
	bool (*tx_voice)(uint8_t v[YSF_VOICE_BYTES], void *ctx);
	// A room reply heard, from the provider's task (ysf.md 9.3)
	void (*rx_wx)(const ysf_wx_t *w, void *ctx);
	// The call's voice on a path opens before its first frame there (open), and closes at the call's end if the path
	// is still the one opened (a switch closes it itself). From the provider's task. NULL: nothing to open
	void (*rx_path)(uint8_t path, bool open, void *ctx);
	// A VW voice frame for the IMBE path (CONFIG_YSF_VW); errs packed as fec_imbe88_decode. From the provider's task
	void (*rx_vw)(const uint8_t imbe[YSF_VW_BYTES], uint8_t errs, void *ctx);
} ysf_engine_hooks_t;

typedef struct {
	uint8_t rx_dgid;	// YSF_DGID_ALL: every call; else only calls with this DG-ID
	ysf_tx_id_t tx;		// what we send: callsign, radio ID, DG-ID, route
	bool tx_inhibit;	// no callsign: PTT refused
	const uint8_t *silence;	// a voice frame of silence (YSF_VOICE_BYTES), from the vocoder
} ysf_engine_cfg_t;

typedef struct {
	bool rx_active;		// a call is being received (whether or not it is played)
	bool rx_played;
	ysf_call_t rx;		// its fields; kept after it ends (the last call heard)
	uint8_t tx;		// air_tx_state_t
	uint32_t rx_calls, tx_calls;
	uint32_t tx_filler;	// silence frames sent because the encoder had none ready
	uint32_t path_drops;	// VCH dropped while the voice path was not the one their data type needs
} ysf_engine_view_t;

typedef struct {
	air_voice_t av;			// the session, the call in hand, the view, transmit (air_voice.h)
	ysf_call_rx_t rx;
	ysf_call_tx_t tx;
	ysf_engine_cfg_t cfg;
	ysf_engine_hooks_t hooks;
	void *ctx;
	ysf_call_t heard;		// the call heard as published with av's view
	// The voice path (ysf_engine_path_*): path the radio core's task writes, the frames' one now; want the provider's,
	// what the call heard needs (AMBE between calls); gate odd while the provider is inside a voice hook, so the radio
	// core knows when the old path is no longer in use; opened the path whose voice the call opened
	volatile uint8_t path, want, gate;
	uint8_t opened;
	uint32_t path_drops;
	uint8_t frame[YSF_FRAME_BYTES];		// the framer's frame buffer ...
	int8_t soft[YSF_FRAME_SYMS];		// ... and its soft values, for the decoders
#if CONFIG_AIR_RX_ADAPT
	air_softmodem_adapt_t adapt;		// lent to a soft modem receiving the session (ysf.md 5.4)
#endif
} ysf_engine_t;

// What the board's provider gives YSF: AIR_FIT_xxx, 0 = this board can't
uint32_t ysf_engine_fit(const hal_air_t *air);
// Start on the tuned channel (the board in its digital mode, its DMR stack stopped). False: no provider, no fit
bool ysf_engine_begin(ysf_engine_t *e, const hal_air_t *air, const ysf_engine_cfg_t *cfg,
		      const ysf_engine_hooks_t *hooks, void *ctx);
void ysf_engine_end(ysf_engine_t *e);
void ysf_engine_retuned(ysf_engine_t *e);
// Change what is received and sent while running. Not while transmitting
void ysf_engine_set(ysf_engine_t *e, const ysf_engine_cfg_t *cfg);

// Transmit (the radio core's task), as nxdn_engine.h: prepare, hal_radio_t.tx(), go; stop when the operator lets
// go; AIR_TX_DONE = switch the transmitter off, then clear
bool ysf_engine_tx_prepare(ysf_engine_t *e);
// The same for a data transmission (a room command, ysf.md 9.1): the engine ends it by itself, no stop needed
bool ysf_engine_tx_prepare_data(ysf_engine_t *e, const uint8_t *data, uint8_t len);
bool ysf_engine_tx_go(ysf_engine_t *e);
void ysf_engine_tx_stop(ysf_engine_t *e);
void ysf_engine_tx_abort(ysf_engine_t *e);
air_tx_state_t ysf_engine_tx_state(const ysf_engine_t *e);
void ysf_engine_tx_clear(ysf_engine_t *e);

// A copy of the state for the radio core's task
void ysf_engine_view(const ysf_engine_t *e, ysf_engine_view_t *out);

// The voice path, the radio core's task (ysf.md 5.5). want: the path the call heard needs. A switch: close (frames drop
// from then on, the token to give quiet), quiet until true (no voice hook still runs on the old path), then the radio
// core stops the old path, starts the new one and opens it
uint8_t ysf_engine_path_want(const ysf_engine_t *e);
uint8_t ysf_engine_path_close(ysf_engine_t *e);
bool ysf_engine_path_quiet(const ysf_engine_t *e, uint8_t token);
void ysf_engine_path_open(ysf_engine_t *e, uint8_t path);

#endif
