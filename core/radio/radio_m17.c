// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// M17 on a channel, the radio core's side (radio_m17.h). The shape is radio_nxdn.c's; the voice stream is run as
// p25Voice.c runs its own (started at the first call, beeps mixed in, the speaker routed while a call plays)
#include "config.h"
#if CONFIG_M17	// the whole file
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "board.h"
#include "radio/radio_m17.h"
#include "radio/radio_bands.h"
#include "m17/m17_engine.h"
#include "m17/m17_voice.h"
#include "m17/m17_codeplug.h"
#include "codeplug/codeplug_modes.h"

#define LOAD(x)		__atomic_load_n(&(x), __ATOMIC_ACQUIRE)
#define STORE(x, v)	__atomic_store_n(&(x), (v), __ATOMIC_RELEASE)

static const hal_audio_t *aud;
static radio_m17_settings_t set;
static m17_engine_t *eng;		// while running
static radio_m17_settings_t run;	// the parameters in force while running
static bool no_call;			// no callsign to send as (the view says so; transmit will refuse)
static radio_m17_stats_t stats;
static m17_call_stats_t call_base;	// the call layer's counters already counted (they restart with the engine)
static m17_voice_stats_t voice_base;
static radio_air_counts_t air_base;

// The stream: the radio task starts and stops it; the callback routes the speaker
static bool streaming;
static uint8_t calls;			// bumped by the provider's task at each call played: the stream starts at the first
static bool (*mixer)(int16_t *spk);
static bool cb_speaker, cb_speaker_unsent;

void radio_m17_init(const hal_audio_t *audio)
{
	aud = audio;
	memset(&set, 0, sizeof set);
	eng = NULL;
	streaming = false;
}

void radio_m17_set_mixer(bool (*mix)(int16_t *spk))
{
	STORE(mixer, mix);
}

uint32_t radio_m17_fit(void)
{
	return board.air != NULL ? m17_engine_fit(board.air) : 0;
}

bool radio_m17_fits(void)
{
	return (radio_m17_fit() & (AIR_FIT_RX | AIR_FIT_TX)) != 0;
}

bool radio_m17_take(channel_t *ch)
{
	if (ch->kind != CODEPLUG_KIND_M17 || !radio_m17_fits() || radio_listen_band(ch->rx_hz))
		return false;
	ch->mode = RADIO_MODE_M17;
	ch->bw = RADIO_BW_12K5;
	return true;
}

bool radio_m17_receives(void)
{
	return (radio_m17_fit() & AIR_FIT_RX) != 0;
}

void radio_m17_settings(radio_m17_settings_t *out)
{
	*out = set;
}

// A destination as the codeplug holds it: upper case, the set m17_addr_dest takes, "ALL" as ""
static void dest_clean(char d[RADIO_M17_DEST_LEN + 1])
{
	size_t n = strnlen(d, RADIO_M17_DEST_LEN);
	uint64_t a;

	d[n] = '\0';
	for (size_t i = 0; i < n; i++)
		if (d[i] >= 'a' && d[i] <= 'z')
			d[i] = (char)(d[i] - 'a' + 'A');
	while (n > 0 && d[n - 1] == ' ')
		d[--n] = '\0';
	if (strcmp(d, "ALL") == 0 || !m17_addr_dest(d, n, &a))
		d[0] = '\0';
}

void radio_m17_set_settings(const radio_m17_settings_t *s)
{
	set = *s;
	set.can &= M17_CP_CAN_MASK;
	dest_clean(set.dest);
}

// The parameters for channel ch at number (0: the VFO): a live stored M17 channel's block, else the settings
static void params(const channel_t *ch, uint16_t number, const char *radio_name, radio_m17_settings_t *p)
{
	m17_chan_t mc;
	char call[M17_CP_CALL_LEN + 1];

	if (number != 0 && m17_chan_load(number, ch, &mc)) {
		p->can = mc.can;
		p->hear_any = mc.hear_any;
		memcpy(p->dest, mc.dest, sizeof p->dest);
		dest_clean(p->dest);
	} else {
		*p = set;
	}
	m17_callsign(radio_name, call);
	no_call = call[0] == '\0';
}

static void engine_cfg(const radio_m17_settings_t *p, m17_engine_cfg_t *c)
{
	c->can = p->can;
	c->hear_any = p->hear_any;
}

