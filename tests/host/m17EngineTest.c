// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// M17 receive: the call layer (core/m17/m17_call.c) and the engine (m17_engine.c) through the framer and the air
// interface session, on a made-up provider fed whole transmissions (preamble, LSF, stream frames, EoT) a chunk at a
// time, with noise between them. A call heard with its fields and every voice frame in order; late entry from LICH;
// the CAN squelch and hear every CAN; FN wrap, a lost frame and the end bit; EoT; a signed stream's five trailing
// frames not played; encrypted, Codec 2 1600 and packet transmissions not played; META text; soft noise.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m17/m17_engine.h"
#include "air/air_syms.h"

static int failures, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t rng_state = 0x17171717;
static uint32_t rng(void)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return rng_state;
}
static double gauss(void)
{
	double u = ((rng() >> 8) + 1.0) / 16777217.0, v = ((rng() >> 8) + 1.0) / 16777217.0;

	return sqrt(-2.0 * log(u)) * cos(6.283185307179586 * v);
}

// ── The provider: receive only. A chunk is 30 ms ──
#define CHUNK	144
static air_event_cb_t prov_cb;
static void *prov_ctx;
static uint32_t now_us;
static double noise_sd;		// soft noise, in units of the outer symbol's level

static uint32_t p_fit(const air_profile_t *pr)
{
	return pr->sym_rate == 4800 ? AIR_FIT_RX | AIR_FIT_SOFT : 0;
}
static bool p_open(const air_profile_t *pr, air_event_cb_t cb, void *ctx) { (void)pr; prov_cb = cb; prov_ctx = ctx; return true; }
static void p_close(void) { prov_cb = NULL; }
static void p_restart(void) { }
static bool p_begin(void) { return false; }
static bool p_queue(const air_tx_chunk_t *c) { (void)c; return false; }
static uint8_t p_space(void) { return 0; }
static void p_end(void) { }
static void p_abort(void) { }
static uint32_t p_now(void) { return now_us; }
static hal_air_t air = { p_fit, p_open, p_close, p_restart, p_begin, p_queue, p_space, p_end, p_abort, p_now,
			 NULL, NULL, NULL, NULL, 72, 4, 60, 100 };

// The symbols on air, a whole transmission or more
#define AIR_MAX	(200 * M17_FRAME_SYMS)
static uint8_t on_air[AIR_MAX / 4];
static uint32_t nair;

static void put_syms(const uint8_t *syms, uint16_t n)
{
	for (uint16_t i = 0; i < n && nair < AIR_MAX; i++)
		air_sym_put(on_air, (uint16_t)nair++, air_sym_get(syms, i));
}
static void put_noise(uint16_t n)
{
	for (uint16_t i = 0; i < n && nair < AIR_MAX; i++, nair++)
		air_sym_put(on_air, (uint16_t)nair, (uint8_t)(rng() & 3));
}

// Everything on air to the receiver, a chunk at a time
static void play_air(void)
{
	for (uint32_t at = 0; at < nair; at += CHUNK) {
		uint8_t hard[CHUNK / 4];
		int8_t soft[CHUNK];
		uint16_t n = (uint16_t)(nair - at < CHUNK ? nair - at : CHUNK);

		for (uint16_t i = 0; i < n; i++) {
			uint8_t d = air_sym_get(on_air, (uint16_t)(at + i));
			double v = air_sym_level(d) / 3.0 + noise_sd * gauss();
			int s = (int)lround(v * AIR_SOFT_OUTER);

			soft[i] = (int8_t)(s > 127 ? 127 : s < -127 ? -127 : s);
			air_sym_put(hard, i, d);
		}
		air_chunk_t c = { .t_us = now_us, .nsyms = n, .sync_at = -1, .hard = hard, .soft = soft };
		air_event_t e = { .kind = AIR_EV_RX, .t_us = now_us, .chunk = &c };
		now_us += 30000;
		if (prov_cb)
			prov_cb(&e, prov_ctx);
	}
	nair = 0;
}

