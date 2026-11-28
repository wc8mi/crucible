// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 David Behnke
// YSF DN on a channel, the radio core's side (radio_ysf.h)
#include "config.h"
#if CONFIG_YSF	// the whole file
#include <stddef.h>
#include <string.h>
#include "board.h"
#include "radio/radio_ysf.h"
#include "radio/radio_bands.h"
#include "ysf/ysf_engine.h"
#include "ysf/ysf_codeplug.h"
#include "ysf/ysf_id.h"
#include "codeplug/codeplug_modes.h"
#include "dmr/ambe/ambeVoice.h"
#include "dmr/call/dmr_call.h"
#if CONFIG_YSF_VW
#include "p25/p25Voice.h"
#endif

// The voice frame YSF hands on is the one the DMR side's voice path takes and gives (ysf.h)
_Static_assert(YSF_VOICE_BYTES == DMR_AMBE_FRAME_BYTES, "one voice frame size");
_Static_assert(RADIO_YSF_RID_LEN == YSF_RID_LEN && RADIO_YSF_CALL_LEN == YSF_CS_LEN, "one field size");


static radio_ysf_settings_t set;
// Rooms: the command armed for the next transmission, and the node's last reply as published (the provider's task
// writes it, any task reads it: wx_seq odd while it writes). The room list itself stays in the engine's parser
// (ysf_wx_rx_t.out): it goes with the engine, as the page needs the YSF channel anyway
static uint8_t wx_buf[YSF_WX_CMD_MAX], wx_len, wx_cmd = YSF_WX_NONE;
static uint32_t wx_arg;
static radio_ysf_wx_t wx_state = { .asked_cmd = YSF_WX_NONE };
static volatile uint8_t wx_seq;
// This radio's RID and where it came from: worked out on the radio task only (init, and again at each begin while
// it is the fallback, which follows the codeplug); other tasks copy it under rid_seq (odd while it is written)
static char rid[RADIO_YSF_RID_LEN + 1];
static bool rid_weak;
static volatile uint8_t rid_seq;
static ysf_engine_t *eng;		// while running
static radio_ysf_stats_t stats;
static ysf_call_stats_t call_base;	// the call layer's counters already counted (they restart with the engine)
static radio_air_counts_t air_base;	// the same for the engine's and the session's
static uint32_t drops_base;		// the engine's path_drops already counted
// The voice path running (YSF_PATH_*, ysf.md 5.5): the DMR side's from begin; P25's IMBE one for a VW call
// (CONFIG_YSF_VW). Radio task only
static uint8_t path_on = YSF_PATH_AMBE;

static void rid_update(void)
{
	hal_chip_t chip;
	char r[RADIO_YSF_RID_LEN + 1];
	bool have = board.system != NULL && board.system->chip != NULL && board.system->chip(&chip) &&
		    ysf_uid_usable(chip.uid);

	if (have) {
		ysf_rid_from_uid(chip.uid, r);
	} else {
		char name[CODEPLUG_NAME_LEN + 1];

		codeplug_radio_name(name);
		ysf_rid_fallback(codeplug_dmr_id(), name, r);
	}
	rid_seq++;
	memcpy(rid, r, sizeof rid);
	rid_weak = !have;
	rid_seq++;
}

void radio_ysf_init(void)
{
	memset(&set, 0, sizeof set);
	eng = NULL;
	rid_update();		// the chip's ID now; a fallback is worked out again at each begin, once the codeplug is read
}

void radio_ysf_settings(radio_ysf_settings_t *out)
{
	*out = set;
}

void radio_ysf_set_settings(const radio_ysf_settings_t *s)
{
	set = *s;
	if (set.tx_dgid > RADIO_YSF_DGID_MAX)
		set.tx_dgid = 0;
	if (set.rx_dgid > RADIO_YSF_DGID_MAX)
		set.rx_dgid = 0;
	if (set.temp_dgid > RADIO_YSF_DGID_MAX)
		set.temp_chan = 0;
	if (set.voice > YSF_CP_VOICE_VD1)
		set.voice = YSF_CP_VOICE_DN;
}

uint32_t radio_ysf_fit(void)
{
	return board.air != NULL ? ysf_engine_fit(board.air, false) : 0;
}

bool radio_ysf_fits(void)
{
	return (radio_ysf_fit() & (AIR_FIT_RX | AIR_FIT_TX)) != 0;
}

bool radio_ysf_take(channel_t *ch)
{
	if (ch->kind != CODEPLUG_KIND_YSF || !radio_ysf_fits() || radio_listen_band(ch->rx_hz))
		return false;
	ch->mode = RADIO_MODE_YSF;
	ch->bw = RADIO_BW_12K5;
	return true;
}

