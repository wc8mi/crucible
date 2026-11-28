// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 David Behnke
// YSF DN voice on a channel (ysf_engine.h), on the shared voice engine (air_voice.h)
#include "ysf_engine.h"

#include <stddef.h>
#include <string.h>

#include "config.h"

_Static_assert(YSF_VOICE_BYTES <= AIR_VOICE_FRAME_MAX, "a voice frame fits air_voice's");

uint32_t ysf_engine_fit(const hal_air_t *air, bool narrow)
{
	return air_session_fit(air, narrow ? &ysf_air_mode_narrow : &ysf_air_mode);
}

// ── Receive (provider's task) ──
// Voice this build plays (DN, V/D mode 1, VW with CONFIG_YSF_VW) whose DG-ID is ours, or every call with ours at 0
// (ysf.md 5.1)
static bool wanted(const ysf_engine_t *e, const ysf_call_t *c)
{
	bool voice = c->dt == YSF_DT_VD2 || c->dt == YSF_DT_VD1 || (CONFIG_YSF_VW && c->dt == YSF_DT_VW);

	return voice && (e->cfg.rx_dgid == YSF_DGID_ALL || c->dgid == e->cfg.rx_dgid);
}

// A voice frame for path: asked for (the radio core switches to it), and true when it may go there now, the path
// opened for the call first. The caller holds the gate (gate_in, gate_out) around this and the hook it calls
static bool path_ready(ysf_engine_t *e, uint8_t path)
{
	e->want = path;
	if (e->path != path) {
		e->path_drops++;
		return false;
	}
	if (e->opened != path) {
		e->opened = path;
		if (e->hooks.rx_path)
			e->hooks.rx_path(path, true, e->ctx);
	}
	return true;
}

static void gate_in(ysf_engine_t *e) { e->gate = (uint8_t)(e->gate + 1); }
static void gate_out(ysf_engine_t *e) { e->gate = (uint8_t)(e->gate + 1); }

static void notify(ysf_engine_t *e, const ysf_call_t *c, air_voice_event_t ev)
{
	if (ev == AIR_VOICE_END) {
		// The call no longer plays (it ended, or left our DG-ID): its voice closes on the path it opened, if a switch
		// hasn't closed that already
		gate_in(e);
		if (e->opened != YSF_PATH_NONE && e->opened == e->path && e->hooks.rx_path)
			e->hooks.rx_path(e->opened, false, e->ctx);
		e->opened = YSF_PATH_NONE;
		gate_out(e);
	}
	if (ev == AIR_VOICE_START && e->hooks.rx_start)
		e->hooks.rx_start(c, e->ctx);
	else if (ev == AIR_VOICE_UPDATE && e->hooks.rx_update)
		e->hooks.rx_update(c, e->ctx);
	else if (ev == AIR_VOICE_END && e->hooks.rx_end)
		e->hooks.rx_end(e->ctx);
}

static void rx_fields(ysf_engine_t *e, const ysf_call_t *c, bool start)
{
	notify(e, c, air_voice_rx_fields(&e->av, c, start, wanted(e, c), &e->heard, sizeof e->heard));
}

static void on_start(const ysf_call_t *c, void *ctx)
{
	rx_fields(ctx, c, true);
}

static void on_update(const ysf_call_t *c, void *ctx)
{
	rx_fields(ctx, c, false);
}

static void on_voice(const uint8_t v[YSF_VOICE_BYTES], void *ctx)
{
	ysf_engine_t *e = ctx;

	if (air_voice_recheck_due(&e->av))
		rx_fields(e, &e->rx.call, false);
	if (!air_voice_rx_voice(&e->av))
		return;
	gate_in(e);
	if (path_ready(e, YSF_PATH_AMBE) && e->hooks.rx_voice)
		e->hooks.rx_voice(v, e->ctx);
	gate_out(e);
}

#if CONFIG_YSF_VW
static void on_vw(const uint8_t imbe[YSF_VW_BYTES], uint8_t errs, void *ctx)
{
	ysf_engine_t *e = ctx;

	if (air_voice_recheck_due(&e->av))
		rx_fields(e, &e->rx.call, false);
	if (!air_voice_rx_voice(&e->av))
		return;
	gate_in(e);
	if (path_ready(e, YSF_PATH_IMBE) && e->hooks.rx_vw)
		e->hooks.rx_vw(imbe, errs, e->ctx);
	gate_out(e);
}
#else
#define on_vw NULL
#endif

static void on_end(void *ctx)
{
	ysf_engine_t *e = ctx;

	e->want = YSF_PATH_AMBE;	// between calls: the path a transmission sends on
	notify(e, NULL, air_voice_rx_end(&e->av));
}

static void on_wx(const ysf_wx_t *w, void *ctx)
{
	ysf_engine_t *e = ctx;

	if (e->hooks.rx_wx)
		e->hooks.rx_wx(w, e->ctx);
}

static const ysf_call_sink_t sink = { on_start, on_update, on_voice, on_end, on_wx, on_vw };