// ── A transmission ──
typedef struct {
	const char *src, *dst;
	uint8_t can;
	uint16_t type_extra;		// TYPE bits beyond stream, voice and the CAN; or a whole TYPE with raw_type
	bool raw_type;
	uint16_t fn0;			// the first FN
	uint16_t frames;
	bool preamble_lsf;		// a preamble and an LSF ahead; else stream frames only (a late entry)
	uint16_t skip;			// stream frames before this one are not on air (joined late)
	int16_t lose;			// this stream frame is noise on air; -1 none
	bool eot;
	bool end_bit;			// the last frame carries the end bit
	const char *text;		// META text, up to 52 characters
} tx_t;

static uint8_t voice_byte(uint16_t fn, int i) { return (uint8_t)(fn * 7u + (unsigned)i * 13u + 1u); }

static void lsf_make(const tx_t *t, uint8_t lsf[M17_LSF_BYTES], uint8_t block)
{
	uint64_t a;
	uint16_t type = t->raw_type ? t->type_extra :
			(uint16_t)(M17_TYPE_STREAM | M17_TYPE_DATA_VOICE | (t->can << M17_TYPE_CAN_SHIFT) | t->type_extra);

	memset(lsf, 0, M17_LSF_BYTES);
	m17_addr_dest(t->dst, strlen(t->dst), &a);
	m17_addr_put(&lsf[M17_LSF_DST], a);
	m17_addr_encode(t->src, strlen(t->src), &a);
	m17_addr_put(&lsf[M17_LSF_SRC], a);
	lsf[M17_LSF_TYPE] = (uint8_t)(type >> 8);
	lsf[M17_LSF_TYPE + 1] = (uint8_t)type;
	if (t->text) {
		size_t len = strlen(t->text), nb = (len + M17_TEXT_BLOCK - 1) / M17_TEXT_BLOCK;
		uint8_t *m = &lsf[M17_LSF_META];

		if (nb == 0)
			nb = 1;
		block = (uint8_t)(block % nb);
		m[0] = (uint8_t)(((1u << nb) - 1u) << 4 | (1u << block));
		for (size_t i = 0; i < M17_TEXT_BLOCK; i++) {
			size_t at = block * M17_TEXT_BLOCK + i;
			m[1 + i] = (uint8_t)(at < len ? t->text[at] : ' ');
		}
	}
	m17_lsf_crc_set(lsf);
}

static void transmit(const tx_t *t)
{
	uint8_t frame[M17_FRAME_BYTES], lsf[M17_LSF_BYTES];

	put_noise((uint16_t)(400 + rng() % 200));
	lsf_make(t, lsf, 0);
	if (t->preamble_lsf) {
		uint8_t pre[M17_FRAME_BYTES];
		memset(pre, 0x77, sizeof pre);
		put_syms(pre, M17_FRAME_SYMS);
		m17_lsf_put(frame, lsf);
		put_syms(frame, M17_FRAME_SYMS);
	}
	for (uint16_t i = 0; i < t->frames; i++) {
		uint16_t fn = (uint16_t)((t->fn0 + i) & M17_FN_MASK);
		uint8_t payload[M17_STREAM_BYTES], lich[M17_LICH_BYTES];

		if (i < t->skip)
			continue;
		if (t->lose == (int16_t)i) {
			put_noise(M17_FRAME_SYMS);
			continue;
		}
		if (t->text && i % 6 == 0)
			lsf_make(t, lsf, (uint8_t)(i / 6));	// a new text block each LSF cycle
		for (int k = 0; k < M17_STREAM_BYTES; k++)
			payload[k] = voice_byte(fn, k);
		m17_lich_make(lsf, (uint8_t)(i % 6), lich);
		m17_stream_put(frame, lich, (uint16_t)(fn | (t->end_bit && i + 1u == t->frames ? M17_FN_LAST : 0)), payload);
		put_syms(frame, M17_FRAME_SYMS);
	}
	if (t->eot) {
		uint8_t eot[M17_FRAME_BYTES];
		for (int k = 0; k < M17_FRAME_BYTES; k += 2) {
			eot[k] = 0x55;
			eot[k + 1] = 0x5D;
		}
		put_syms(eot, M17_FRAME_SYMS);
	}
	put_noise((uint16_t)(600 + rng() % 200));
}