// ── Who we are on air ──
// The RID: from the MCU's unique ID, the one source that differs from radio to radio by construction. Only a board
// without one falls back to the DMR ID and callsign, which a copied codeplug would share: the view says so
// (RADIO_YSF_RID_WEAK) and the screen that shows the RID marks it. Worked out at first use and, for the fallback, at
// each engine start (it follows a codeplug change); read by any task


void radio_ysf_rid(char out[RADIO_YSF_RID_LEN + 1], bool *weak)
{
	uint8_t s;
	bool w;

	do {
		s = rid_seq;
		memcpy(out, rid, sizeof rid);
		w = rid_weak;
	} while ((s & 1) || s != rid_seq);
	if (weak)
		*weak = w;
}

void radio_ysf_callsign(const char *radio_name, char out[RADIO_YSF_CALL_LEN + 1])
{
	ysf_radio_t r;

	ysf_radio_load(&r);
	if (r.callsign[0] != '\0') {
		memcpy(out, r.callsign, sizeof r.callsign);
		return;
	}
	ysf_callsign_from_name(radio_name, out);
}

// The engine's configuration for channel ch at number (0: the VFO): a live stored YSF channel's tag gives the
// DG-IDs (or the quick settings' DG-ID for it, for now), the voice mode and the deviation, else the settings
static void params(const channel_t *ch, uint16_t number, const char *radio_name, ysf_engine_cfg_t *c)
{
	ysf_chan_t yc;
	bool weak;

	memset(c, 0, sizeof *c);
	c->silence = dmr_ambe_silence;
	if (number != set.temp_chan)
		set.temp_chan = 0;	// another channel (or the VFO): the quick DG-ID was for the one left
	if (number != 0 && ysf_chan_load(number, ch, &yc)) {
		c->rx_dgid = yc.rx_dgid;
		c->tx.dgid = yc.tx_dgid;
		c->voice = yc.voice;
		c->narrow = yc.narrow;
	} else {
		c->rx_dgid = set.rx_dgid;
		c->tx.dgid = set.tx_dgid;
		c->voice = set.voice;
		c->narrow = set.narrow;
	}
	if (number != 0 && number == set.temp_chan) {
		c->rx_dgid = set.temp_dgid;	// the quick DG-ID: the channel's voice mode and deviation stay
		c->tx.dgid = set.temp_dgid;
	}
	if (c->voice > YSF_CP_VOICE_VD1 || (!CONFIG_YSF_VW && c->voice == YSF_CP_VOICE_VW))
		c->voice = YSF_CP_VOICE_DN;	// only a mode this build sends (ADR 005 6: a VW channel sends DN without VW)
	c->tx.mr = YSF_MR_DIRECT;
	radio_ysf_callsign(radio_name, c->tx.callsign);
	radio_ysf_rid(c->tx.radio_id, &weak);
	c->tx_inhibit = c->tx.callsign[0] == '\0';
}

// ── Voice: the DMR side's voice path (ambeVoice.h), as radio_nxdn.c uses it; P25's for VW (p25Voice.h) ──
// The caller for the receive AGC's memory (rx-agc.md): its callsign, else its radio ID; the DG-ID as the scope. The
// fields fill in as the call goes on (a late entry learns them from the DCH), so the update passes them again. Both
// paths take it: the one not running ignores it until its next call
#if CONFIG_RX_AGC
static uint32_t id_key;
static uint8_t id_dgid;
static void v_identify(const ysf_call_t *c)
{
	id_key = c != NULL ? ysf_caller_key(c->src_id, c->src) : 0;
	id_dgid = c != NULL ? c->dgid : 0;
	ambeVoiceRxIdentity(RX_AGC_YSF, id_key, id_dgid);
#if CONFIG_YSF_VW
	p25VoiceRxIdentity(id_key, id_dgid);
#endif
}
static void v_update(const ysf_call_t *c, void *ctx) { (void)ctx; v_identify(c); }
#else
static void v_identify(const ysf_call_t *c) { (void)c; }
#define v_update NULL
#endif
static void v_start(const ysf_call_t *c, void *ctx) { (void)ctx; v_identify(c); }
static void v_voice(const uint8_t v[YSF_VOICE_BYTES], void *ctx) { (void)ctx; radio_air_voice_rx_frame(v); }
static bool v_tx(uint8_t v[YSF_VOICE_BYTES], void *ctx) { (void)ctx; return radio_air_voice_tx_next(v); }