// ── Voice. The hooks run in the provider's task, the only one feeding m17_voice while M17 runs ──
static void v_start(const m17_call_t *c, void *ctx)
{
	(void)c;
	(void)ctx;
	STORE(calls, (uint8_t)(calls + 1u));
}

static void v_voice(const uint8_t payload[M17_STREAM_BYTES], bool first, void *ctx)
{
	(void)ctx;
	m17_voice_rx(payload, first);
}

static void v_end(void *ctx)
{
	(void)ctx;
	m17_voice_rx_end();
}

static const m17_engine_hooks_t voice_hooks = { v_start, NULL, v_voice, v_end };

// ── The stream's callback (audio task) ──
static void speaker(bool on)
{
	if (cb_speaker != on || LOAD(cb_speaker_unsent)) {
		cb_speaker = on;
		STORE(cb_speaker_unsent, false);
		if (aud != NULL && aud->speaker_pcm != NULL)
			aud->speaker_pcm(on);
	}
}

static void block(const int16_t *mic, int16_t *spk, void *ctx)
{
	bool (*mix)(int16_t *) = LOAD(mixer);
	bool on;

	(void)ctx;
	on = m17_voice_block(mic, spk);
	if (mix != NULL && mix(spk))
		on = true;
	speaker(on);
}

static void stream_start(void)
{
	STORE(cb_speaker_unsent, true);		// the route is what the stream's last user left: sent at the first block
	cb_speaker = false;
	streaming = aud != NULL && aud->start != NULL && aud->start(block, NULL);
}

static void stream_stop(void)
{
	if (streaming && aud != NULL) {
		if (aud->stop != NULL)
			aud->stop();
		if (aud->speaker_pcm != NULL)
			aud->speaker_pcm(false);
	}
	streaming = false;
}

// The engine's memory: 8-aligned and large enough, else none
static m17_engine_t *engine_get(void)
{
	size_t len = 0;
	void *m = radio_m17_engine_mem(&len);

	if (m == NULL || len < sizeof(m17_engine_t) || ((uintptr_t)m & 7u) != 0)
		return NULL;
	return m;
}

static m17_voice_mem_t *voice_get(void)
{
	size_t len = 0;
	void *m = radio_m17_voice_mem(&len);

	if (m == NULL || len < sizeof(m17_voice_mem_t) || ((uintptr_t)m & 7u) != 0)
		return NULL;
	return m;
}

bool radio_m17_begin(const channel_t *ch, uint16_t number, const char *radio_name)
{
	m17_engine_cfg_t cfg;
	m17_voice_mem_t *vm;

	radio_m17_end();
	stats.begins++;
	params(ch, number, radio_name, &run);
	engine_cfg(&run, &cfg);
	eng = engine_get();
	vm = voice_get();
	if (eng == NULL || vm == NULL || board.air == NULL || m17_engine_fit(board.air) == 0) {
		eng = NULL;
		stats.begin_fails++;
		return false;
	}
	m17_voice_begin(vm);	// before the engine: its first call may come at once
	calls = 0;
	if (!m17_engine_begin(eng, board.air, &cfg, &voice_hooks, NULL)) {
		m17_voice_end();
		eng = NULL;
		stats.begin_fails++;
		return false;
	}
	memset(&call_base, 0, sizeof call_base);
	memset(&voice_base, 0, sizeof voice_base);
	m17_voice_stats(&voice_base);	// its counters run on across begins: what moved from here
	memset(&air_base, 0, sizeof air_base);
	return true;
}

void radio_m17_end(void)
{
	if (eng == NULL)
		return;
	m17_engine_end(eng);	// the provider closed: no hook runs after this
	stream_stop();		// the callback no longer runs
	m17_voice_end();
	eng = NULL;
}

bool radio_m17_running(void)
{
	return eng != NULL;
}

void radio_m17_retuned(void)
{
	if (eng != NULL)
		m17_engine_retuned(eng);
}

void radio_m17_apply(const channel_t *ch, uint16_t number, const char *radio_name)
{
	m17_engine_cfg_t cfg;

	if (eng == NULL)
		return;
	params(ch, number, radio_name, &run);
	engine_cfg(&run, &cfg);
	m17_engine_set(eng, &cfg);
}

bool radio_m17_in_force(radio_m17_settings_t *out)
{
	if (eng == NULL)
		return false;
	*out = run;
	return true;
}