// ── The receiving radio ──
#define VMAX	256
static struct {
	m17_engine_t e;
	uint16_t starts, updates, ends;
	m17_call_t call;
	uint16_t nvoice, firsts;
	uint16_t fn_of[VMAX];		// the FN each frame played was made with (from its first byte)
	bool bad;			// a frame played that was not one sent
} rx;

static void h_start(const m17_call_t *c, void *ctx) { (void)ctx; rx.starts++; rx.call = *c; }
static void h_update(const m17_call_t *c, void *ctx) { (void)ctx; rx.updates++; rx.call = *c; }
static void h_end(void *ctx) { (void)ctx; rx.ends++; }
static void h_voice(const uint8_t p[M17_STREAM_BYTES], bool first, void *ctx)
{
	(void)ctx;
	if (first)
		rx.firsts++;
	// Find the FN it was made with: voice_byte(fn, 0) = 7 fn + 1 (mod 256) has one answer mod 256
	uint16_t fn = 0;
	while ((uint8_t)(fn * 7u + 1u) != p[0])
		fn++;
	for (int k = 0; k < M17_STREAM_BYTES; k++)
		if (p[k] != voice_byte(fn, k))
			rx.bad = true;
	if (rx.nvoice < VMAX)
		rx.fn_of[rx.nvoice++] = fn;
}
static const m17_engine_hooks_t hooks = { h_start, h_update, h_voice, h_end };

static void start(uint8_t can, bool any)
{
	m17_engine_cfg_t cfg = { .can = can, .hear_any = any };

	memset(&rx, 0, sizeof rx);
	CHECK(m17_engine_begin(&rx.e, &air, &cfg, &hooks, NULL), "engine begins");
}

static const m17_call_stats_t *st(void) { return &rx.e.rx.stats; }

static tx_t plain(void)
{
	tx_t t = { .src = "N0CALL", .dst = "", .can = 0, .fn0 = 0, .frames = 30, .preamble_lsf = true, .lose = -1,
		   .eot = true, .end_bit = true };
	return t;
}

// The frames played, in order, are the stream's from `from` on, n of them (FN mod 256 checked)
static bool played_in_order(uint16_t fn0, uint16_t from, uint16_t n, int16_t lost)
{
	uint16_t k = 0;

	for (uint16_t i = from; i < from + n + (lost >= 0 ? 1 : 0); i++) {
		if ((int16_t)i == lost)
			continue;
		if (k >= rx.nvoice || rx.fn_of[k] != (uint8_t)(fn0 + i))
			return false;
		k++;
	}
	return k == rx.nvoice;
}

static void test_call(void)
{
	tx_t t = plain();
	m17_engine_view_t v;
	char s[M17_CALL_TEXT];

	start(0, false);
	t.dst = "#ECHO";
	transmit(&t);
	play_air();
	CHECK(rx.starts == 1 && rx.ends == 1, "one call: %u starts %u ends", rx.starts, rx.ends);
	CHECK(rx.nvoice == 30 && !rx.bad && played_in_order(0, 0, 30, -1), "every frame in order: %u", rx.nvoice);
	CHECK(rx.firsts == 1, "the first frame marked once");
	m17_addr_decode(rx.call.src, s);
	CHECK(strcmp(s, "N0CALL") == 0, "src %s", s);
	m17_addr_decode(rx.call.dst, s);
	CHECK(strcmp(s, "#ECHO") == 0, "dst %s", s);
	CHECK(!rx.call.late, "began at the LSF");
	CHECK(st()->lsf_ok == 1 && st()->frames == 30 && st()->fn_gaps == 0 && st()->eot == 0,
	      "lsf %u frames %u gaps %u eot %u (the end bit ended it first)", st()->lsf_ok, st()->frames, st()->fn_gaps,
	      st()->eot);
	m17_engine_view(&rx.e, &v);
	CHECK(!v.rx_active && v.rx_calls == 1 && v.deny == M17_DENY_NONE, "view after the call");
	m17_engine_end(&rx.e);
}