// The call's voice opens on a path before its first frame there and closes at the call's end (ysf_engine.h): the
// stream's call boundaries, so a call that switched paths ends on each
static void v_path(uint8_t path, bool open, void *ctx)
{
	(void)ctx;
#if CONFIG_YSF_VW
	if (path == YSF_PATH_IMBE) {
		const p25_engine_hooks_t *h = p25VoiceHooks();

		if (open) {
			h->rx_start(NULL, NULL);
#if CONFIG_RX_AGC
			p25VoiceRxIdentity(id_key, id_dgid);
#endif
		} else {
			h->rx_end(NULL);
		}
		return;
	}
#endif
	if (open)
		radio_air_voice_rx_start();
	else
		radio_air_voice_rx_end();
}

#if CONFIG_YSF_VW
static void v_vw(const uint8_t imbe[YSF_VW_BYTES], uint8_t errs, void *ctx)
{
	(void)ctx;
	p25VoiceHooks()->rx_voice(imbe, errs, NULL);
}
#else
#define v_vw NULL
#endif

// A node's reply: published for the screens. A list keeps the page asked for as its first entry
static void v_wx(const ysf_wx_t *w, void *ctx)
{
	(void)ctx;
	wx_seq++;
	if (w->kind == YSF_WX_ALL) {
		wx_state.search = w->search;
		wx_state.rooms = w->rooms;
		wx_state.rooms_total = w->rooms_total;
	}
	// The parser keeps the node and room of the last replies, and drops them when another node answers
	wx_state.connected = w->connected;
	memcpy(wx_state.node_name, w->node_name, sizeof wx_state.node_name);
	memcpy(wx_state.room_id, w->room_id, sizeof wx_state.room_id);
	memcpy(wx_state.room_name, w->room_name, sizeof wx_state.room_name);
	memcpy(wx_state.node_call, w->node_call, sizeof wx_state.node_call);
	wx_state.kind = w->kind;
	wx_state.serial++;
	wx_seq++;
}

static const ysf_engine_hooks_t voice_hooks = { v_start, v_update, v_voice, NULL, v_tx, v_wx, v_path, v_vw };

bool radio_ysf_wx_arm(uint8_t cmd, uint32_t arg, const char *text)
{
	wx_len = ysf_wx_command(cmd, arg, text, wx_buf);
	wx_cmd = wx_len ? cmd : YSF_WX_NONE;
	wx_arg = arg;
	return wx_len != 0;
}

void radio_ysf_wx_disarm(void)
{
	wx_cmd = YSF_WX_NONE;
}

void radio_ysf_wx(radio_ysf_wx_t *out)
{
	uint8_t s;

	do {
		s = wx_seq;
		*out = wx_state;
	} while ((s & 1) || s != wx_seq);
}

bool radio_ysf_wx_room(uint8_t i, ysf_wx_room_t *out)
{
	uint8_t s;
	bool ok;

	do {
		const ysf_engine_t *e = eng;

		s = wx_seq;
		ok = e != NULL && i < wx_state.rooms;
		if (ok)
			*out = e->rx.wx.out.room[i];
	} while ((s & 1) || s != wx_seq);
	return ok;
}

static ysf_engine_t *engine_mem(void)
{
#if CONFIG_YSF_VW
	size_t len = 0;
	void *m = radio_air_ysf_mem(&len);

	return m != NULL && len >= sizeof(ysf_engine_t) && ((uintptr_t)m & 7u) == 0 ? m : NULL;
#else
	return radio_air_engine_get(sizeof(ysf_engine_t));
#endif
}

// The voice path running stops: the DMR side's stream, or P25's (its memory free again)
static void path_stop(void)
{
#if CONFIG_YSF_VW
	if (path_on == YSF_PATH_IMBE) {
		p25VoiceEnd();
		return;
	}
#endif
	if (path_on == YSF_PATH_AMBE)
		ambeVoiceStop();
}

#if CONFIG_YSF_VW
// IMBE: P25's voice path with a VW frame's 5 voice frames for a burst and YSF's receive AGC settings. False: no memory
static bool path_start(uint8_t path)
{
	if (path == YSF_PATH_IMBE) {
		size_t len = 0;
		void *m = radio_air_imbe_mem(&len);

		if (m == NULL || len < sizeof(p25_voice_mem_t) || ((uintptr_t)m & 7u) != 0)
			return false;
		p25VoiceBegin(m, YSF_VCH_PER_FRAME, RX_AGC_YSF);
		return true;
	}
	ambeVoiceStart();
	return true;
}

