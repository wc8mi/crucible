// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// The m17 section: M17 on the channel (docs/spec/m17.md 6, core/radio/radio_m17.h): the call heard, the settings in
// force, what the board's provider gives M17, the engine's and the voice path's counters; then the Codec 2 benchmark's
// keys (debuglink_m17_bench.c). "m17.built=0" without CONFIG_M17
#include "config.h"
#include "debuglink/debuglink.h"
#if CONFIG_M17
#include "radio/radio_m17.h"
#include "m17/m17_call.h"
#include "m17/m17_engine.h"
#endif

#if CONFIG_M17
static const char *deny_key(uint8_t d)
{
	static const char *const names[] = { "none", "can", "enc", "c2_1600", "data" };
	return d < sizeof names / sizeof names[0] ? names[d] : "?";
}
#endif

void debuglink_view_m17(const radio_view_t *v, dl_text_t *t)
{
#if CONFIG_M17
	const radio_m17_view_t *m = &v->m17;
	radio_m17_settings_t s;
	radio_m17_stats_t c;

	// The call heard now, or the last one (on=0: the engine is not running on this channel)
	dl_text(t, "m17.built=1\nm17.on=%u\nm17.fit=%u\n", (m->flags & RADIO_M17_ON) != 0, m->fit);
	dl_text(t, "m17.rx=%u\nm17.rx.played=%u\nm17.rx.late=%u\nm17.rx.src=%s\nm17.rx.dst=%s\nm17.rx.can=%u\n",
		(m->flags & RADIO_M17_RX) != 0, (m->flags & RADIO_M17_PLAYED) != 0, (m->flags & RADIO_M17_LATE) != 0,
		m->src, m->dst, m->can);
	dl_text(t, "m17.rx.enc=%u\nm17.rx.dtype=%u\nm17.rx.deny=%s\nm17.rx.text=%s\n", m17_type_enc(m->type),
		(unsigned)(m17_type_data(m->type) >> 1), deny_key(m->deny), m->text);
	// Transmit (ADR 006 step 4): refused for now; no_call: no callsign to send as
	dl_text(t, "m17.tx=%u\nm17.tx.deny=%u\nm17.no_call=%u\n", m->tx, v->tx_deny, (m->flags & RADIO_M17_NO_CALL) != 0);
	// The settings in force (a stored channel's own while the engine runs, else the VFO's), then the VFO's
	dl_text(t, "m17.run.can=%u\nm17.run.any=%u\nm17.run.dst=%s\n", m->run_can, (m->flags & RADIO_M17_ANY) != 0,
		m->run_dst[0] != '\0' ? m->run_dst : "ALL");
	radio_m17_settings(&s);
	dl_text(t, "m17.set.can=%u\nm17.set.any=%u\nm17.set.dst=%s\n", s.can, s.hear_any,
		s.dest[0] != '\0' ? s.dest : "ALL");
	radio_m17_stats(&c);
	dl_text(t, "m17.begins=%u\nm17.begin_fails=%u\nm17.rx_calls=%u\nm17.lsf_ok=%u\nm17.lsf_fail=%u\n",
		(unsigned)c.begins, (unsigned)c.begin_fails, (unsigned)c.air.rx_calls, (unsigned)c.lsf_ok,
		(unsigned)c.lsf_fail);
	dl_text(t, "m17.lich_ok=%u\nm17.lich_fail=%u\nm17.frames=%u\nm17.fn_gaps=%u\nm17.fec_bits=%u\n",
		(unsigned)c.lich_ok, (unsigned)c.lich_fail, (unsigned)c.frames, (unsigned)c.fn_gaps, (unsigned)c.fec_bits);
	dl_text(t, "m17.auth_frames=%u\nm17.eot=%u\nm17.packets=%u\nm17.held=%u\nm17.rx_chunks=%u\n",
		(unsigned)c.auth_frames, (unsigned)c.eot, (unsigned)c.packets, (unsigned)c.held,
		(unsigned)c.air.rx_chunks);
	dl_text(t, "m17.tx_underruns=%u\nm17.tx_syms=%u\n", (unsigned)c.air.tx_underruns, (unsigned)c.air.tx_syms);
	// The voice path: Codec 2 frames played, blocks concealed (a repeat or silence), blocks with nothing queued,
	// frames dropped with the queue full, mic blocks encoded
	dl_text(t, "m17.c2_frames=%u\nm17.c2_concealed=%u\nm17.c2_underruns=%u\nm17.c2_overflows=%u\nm17.tx_encoded=%u\n",
		(unsigned)c.c2_frames, (unsigned)c.c2_concealed, (unsigned)c.c2_underruns, (unsigned)c.c2_overflows,
		(unsigned)c.tx_encoded);
	debuglink_view_m17_bench(t);
#else
	(void)v;
	dl_text(t, "m17.built=0\n");
#endif
}

void debuglink_m17_reset(void)
{
#if CONFIG_M17
	radio_m17_reset_stats();
#endif
}