// The engine's and the voice path's counters into ours; the engine's restart with it, so ours add what moved. The
// air counts as radio_air_count adds them (radio_air_voice.c, built only with NXDN or YSF)
static void count(const m17_engine_view_t *ev)
{
	const m17_call_stats_t *c = &eng->rx.stats;
	const air_session_stats_t *a = &eng->av.session.stats;
	m17_voice_stats_t v;

	stats.air.rx_calls += ev->rx_calls - air_base.rx_calls;
	stats.air.tx_calls += ev->tx_calls - air_base.tx_calls;
	stats.air.rx_chunks += a->rx_chunks - air_base.rx_chunks;
	stats.air.tx_underruns += a->tx_underruns - air_base.tx_underruns;
	stats.air.tx_syms += a->tx_syms - air_base.tx_syms;
	air_base.rx_calls = ev->rx_calls;
	air_base.tx_calls = ev->tx_calls;
	air_base.rx_chunks = a->rx_chunks;
	air_base.tx_underruns = a->tx_underruns;
	air_base.tx_syms = a->tx_syms;
#define ADD(f) stats.f += c->f - call_base.f
	ADD(lsf_ok); ADD(lsf_fail); ADD(lich_ok); ADD(lich_fail); ADD(frames); ADD(fn_gaps); ADD(fec_bits);
	ADD(auth_frames); ADD(eot); ADD(packets); ADD(held);
#undef ADD
	call_base = *c;
	m17_voice_stats(&v);
	stats.c2_frames += v.rx_frames - voice_base.rx_frames;
	stats.c2_concealed += v.rx_concealed - voice_base.rx_concealed;
	stats.c2_underruns += v.rx_underruns - voice_base.rx_underruns;
	stats.c2_overflows += v.rx_overflows - voice_base.rx_overflows;
	stats.tx_encoded += v.tx_encoded - voice_base.tx_encoded;
	voice_base = v;
}

bool radio_m17_poll(radio_m17_view_t *v)
{
	m17_engine_view_t ev;
	radio_m17_view_t n;
	const radio_m17_settings_t *p = eng != NULL ? &run : &set;

	if (eng == NULL) {
		n = *v;		// the last call heard stays
		n.flags = 0;
		n.tx = AIR_TX_IDLE;
	} else {
		if (!streaming && LOAD(calls) != 0)
			stream_start();
		m17_engine_view(eng, &ev);
		count(&ev);
		memset(&n, 0, sizeof n);
		n.flags = RADIO_M17_ON;
		if (ev.rx_active) {
			n.flags |= RADIO_M17_RX;
			if (ev.rx_played)
				n.flags |= RADIO_M17_PLAYED;
		}
		if (ev.rx.late)
			n.flags |= RADIO_M17_LATE;
		if (no_call)
			n.flags |= RADIO_M17_NO_CALL;
		n.tx = ev.tx;
		n.deny = ev.deny;
		n.can = ev.rx.can;
		n.type = ev.rx.type;
		if (ev.rx.src != 0 || ev.rx.dst != 0) {
			m17_addr_decode(ev.rx.src, n.src);
			m17_addr_decode(ev.rx.dst, n.dst);
		}
		memcpy(n.text, ev.rx.text, sizeof n.text);
		n.text[RADIO_M17_TEXT_MAX] = '\0';
	}
	n.flags &= (uint8_t)~RADIO_M17_ANY;
	if (p->hear_any)
		n.flags |= RADIO_M17_ANY;
	n.run_can = p->can;
	memcpy(n.run_dst, p->dest, sizeof n.run_dst);
	n.fit = (uint16_t)radio_m17_fit();
	bool changed = memcmp(&n, v, sizeof n) != 0;
	*v = n;
	return changed;
}

// ── Transmit: none yet (ADR 006 step 4). PTT is refused before anything is queued ──
static bool m17_no_tx(void) { return true; }
static bool m17_tx_prepare(void) { return false; }
static bool m17_tx_go(void) { return false; }
static void m17_tx_nop(void) { }
static uint8_t m17_tx_state(void) { return (uint8_t)AIR_TX_IDLE; }

const radio_air_ops_t radio_m17_air = {
	radio_m17_running, m17_no_tx, m17_tx_prepare, m17_tx_go, m17_tx_nop, m17_tx_nop, m17_tx_state, m17_tx_nop,
};

void radio_m17_stats(radio_m17_stats_t *out)
{
	*out = stats;
}

void radio_m17_reset_stats(void)
{
	memset(&stats, 0, sizeof stats);
}

#endif	// CONFIG_M17
