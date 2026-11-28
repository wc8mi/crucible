// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// M17 voice on a channel (m17_engine.h), on the shared voice engine (air_voice.h). The shape is nxdn_engine.c's
#include "m17_engine.h"

#include <stddef.h>
#include <string.h>

uint32_t m17_engine_fit(const hal_air_t *air)
{
	return air_session_fit(air, &m17_air_mode);
}

// ── Receive (provider's task) ──
uint8_t m17_engine_deny(const m17_engine_cfg_t *cfg, const m17_call_t *c)
{
	if (m17_type_enc(c->type) != 0)
		return M17_DENY_ENC;
	if (m17_type_data(c->type) == M17_TYPE_DATA_VD)
		return M17_DENY_1600;
	if (m17_type_data(c->type) != M17_TYPE_DATA_VOICE)
		return M17_DENY_DATA;
	if (!cfg->hear_any && c->can != cfg->can)
		return M17_DENY_CAN;
	return M17_DENY_NONE;
}

static void notify(m17_engine_t *e, const m17_call_t *c, air_voice_event_t ev)
{
	if (ev == AIR_VOICE_START) {
		e->first = true;
		if (e->hooks.rx_start)
			e->hooks.rx_start(c, e->ctx);
	} else if (ev == AIR_VOICE_UPDATE && e->hooks.rx_update) {
		e->hooks.rx_update(c, e->ctx);
	} else if (ev == AIR_VOICE_END && e->hooks.rx_end) {
		e->hooks.rx_end(e->ctx);
	}
}

static void rx_fields(m17_engine_t *e, const m17_call_t *c, bool start)
{
	uint8_t deny = m17_engine_deny(&e->cfg, c);

	e->deny = deny;
	notify(e, c, air_voice_rx_fields(&e->av, c, start, deny == M17_DENY_NONE, &e->heard, sizeof e->heard));
}

static void on_start(const m17_call_t *c, void *ctx) { rx_fields(ctx, c, true); }
static void on_update(const m17_call_t *c, void *ctx) { rx_fields(ctx, c, false); }

static void on_voice(const uint8_t payload[M17_STREAM_BYTES], uint16_t fn, void *ctx)
{
	m17_engine_t *e = ctx;

	(void)fn;
	// The squelch settings changed inside this call: it is judged again with the fields it has
	if (air_voice_recheck_due(&e->av))
		rx_fields(e, &e->rx.call, false);
	if (air_voice_rx_voice(&e->av) && e->hooks.rx_voice) {
		e->hooks.rx_voice(payload, e->first, e->ctx);
		e->first = false;
	}
}

static void on_end(void *ctx)
{
	m17_engine_t *e = ctx;

	notify(e, NULL, air_voice_rx_end(&e->av));
}

static const m17_call_sink_t sink = { on_start, on_update, on_voice, on_end };

// ── Transmit: none yet (ADR 006 step 4). air_voice's tx_fill is never reached: nothing begins a transmission ──
static uint16_t t_ready(void *t, uint16_t *first) { (void)t; *first = 0; return 0; }
static const uint8_t *t_syms(void *t) { (void)t; return NULL; }
static void t_sent(void *t, uint16_t n) { (void)t; (void)n; }
static bool t_done(void *t) { (void)t; return true; }
static bool t_voice(void *t, const uint8_t *v) { (void)t; (void)v; return false; }
static void t_end(void *t, const uint8_t *silence) { (void)t; (void)silence; }
static int8_t t_next(void *eng, uint8_t v[AIR_VOICE_FRAME_MAX]) { (void)eng; (void)v; return -1; }
static const uint8_t *t_silence(void *eng) { (void)eng; return NULL; }

_Static_assert(offsetof(m17_engine_t, av) == 0, "air_voice_t first: tx_fill reaches the engine from it");
static const air_voice_tx_ops_t tx_ops = {
	offsetof(m17_engine_t, rx), t_ready, t_syms, t_sent, t_done, t_voice, t_end, t_next, t_silence,
};

bool m17_engine_begin(m17_engine_t *e, const hal_air_t *air, const m17_engine_cfg_t *cfg,
		      const m17_engine_hooks_t *hooks, void *ctx)
{
	memset(e, 0, sizeof *e);
	e->cfg = *cfg;
	e->hooks = *hooks;
	e->ctx = ctx;
	m17_call_rx_init(&e->rx, &sink, e);
	return air_voice_open(&e->av, air, &m17_air_mode, &e->rx, e->frame, e->soft, M17_FRAME_SYMS, &tx_ops);
}

void m17_engine_end(m17_engine_t *e)
{
	air_voice_close(&e->av);
}

void m17_engine_retuned(m17_engine_t *e)
{
	air_voice_retuned(&e->av);
}

void m17_engine_set(m17_engine_t *e, const m17_engine_cfg_t *cfg)
{
	e->cfg = *cfg;
	air_voice_recheck(&e->av);
}

air_tx_state_t m17_engine_tx_state(const m17_engine_t *e)
{
	return air_voice_tx_state(&e->av);
}

void m17_engine_view(const m17_engine_t *e, m17_engine_view_t *out)
{
	air_voice_view_t v;

	air_voice_view(&e->av, &v, &e->heard, &out->rx, sizeof out->rx);
	out->rx_active = v.rx_active;
	out->rx_played = v.rx_played;
	out->deny = e->deny;
	out->tx = v.tx;
	out->rx_calls = v.rx_calls;
	out->tx_calls = v.tx_calls;
}