// The radio task's side of a switch (ysf_engine.h): the call heard wants the other path, and nothing is transmitted.
// The engine drops frames from the close; once no hook is left on the old path it stops and the new one starts. A
// path that can't start (no memory) leaves the DMR side's, whose frames this call doesn't use: its voice is lost,
// and no switch is tried again until the call wanting it ends
static uint8_t sw_to, sw_token;
static bool sw_busy, sw_failed;

static void path_service(void)
{
	uint8_t want = ysf_engine_path_want(eng);

	if (!sw_busy) {
		if (want == YSF_PATH_AMBE)
			sw_failed = false;
		if (want == path_on || sw_failed || ysf_engine_tx_state(eng) != AIR_TX_IDLE)
			return;
		sw_to = want;
		sw_token = ysf_engine_path_close(eng);
		sw_busy = true;
	}
	if (!ysf_engine_path_quiet(eng, sw_token))
		return;		// the next poll
	sw_busy = false;
	path_stop();
	path_on = sw_to;
	if (!path_start(path_on)) {
		stats.path_fails++;
		sw_failed = true;
		path_on = YSF_PATH_AMBE;
		ambeVoiceStart();
	}
	stats.path_switches++;
	ysf_engine_path_open(eng, path_on);
}
#endif

bool radio_ysf_begin(const channel_t *ch, uint16_t number, const char *radio_name)
{
	ysf_engine_cfg_t cfg;

	radio_ysf_end();
	stats.begins++;
	if (rid_weak)
		rid_update();	// the fallback follows the codeplug
	params(ch, number, radio_name, &cfg);
	eng = engine_mem();
	if (eng == NULL || board.air == NULL || ysf_engine_fit(board.air, cfg.narrow) == 0) {
		eng = NULL;
		stats.begin_fails++;
		return false;
	}
	ambeVoiceStart();
	if (!ysf_engine_begin(eng, board.air, &cfg, &voice_hooks, NULL)) {
		ambeVoiceStop();
		eng = NULL;
		stats.begin_fails++;
		return false;
	}
	path_on = YSF_PATH_AMBE;
	memset(&call_base, 0, sizeof call_base);
	memset(&air_base, 0, sizeof air_base);
	drops_base = 0;
	return true;
}

void radio_ysf_end(void)
{
	if (eng == NULL)
		return;
	wx_seq++;
	wx_state.rooms = 0;	// the list lived in the engine
	wx_seq++;
	ysf_engine_end(eng);
	path_stop();
	path_on = YSF_PATH_AMBE;
#if CONFIG_YSF_VW
	sw_busy = sw_failed = false;
#endif
	eng = NULL;
}

bool radio_ysf_running(void)
{
	return eng != NULL;
}

void radio_ysf_retuned(void)
{
	if (eng != NULL)
		ysf_engine_retuned(eng);
}

void radio_ysf_apply(const channel_t *ch, uint16_t number, const char *radio_name)
{
	ysf_engine_cfg_t cfg;

	if (eng == NULL || ysf_engine_tx_state(eng) != AIR_TX_IDLE)
		return;
	params(ch, number, radio_name, &cfg);
	ysf_engine_set(eng, &cfg);
}

static void count(const ysf_engine_view_t *ev)
{
	const ysf_call_stats_t *c = &eng->rx.stats;

	radio_air_count(&stats.air, &air_base, ev->rx_calls, ev->tx_calls, ev->tx_filler, &eng->av.session.stats);
	stats.frames += c->frames - call_base.frames;
	stats.fich_fail += c->fich_fail - call_base.fich_fail;
	stats.csd_fail += c->csd_fail - call_base.csd_fail;
	stats.dch_fail += c->dch_fail - call_base.dch_fail;
	stats.voice += c->voice - call_base.voice;
	stats.voice_held += c->voice_held - call_base.voice_held;
	stats.fec_bits += c->fec_bits - call_base.fec_bits;
	stats.vch_fixed += c->vch_fixed - call_base.vch_fixed;
	stats.vd1_frames += c->vd1_frames - call_base.vd1_frames;
	stats.vw_frames += c->vw_frames - call_base.vw_frames;
	stats.vw_fec_bits += c->vw_fec_bits - call_base.vw_fec_bits;
	call_base = *c;
	stats.path_drops += ev->path_drops - drops_base;
	drops_base = ev->path_drops;
}