static void test_late(void)
{
	tx_t t = plain();

	start(0, false);
	t.preamble_lsf = false;
	t.skip = 8;		// joins at frame 8, LICH counter 2
	transmit(&t);
	play_air();
	CHECK(rx.starts == 1 && rx.call.late, "a late call: %u starts, late %d", rx.starts, rx.call.late);
	// LICH 2,3,4,5,0,1 make the LSF at frame 13: played from there
	CHECK(rx.nvoice == 30 - 13 && played_in_order(0, 13, 17, -1) && !rx.bad, "played from frame 13: %u", rx.nvoice);
	// (and after the call, any noise frame that passed for one of its stream's while its LSF was fresh)
	CHECK(st()->held >= 5 && st()->held <= 7, "five held before it: %u", st()->held);
	m17_engine_end(&rx.e);
}

static void test_can(void)
{
	tx_t t = plain();
	m17_engine_view_t v;

	start(3, false);
	t.can = 5;
	transmit(&t);
	play_air();
	m17_engine_view(&rx.e, &v);
	CHECK(rx.starts == 0 && rx.nvoice == 0, "another CAN: not played");
	CHECK(v.rx_calls == 1 && v.deny == M17_DENY_CAN && v.rx.can == 5, "seen, CAN 5, deny %u", v.deny);
	t.can = 3;
	transmit(&t);
	play_air();
	CHECK(rx.starts == 1 && rx.nvoice == 30, "our CAN: played");
	m17_engine_end(&rx.e);

	start(3, true);
	t.can = 9;
	transmit(&t);
	play_air();
	CHECK(rx.starts == 1 && rx.nvoice == 30, "hear every CAN: CAN 9 played");
	m17_engine_end(&rx.e);

	// The CAN changed inside a call: judged again at the next frame
	start(3, false);
	t.can = 7;
	t.frames = 40;
	t.eot = t.end_bit = true;
	transmit(&t);
	uint32_t half = (nair / 2) & ~3u;	// a whole byte of symbols
	uint32_t keep = nair;
	nair = half;
	play_air();
	CHECK(rx.nvoice == 0, "CAN 7 on a CAN 3 channel");
	m17_engine_cfg_t cfg = { .can = 7, .hear_any = false };
	m17_engine_set(&rx.e, &cfg);
	// The rest of the transmission
	memmove(on_air, on_air + half / 4, (keep - half + 3) / 4);
	nair = keep - half;
	play_air();
	CHECK(rx.starts == 1 && rx.nvoice > 5 && rx.firsts == 1 && !rx.bad, "played after the change: %u", rx.nvoice);
	m17_engine_end(&rx.e);
}

static void test_fn(void)
{
	tx_t t = plain();

	start(0, false);
	t.fn0 = 0x7FF0;		// wraps to 0 inside the call
	transmit(&t);
	play_air();
	CHECK(rx.nvoice == 30 && st()->fn_gaps == 0 && !rx.bad, "FN wraps: %u frames, %u gaps", rx.nvoice, st()->fn_gaps);
	m17_engine_end(&rx.e);

	start(0, false);
	t = plain();
	t.lose = 12;
	transmit(&t);
	play_air();
	CHECK(rx.starts == 1 && rx.ends == 1, "a lost frame: still one call (%u, %u)", rx.starts, rx.ends);
	CHECK(rx.nvoice == 29 && played_in_order(0, 0, 29, 12) && st()->fn_gaps == 1 && !rx.bad,
	      "29 frames played, one gap: %u, %u", rx.nvoice, st()->fn_gaps);
	m17_engine_end(&rx.e);
}

static void test_eot(void)
{
	tx_t t = plain();

	start(0, false);
	t.end_bit = false;	// a sender that marks the end only with EoT
	transmit(&t);
	play_air();
	CHECK(rx.starts == 1 && rx.ends == 1 && rx.nvoice == 30 && st()->eot == 1, "EoT ends the call: %u", st()->eot);
	m17_engine_end(&rx.e);

	start(0, false);
	t.eot = false;		// neither: the signal is lost
	transmit(&t);
	play_air();
	CHECK(rx.starts == 1 && rx.ends == 1 && rx.nvoice == 30, "the lost signal ends it");
	m17_engine_end(&rx.e);
}

