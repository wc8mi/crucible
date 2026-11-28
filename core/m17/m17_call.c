// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// The M17 call layer, receive (m17_call.h; docs/spec/m17.md 6)
#include <string.h>

#include "m17_call.h"
#include "../../hal/hal_radio.h"

#define COAST_FRAMES	3	// stream frames in a row taken without a LICH inside a call
// A stream frame with no call in hand and no LSF chunk set started is taken only when its code corrected at most this
// many of its 272 bits: four Golay words alone pass a tenth of noise frames, Viterbi on noise corrects a quarter
#define STREAM_ERRS_IDLE	24
// LSF chunks this old no longer make a frame with a bad LICH or a high error count welcome (the stream is gone)
#define ASM_FRESH_US		480000u
#define LSF_SAME_BYTES		M17_LSF_META	// DST, SRC and TYPE: the same call; META may change inside it (text)

static int8_t clamp8(int v) { return (int8_t)(v > 127 ? 127 : v); }

// ── META text (spec, Application Layer: Text Data): a control byte, then 13 characters. Its high nibble has a bit for
// each block the sender uses, its low nibble the bit of this block ──
bool m17_meta_text_add(m17_call_t *c, const uint8_t meta[M17_META_BYTES])
{
	uint8_t want = (uint8_t)(meta[0] >> 4), blk = (uint8_t)(meta[0] & 0x0Fu);
	unsigned k = 0;

	if (blk == 0 || (blk & (blk - 1u)) != 0)
		return false;
	while (!(blk & (1u << k)))
		k++;
	if (c->text_want != want) {
		c->text_have = 0;	// the sender's text changed
		c->text_want = want;
	}
	memcpy(&c->text_raw[k * M17_TEXT_BLOCK], &meta[1], M17_TEXT_BLOCK);
	c->text_have |= blk;
	// As far as the blocks in hand run from the first, up to a NUL
	size_t n = 0;
	for (unsigned b = 0; b < M17_TEXT_BLOCKS && (c->text_have & (1u << b)); b++)
		n += M17_TEXT_BLOCK;
	size_t i = 0;
	while (i < n && c->text_raw[i] != 0)
		i++;
	memcpy(c->text, c->text_raw, i);
	c->text[i] = 0;
	return true;
}

// ── The call ──
// The call in hand ends. Its LSF stays as the chunk set, all in: an end bit read wrong in a fade, or a fade that
// cost the lock, leaves the stream on air, and its next LICH chunk takes the call up again at once. A chunk from
// another stream starts the set over
static void call_end(m17_call_rx_t *c)
{
	c->coasted = 0;
	if (!c->active)
		return;
	c->active = false;
	memcpy(c->asm_.lsf, c->lsf, M17_LSF_BYTES);
	c->asm_.have = (1u << M17_LICH_CHUNKS) - 1u;
	c->asm_t = c->t_us;
	if (c->sink->on_end)
		c->sink->on_end(c->ctx);
}

static bool meta_text(m17_call_t *call, const uint8_t lsf[M17_LSF_BYTES])
{
	uint16_t t = call->type;

	if (m17_type_enc(t) != 0 || m17_type_sub(t) != M17_META_TEXT)
		return false;
	return m17_meta_text_add(call, &lsf[M17_LSF_META]);
}

// An LSF in hand, from its frame or from LICH chunks: the call in hand goes on (META may have changed), or another
// begins
static void call_lsf(m17_call_rx_t *c, const uint8_t lsf[M17_LSF_BYTES], bool from_lich)
{
	m17_call_t *call = &c->call;

	if (c->active && memcmp(c->lsf, lsf, LSF_SAME_BYTES) == 0) {
		bool changed = memcmp(&c->lsf[M17_LSF_META], &lsf[M17_LSF_META], M17_META_BYTES) != 0;
		memcpy(c->lsf, lsf, M17_LSF_BYTES);
		memcpy(call->meta, &lsf[M17_LSF_META], M17_META_BYTES);
		if (changed && meta_text(call, lsf) && c->sink->on_update)
			c->sink->on_update(call, c->ctx);
		return;
	}
	call_end(c);	// another call in hand: it ended
	memset(call, 0, sizeof *call);
	m17_lsf_asm_reset(&c->asm_);	// from here it gathers this call's LSF again (META changes)
	memcpy(c->lsf, lsf, M17_LSF_BYTES);
	call->dst = m17_addr_get(&lsf[M17_LSF_DST]);
	call->src = m17_addr_get(&lsf[M17_LSF_SRC]);
	call->type = m17_be16(&lsf[M17_LSF_TYPE]);
	call->can = m17_type_can(call->type);
	call->late = from_lich;
	memcpy(call->meta, &lsf[M17_LSF_META], M17_META_BYTES);
	meta_text(call, lsf);
	c->active = true;
	c->started_lsf = !from_lich;
	c->fn_known = false;
	c->coasted = 0;
	c->stats.calls++;
	if (c->sink->on_start)
		c->sink->on_start(call, c->ctx);
}