bool radio_ysf_poll(radio_ysf_view_t *v)
{
	ysf_engine_view_t ev;
	radio_ysf_view_t n;
	bool weak;
	char me[RADIO_YSF_RID_LEN + 1];

	if (eng == NULL) {
		n = *v;		// the last call heard stays
		n.flags = 0;
		n.tx = AIR_TX_IDLE;
		n.rx_dgid = set.rx_dgid;
		n.tx_dgid = set.tx_dgid;
		n.voice = set.voice;
		n.narrow = set.narrow;
	} else {
#if CONFIG_YSF_VW
		path_service();
		if (path_on == YSF_PATH_IMBE)
			p25VoicePoll();
#endif
		ysf_engine_view(eng, &ev);
		count(&ev);
		memset(&n, 0, sizeof n);
		n.flags = RADIO_YSF_ON;
		if (ev.rx_active) {
			n.flags |= RADIO_YSF_RX;
			if (ev.rx_played)
				n.flags |= RADIO_YSF_PLAYED;
			if (ev.rx.late)
				n.flags |= RADIO_YSF_LATE;
			if (ev.rx.dt == YSF_DT_DATA || (!CONFIG_YSF_VW && ev.rx.dt == YSF_DT_VW))
				n.flags |= RADIO_YSF_NOT_DN;
			if (ev.rx.dt == YSF_DT_DATA)
				n.flags |= RADIO_YSF_DATA;
		}
		if (eng->cfg.tx_inhibit)
			n.flags |= RADIO_YSF_NO_CALL;
		n.tx = ev.tx;
		n.dgid = ev.rx.dgid;
		n.dt = ev.rx.dt;
		n.path = eng->path;
		n.rx_dgid = eng->cfg.rx_dgid;
		n.tx_dgid = eng->cfg.tx.dgid;
		n.voice = eng->cfg.voice;
		n.narrow = eng->cfg.narrow;
		memcpy(n.src, ev.rx.src, sizeof n.src);
		memcpy(n.src_rid, ev.rx.src_id, sizeof n.src_rid);
	}
	radio_ysf_rid(me, &weak);
	if (weak)
		n.flags |= RADIO_YSF_RID_WEAK;
	n.fit = (uint16_t)radio_ysf_fit();
	bool changed = memcmp(&n, v, sizeof n) != 0;
	*v = n;
	return changed;
}

static bool ysf_no_tx(void)
{
	return eng != NULL && (eng->av.session.fit & AIR_FIT_TX) == 0;
}

static bool ysf_tx_prepare(void)
{
	// Transmit is DN (or room data) on the DMR side's path: refused while a VW call has P25's, or a switch is due
	if (eng == NULL || path_on != YSF_PATH_AMBE || ysf_engine_path_want(eng) != YSF_PATH_AMBE)
		return false;
	if (wx_cmd != YSF_WX_NONE) {
		// A room command armed: this transmission is it, and ends by itself
		uint8_t cmd = wx_cmd;

		wx_cmd = YSF_WX_NONE;
		if (!ysf_engine_tx_prepare_data(eng, wx_buf, wx_len))
			return false;
		wx_seq++;
		wx_state.asked++;
		wx_state.asked_cmd = cmd;
		if (cmd == YSF_WX_ALL || cmd == YSF_WX_SEARCH)
			wx_state.rooms_first = (uint16_t)(cmd == YSF_WX_ALL && wx_arg >= 1 ? wx_arg : 1);
		wx_seq++;
		return true;
	}
	if (!ysf_engine_tx_prepare(eng))
		return false;
	ambeVoiceTxProfile(AMBE_TX_YSF);	// its own leveling target (ambeVoice.h); begin() takes it
	radio_air_voice_tx(true);
	return true;
}

static bool ysf_tx_go(void)
{
	return eng != NULL && ysf_engine_tx_go(eng);
}

static void ysf_tx_stop(void)
{
	if (eng != NULL) {
		radio_air_voice_tx(false);
		ysf_engine_tx_stop(eng);
	}
}

static void ysf_tx_abort(void)
{
	if (eng != NULL) {
		radio_air_voice_tx(false);
		ysf_engine_tx_abort(eng);
	}
}

static uint8_t ysf_tx_state(void)
{
	return eng != NULL ? (uint8_t)ysf_engine_tx_state(eng) : (uint8_t)AIR_TX_IDLE;
}

static void ysf_tx_clear(void)
{
	if (eng != NULL) {
		radio_air_voice_tx(false);
		ysf_engine_tx_clear(eng);
	}
}

const radio_air_ops_t radio_ysf_air = {
	radio_ysf_running, ysf_no_tx, ysf_tx_prepare, ysf_tx_go, ysf_tx_stop, ysf_tx_abort, ysf_tx_state, ysf_tx_clear,
};

void radio_ysf_stats(radio_ysf_stats_t *out)
{
	*out = stats;
}

void radio_ysf_reset_stats(void)
{
	memset(&stats, 0, sizeof stats);
}

#endif	// CONFIG_YSF
