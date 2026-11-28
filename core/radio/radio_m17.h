// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// M17 on a channel, the radio core's side (docs/spec/m17.md 6; docs/crucible/adr/006-m17.md): the engine
// (core/m17/m17_engine.h) on the memory it runs in, its Codec 2 voice (core/m17/m17_voice.h) on the PCM stream, the
// channel's M17 parameters and a compact view for the radio view. radio_sm.c decides when M17 runs. Receive only so
// far (ADR 006 step 2): PTT is refused (radio_air_ops_t.no_tx).
//
// The parameters of a stored M17 channel are its own (docs/spec/m17-codeplug.md); the VFO and a channel whose mode was
// changed to M17 take the RAM settings (radio_m17_set_settings), which the debug link changes.
#ifndef RFOUNDRY_RADIO_M17_H
#define RFOUNDRY_RADIO_M17_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "codeplug/codeplug.h"
#include "radio/radio_air.h"

#define RADIO_M17_DEST_LEN	9	// a destination's characters; "" = BROADCAST
#define RADIO_M17_CALL_TEXT	10	// a decoded address and its NUL (m17.h M17_CALL_TEXT)
#define RADIO_M17_TEXT_MAX	52	// META text (m17_call.h M17_TEXT_MAX)

// The M17 settings (RAM): the VFO's, and the shape of what is in force
typedef struct {
	uint8_t can;				// 0-15
	bool hear_any;				// play every CAN, else only can
	char dest[RADIO_M17_DEST_LEN + 1];	// "" = BROADCAST
} radio_m17_settings_t;

// view flags
#define RADIO_M17_ON		0x01u	// the engine runs on this channel
#define RADIO_M17_RX		0x02u	// a call is being received
#define RADIO_M17_PLAYED	0x04u	// ... and passes the squelch (its voice plays)
#define RADIO_M17_LATE		0x08u	// ... was entered from its LICH, not its LSF
#define RADIO_M17_ANY		0x10u	// the settings in force hear every CAN
#define RADIO_M17_NO_CALL	0x20u	// we have no callsign

// The radio view's part (radio_view_t.m17): the call heard now, or the last one
typedef struct {
	uint8_t flags;		// RADIO_M17_*
	uint8_t tx;		// air_tx_state_t of the engine
	uint8_t deny;		// M17_DENY_* of the call heard (m17_engine.h)
	uint8_t can;		// the CAN of the call heard
	uint8_t run_can;	// the settings in force
	uint16_t fit;		// what the board's provider gives M17 (AIR_FIT_*), 0: nothing
	uint16_t type;		// the call's TYPE field
	char src[RADIO_M17_CALL_TEXT];	// the caller, "" not known
	char dst[RADIO_M17_CALL_TEXT];	// whom it calls ("@ALL" for BROADCAST)
	char run_dst[RADIO_M17_DEST_LEN + 1];
	char text[RADIO_M17_TEXT_MAX + 1];	// META text as far as it came
} radio_m17_view_t;

// Counters for the debug link (any task reads them; the radio task writes them while M17 runs)
typedef struct {
	radio_air_counts_t air;		// rx_calls, tx_calls, tx_filler; the session's rx_chunks, tx_underruns, tx_syms
	uint32_t lsf_ok, lsf_fail, lich_ok, lich_fail, frames, fn_gaps, fec_bits, auth_frames, eot, packets, held;
	uint32_t c2_frames, c2_concealed, c2_underruns, c2_overflows, tx_encoded;	// the voice path's
	uint32_t begins, begin_fails;	// engine starts, and the ones refused (no provider, no fit, no memory)
} radio_m17_stats_t;

#if CONFIG_M17
#include "hal_audio.h"

extern const radio_air_ops_t radio_m17_air;

// The block M17's voice runs in (ADR 006 4), 8-aligned: what the board's vocoder lends where it lends (the DMR side's
// codec is stopped), else P25's memory while P25 idles, else a block of its own. Free only while no other air mode
// runs: the NXDN and YSF engines use P25's, DMR voice the vocoder's. *len: its size; NULL: none now
void *radio_m17_voice_mem(size_t *len);
// The block the M17 engine runs in, beside the voice's (radio_m17_mem.c). *len: its size; NULL: none
void *radio_m17_engine_mem(size_t *len);

// Start-up: the PCM stream the voice plays on, the settings at their defaults
void radio_m17_init(const hal_audio_t *audio);
// Beeps mixed into the stream while it runs (beep_mix): true when it added sound to the block. Audio task
void radio_m17_set_mixer(bool (*mix)(int16_t *spk));
// What the board's provider gives M17 (AIR_FIT_*; any task), and whether that is anything
uint32_t radio_m17_fit(void);
bool radio_m17_fits(void);
// A channel read from the codeplug, as the radio runs it: a live M17 channel (kind 7) becomes RADIO_MODE_M17 where the
// board carries M17 and its frequency allows a digital mode. False: left as read (parked)
bool radio_m17_take(channel_t *ch);
// The board's provider receives M17: what scan and dual watch visit
bool radio_m17_receives(void);
void radio_m17_settings(radio_m17_settings_t *out);		// any task
void radio_m17_set_settings(const radio_m17_settings_t *s);	// radio task
// The parameters in force: false when the engine isn't running (diagnostics only)
bool radio_m17_in_force(radio_m17_settings_t *out);

// Radio task. begin: the board tuned in its digital mode, its DMR stack stopped. number: the stored channel ch is (0:
// the VFO, or none): a live M17 channel's tag gives the parameters, else the settings. radio_name: for the callsign
// when the codeplug has none. False: no memory, no provider or no fit (M17 then receives nothing)
bool radio_m17_begin(const channel_t *ch, uint16_t number, const char *radio_name);
void radio_m17_end(void);
bool radio_m17_running(void);
void radio_m17_retuned(void);
// The settings changed: the engine takes them
void radio_m17_apply(const channel_t *ch, uint16_t number, const char *radio_name);
// Every tick: the voice stream, the view and the counters. True when the view changed
bool radio_m17_poll(radio_m17_view_t *v);

void radio_m17_stats(radio_m17_stats_t *out);
void radio_m17_reset_stats(void);
#endif

#endif