// ── The framer's hooks ──
// The frame's kind: its sync, or with none seen a stream frame (the only kind that follows another in a call)
static uint8_t kind(const air_frame_t *f)
{
	return (f->flags & AIR_FRAME_NO_SYNC) ? (uint8_t)M17_SID_STREAM : f->sync_id;
}

static uint16_t rx_head(const air_frame_t *f, void *ctx)
{
	m17_call_rx_t *c = ctx;

	bool fresh = c->asm_.have && f->t_us - c->asm_t < ASM_FRESH_US;

	c->t_us = f->t_us;
	c->cur_lsf_errs = c->cur_lich_errs = -1;
	c->cur_str_errs = -1;
	switch (kind(f)) {
	case M17_SID_LSF: {
		int e = m17_lsf_get(f->hard, f->soft, &c->work, c->cur_lsf);
		c->cur_lsf_errs = e < 0 ? -1 : clamp8(e);
		if (e < 0) {
			c->stats.lsf_fail++;
			return 0;
		}
		return M17_FRAME_SYMS;
	}
	case M17_SID_STREAM: {
		int le = m17_lich_get(f->hard, f->soft, c->cur_lich);
		if (le >= 0 && m17_lich_count(c->cur_lich) >= M17_LICH_CHUNKS)
			le = -1;
		c->cur_lich_errs = (int8_t)le;
		c->cur_str_errs = (int16_t)m17_stream_get(f->hard, f->soft, &c->work, &c->cur_fn, c->cur_payload);
		if (le >= 0) {
			if (c->active || fresh || c->cur_str_errs <= STREAM_ERRS_IDLE)
				return M17_FRAME_SYMS;
			return 0;
		}
		// A fade inside a call, or inside a stream whose LSF is coming together: placed by counting
		if ((c->active || fresh) && c->coasted < COAST_FRAMES)
			return M17_FRAME_SYMS;
		return 0;
	}
	case M17_SID_EOT:
		return c->active ? M17_FRAME_SYMS : 0;
	default:	// packet frames: not decoded (voice only)
		c->stats.packets++;
		return 0;
	}
}

static void stream_frame(m17_call_rx_t *c)
{
	c->stats.frames++;
	if (c->cur_lich_errs >= 0) {
		c->stats.lich_ok++;
		c->stats.fec_bits += (uint32_t)c->cur_lich_errs;
		c->coasted = 0;
		c->asm_t = c->t_us;
		if (m17_lsf_asm_add(&c->asm_, c->cur_lich)) {
			uint8_t lsf[M17_LSF_BYTES];
			memcpy(lsf, c->asm_.lsf, M17_LSF_BYTES);
			m17_lsf_asm_reset(&c->asm_);
			if ((m17_be16(&lsf[M17_LSF_TYPE]) & M17_TYPE_STREAM) != 0)
				call_lsf(c, lsf, true);
		}
	} else {
		c->stats.lich_fail++;
		c->coasted++;
	}
	if (!c->active) {
		c->stats.held++;
		return;
	}
	if (c->cur_lich_errs < 0 && c->cur_str_errs > STREAM_ERRS_IDLE)
		return;		// placed by counting, and its code says noise: lost (the voice path covers the gap)
	if (c->cur_str_errs > 0)
		c->stats.fec_bits += (uint32_t)c->cur_str_errs;
	uint16_t fn = (uint16_t)(c->cur_fn & M17_FN_MASK);
	if (c->fn_known) {
		uint16_t gap = (uint16_t)((fn - c->fn - 1u) & M17_FN_MASK);
		if (gap < 64)	// a larger jump is a frame decoded wrong, not lost frames
			c->stats.fn_gaps += gap;
	}
	c->fn = fn;
	c->fn_known = true;
	if ((c->call.type & M17_TYPE_SIGNED) && fn >= M17_FN_AUTH) {
		c->stats.auth_frames++;	// the signature: never voice
	} else if (c->sink->on_voice) {
		c->sink->on_voice(c->cur_payload, c->cur_fn, c->ctx);
	}
	if (c->cur_fn & M17_FN_LAST)
		call_end(c);
}