static void test_signed(void)
{
	tx_t t = plain();

	start(0, false);
	t.type_extra = M17_TYPE_SIGNED;
	t.fn0 = (uint16_t)(M17_FN_AUTH - 20);	// 20 voice frames, then the authentication frame and four of signature
	t.frames = 25;
	transmit(&t);
	play_air();
	CHECK(rx.nvoice == 20 && st()->auth_frames == 5 && rx.ends == 1 && !rx.bad, "signed: 20 played, %u held",
	      st()->auth_frames);
	m17_engine_end(&rx.e);
}

static void test_denied(void)
{
	tx_t t = plain();
	m17_engine_view_t v;

	start(0, false);
	t.type_extra = 1u << 3;		// encryption type 1 (scrambler)
	transmit(&t);
	play_air();
	m17_engine_view(&rx.e, &v);
	CHECK(rx.nvoice == 0 && v.deny == M17_DENY_ENC && v.rx_calls == 1, "encrypted: seen, not played (%u)", v.deny);

	t = plain();
	t.raw_type = true;
	t.type_extra = M17_TYPE_STREAM | M17_TYPE_DATA_VD;
	transmit(&t);
	play_air();
	m17_engine_view(&rx.e, &v);
	CHECK(rx.nvoice == 0 && v.deny == M17_DENY_1600 && v.rx_calls == 2, "Codec 2 1600: not played (%u)", v.deny);

	// A packet transmission: its LSF says packet; its frames are not ours
	t = plain();
	t.raw_type = true;
	t.type_extra = M17_TYPE_DATA_VOICE;	// packet mode (bit 0 clear)
	t.frames = 0;
	t.eot = true;
	transmit(&t);
	play_air();
	m17_engine_view(&rx.e, &v);
	CHECK(rx.nvoice == 0 && v.rx_calls == 2 && st()->packets >= 1, "packet: no call (%u packets)", st()->packets);
	m17_engine_end(&rx.e);
}

static void test_text(void)
{
	tx_t t = plain();

	start(0, false);
	t.text = "Crucible M17 receive test, four blocks of text here!";	// 52 characters
	t.frames = 30;
	t.eot = true;
	t.end_bit = false;
	transmit(&t);
	play_air();
	CHECK(strcmp(rx.call.text, t.text) == 0, "META text: \"%s\"", rx.call.text);
	CHECK(rx.updates >= 3, "updated as blocks came: %u", rx.updates);
	m17_engine_end(&rx.e);
}

static void test_noise(void)
{
	tx_t t = plain();
	double sds[] = { 0.15, 0.20, 0.25, 0.35 };

	for (unsigned k = 0; k < sizeof sds / sizeof sds[0]; k++) {
		start(0, false);
		noise_sd = sds[k];
		transmit(&t);
		play_air();
		printf("  noise %.2f: %u starts, %u frames played, %u bad, fec bits %u, lich fails %u\n", sds[k], rx.starts,
		       rx.nvoice, rx.bad, st()->fec_bits, st()->lich_fail);
		if (k == 0)
			CHECK(rx.starts == 1 && rx.nvoice == 30 && !rx.bad, "light noise: all played");
		m17_engine_end(&rx.e);
	}
	noise_sd = 0;

	// Noise alone, a long while: no call
	start(0, true);
	for (int i = 0; i < 50; i++) {
		put_noise(10000);
		play_air();
	}
	CHECK(rx.starts == 0 && st()->calls == 0, "noise: no call (%u)", st()->calls);
	printf("  noise alone, 500,000 symbols: %u locks, %u rejects, %u stream frames, %u LSF fails\n",
	       rx.e.av.session.fr.stats.locks, rx.e.av.session.fr.stats.rejects, st()->frames, st()->lsf_fail);
	m17_engine_end(&rx.e);
}

int main(void)
{
	printf("m17_engine_t: %zu bytes (m17_call_rx_t %zu, air_voice_t %zu)\n", sizeof(m17_engine_t),
	       sizeof(m17_call_rx_t), sizeof(air_voice_t));
	test_call();
	test_late();
	test_can();
	test_fn();
	test_eot();
	test_signed();
	test_denied();
	test_text();
	test_noise();
	printf("m17EngineTest: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