// ── Transmit: the call layer behind air_voice's tx_fill ──
static uint16_t t_ready(void *t, uint16_t *first) { return ysf_call_tx_ready(t, first); }
static const uint8_t *t_syms(void *t) { return ysf_call_tx_syms(t); }
static void t_sent(void *t, uint16_t n) { ysf_call_tx_sent(t, n); }
static bool t_done(void *t) { return ysf_call_tx_done(t); }
static bool t_voice(void *t, const uint8_t *v) { return ysf_call_tx_voice(t, v); }
static void t_end(void *t, const uint8_t *silence) { ysf_call_tx_end(t, silence); }

// The encoder's next frame through the hook, and the silence frame from the configuration
static int8_t t_next(void *eng, uint8_t v[AIR_VOICE_FRAME_MAX])
{
	ysf_engine_t *e = eng;

	if (!e->hooks.tx_voice)
		return -1;
	return e->hooks.tx_voice(v, e->ctx) ? 1 : 0;
}
static const uint8_t *t_silence(void *eng) { return ((ysf_engine_t *)eng)->cfg.silence; }

_Static_assert(offsetof(ysf_engine_t, av) == 0, "air_voice_t first: tx_fill reaches the engine from it");
static const air_voice_tx_ops_t tx_ops = {
	offsetof(ysf_engine_t, tx), t_ready, t_syms, t_sent, t_done, t_voice, t_end, t_next, t_silence,
};

bool ysf_engine_begin(ysf_engine_t *e, const hal_air_t *air, const ysf_engine_cfg_t *cfg,
		      const ysf_engine_hooks_t *hooks, void *ctx)
{
	memset(e, 0, sizeof *e);
	e->cfg = *cfg;
	e->hooks = *hooks;
	e->ctx = ctx;
	e->path = e->want = YSF_PATH_AMBE;	// the radio core starts the DMR side's voice path with the engine
	ysf_call_rx_init(&e->rx, &sink, e);
#if CONFIG_AIR_RX_ADAPT
	air_softmodem_lend(&e->adapt);	// before the open: a provider with a soft modem sets it up there
#endif
	if (air_voice_open(&e->av, air, cfg->narrow ? &ysf_air_mode_narrow : &ysf_air_mode, &e->rx.rx, e->frame, e->soft, YSF_FRAME_SYMS, &tx_ops))
		return true;
#if CONFIG_AIR_RX_ADAPT
	air_softmodem_lend(NULL);
#endif
	return false;
}

void ysf_engine_end(ysf_engine_t *e)
{
	air_voice_close(&e->av);
#if CONFIG_AIR_RX_ADAPT
	air_softmodem_lend(NULL);
#endif
}

void ysf_engine_retuned(ysf_engine_t *e)
{
	air_voice_retuned(&e->av);
}

void ysf_engine_set(ysf_engine_t *e, const ysf_engine_cfg_t *cfg)
{
	e->cfg = *cfg;
	air_voice_recheck(&e->av);
}

bool ysf_engine_tx_prepare(ysf_engine_t *e)
{
	if (e->cfg.tx_inhibit || !air_voice_tx_can(&e->av, true))
		return false;
	if (air_voice_tx_begin(&e->av))
		ysf_call_tx_begin(&e->tx, &e->cfg.tx);
	return air_voice_tx_prime(&e->av);
}

bool ysf_engine_tx_prepare_data(ysf_engine_t *e, const uint8_t *data, uint8_t len)
{
	if (e->cfg.tx_inhibit || !len || !air_voice_tx_can(&e->av, false))
		return false;
	if (air_voice_tx_begin(&e->av))
		ysf_call_tx_begin_data(&e->tx, &e->cfg.tx, data, len);
	return air_voice_tx_prime(&e->av);
}

bool ysf_engine_tx_go(ysf_engine_t *e)
{
	return air_voice_tx_go(&e->av);
}

void ysf_engine_tx_stop(ysf_engine_t *e)
{
	air_voice_tx_stop(&e->av);
}

void ysf_engine_tx_abort(ysf_engine_t *e)
{
	air_voice_tx_abort(&e->av);
}

air_tx_state_t ysf_engine_tx_state(const ysf_engine_t *e)
{
	return air_voice_tx_state(&e->av);
}

void ysf_engine_tx_clear(ysf_engine_t *e)
{
	air_voice_tx_clear(&e->av);
}

void ysf_engine_view(const ysf_engine_t *e, ysf_engine_view_t *out)
{
	air_voice_view_t v;

	air_voice_view(&e->av, &v, &e->heard, &out->rx, sizeof out->rx);
	out->rx_active = v.rx_active;
	out->rx_played = v.rx_played;
	out->tx = v.tx;
	out->rx_calls = v.rx_calls;
	out->tx_calls = v.tx_calls;
	out->tx_filler = v.tx_filler;
	out->path_drops = e->path_drops;
}

uint8_t ysf_engine_path_want(const ysf_engine_t *e)
{
	return e->want;
}

uint8_t ysf_engine_path_close(ysf_engine_t *e)
{
	e->path = YSF_PATH_NONE;
	return e->gate;		// read after the write: a hook entered from now on sees NONE
}

bool ysf_engine_path_quiet(const ysf_engine_t *e, uint8_t token)
{
	// Even: no hook was running at the close. Odd: one was, on the old path; quiet once the gate moved on
	return (token & 1u) == 0 || e->gate != token;
}

void ysf_engine_path_open(ysf_engine_t *e, uint8_t path)
{
	e->path = path;
}