static void rx_frame(const air_frame_t *f, void *ctx)
{
	m17_call_rx_t *c = ctx;

	switch (kind(f)) {
	case M17_SID_LSF:
		c->stats.lsf_ok++;
		c->stats.fec_bits += (uint32_t)c->cur_lsf_errs;
		if ((m17_be16(&c->cur_lsf[M17_LSF_TYPE]) & M17_TYPE_STREAM) == 0) {
			c->stats.packets++;	// a packet transmission: not ours
			call_end(c);
			return;
		}
		call_lsf(c, c->cur_lsf, false);
		break;
	case M17_SID_STREAM:
		stream_frame(c);
		break;
	case M17_SID_EOT:
		c->stats.eot++;
		call_end(c);
		break;
	default:
		break;
	}
}

// The signal is gone: the call in hand ends (its LSF kept as the chunk set, call_end)
static void rx_lost(void *ctx)
{
	call_end(ctx);
}

// Preamble: +3 -3 alternating (0x77 as dibits)
#define PRE	0x77

const air_mode_t m17_air_mode = {
	.name = "M17",
	.profile = {
		.sym_rate = 4800,
		.levels = 4,
		.shape = AIR_SHAPE_RRC,		// m17.md 1: root raised cosine 0.5
		.shape_param = 50,
		.dev_outer_hz = 2400,
		.timing = AIR_TIMING_CONTINUOUS,
		.bw = RADIO_BW_12K5,
		.sync_every_syms = 0,		// no recurring 24-symbol pattern
		.sync_any_syms = M17_FRAME_SYMS,	// a sync opens every frame
		.idle = PRE,
		.rx_opts = 0,
		.nsyncs = 5,
		.sync = {
			// The end of the preamble and the LSF's sync: the frame starts 16 symbols in
			{ .id = M17_SID_LSF, .nsyms = 24, .max_errs = 1, .flags = AIR_SYNC_START_ONLY, .frame_at = 16,
			  .syms = { PRE, PRE, PRE, PRE, 0x55, 0xF7 } },
			{ .id = M17_SID_STREAM, .nsyms = 8, .max_errs = 1, .frame_at = 0, .syms = { 0xFF, 0x5D } },
			{ .id = M17_SID_LSF, .nsyms = 8, .max_errs = 1, .frame_at = 0, .syms = { 0x55, 0xF7 } },
			{ .id = M17_SID_EOT, .nsyms = 8, .max_errs = 0, .frame_at = 0, .syms = { 0x55, 0x5D } },
			{ .id = M17_SID_PACKET, .nsyms = 8, .max_errs = 0, .frame_at = 0, .syms = { 0x75, 0xFF } },
		},
	},
	.frame_syms = M17_FRAME_SYMS,
	.head_syms = M17_FRAME_SYMS,		// the LICH and the code reach over the whole frame (interleaved)
	.lost_frames = 4,			// 160 ms without a sync
	.head = rx_head,
	.rx_frame = rx_frame,
	.rx_lost = rx_lost,
};

void m17_call_rx_init(m17_call_rx_t *c, const m17_call_sink_t *sink, void *ctx)
{
	memset(c, 0, sizeof *c);
	c->sink = sink;
	c->ctx = ctx;
	c->cur_lsf_errs = c->cur_lich_errs = -1;
	c->cur_str_errs = -1;
}

void m17_call_rx_reset(m17_call_rx_t *c)
{
	call_end(c);
	m17_lsf_asm_reset(&c->asm_);	// another frequency: nothing of the stream is there
}
