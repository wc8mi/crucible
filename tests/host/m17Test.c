// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// M17 frames and addresses (core/m17): the CRC against the specification's vectors, the base-40 addresses against
// its example and libm17's (tests/vectors/m17-frames.txt), the LSF and the stream frame against libm17's frames bit for
// bit, decoded back from hard and soft symbols, with errors corrected, and the LSF put together from LICH chunks.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m17/m17.h"
#include "air/air_syms.h"
#include "hal_air.h"

static int failures, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t rng_state = 0x5EED17;
static uint32_t rng(void)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return rng_state;
}

static void hex(const char *s, uint8_t *out, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		unsigned v = 0;
		sscanf(s + 2 * i, "%2x", &v);
		out[i] = (uint8_t)v;
	}
}

// The frame's symbols as soft values at the nominal levels (AIR_SOFT_OUTER at +3), plus noise up to +-noise
static void soften(const uint8_t *frame, int8_t *soft, int noise)
{
	static const int lvl[4] = { AIR_SOFT_OUTER / 3, AIR_SOFT_OUTER, -AIR_SOFT_OUTER / 3, -AIR_SOFT_OUTER };

	for (uint16_t k = 0; k < M17_FRAME_SYMS; k++) {
		int v = lvl[air_sym_get(frame, k)] + (noise ? (int)(rng() % (2u * noise + 1u)) - noise : 0);
		soft[k] = (int8_t)(v > 127 ? 127 : v < -127 ? -127 : v);
	}
}

// Flip a payload symbol's sign (the receiver reads the wrong high bit)
static void flip_sign(uint8_t *frame, uint16_t k)
{
	air_sym_put(frame, k, (uint8_t)(air_sym_get(frame, k) ^ 2));
}

// ── CRC (spec, Data Link Layer: CRC) ──
static void testCrc(void)
{
	uint8_t all[256], lsf[M17_LSF_BYTES];

	for (int i = 0; i < 256; i++)
		all[i] = (uint8_t)i;
	CHECK(m17_crc((const uint8_t *)"", 0) == 0xFFFF, "empty: %04X", m17_crc((const uint8_t *)"", 0));
	CHECK(m17_crc((const uint8_t *)"A", 1) == 0x206E, "A: %04X", m17_crc((const uint8_t *)"A", 1));
	CHECK(m17_crc((const uint8_t *)"123456789", 9) == 0x772B, "123456789: %04X",
	      m17_crc((const uint8_t *)"123456789", 9));
	CHECK(m17_crc(all, 256) == 0x1C31, "0x00..0xFF: %04X", m17_crc(all, 256));
	for (int i = 0; i < M17_LSF_BYTES; i++)
		lsf[i] = (uint8_t)rng();
	m17_lsf_crc_set(lsf);
	CHECK(m17_lsf_crc_ok(lsf), "an LSF with its CRC set checks");
	lsf[3] ^= 0x10;
	CHECK(!m17_lsf_crc_ok(lsf), "a changed LSF fails its CRC");
}

// ── Addresses (spec, appendix A: Address Encoding) ──
static bool enc(const char *s, uint64_t *a) { return m17_addr_encode(s, strlen(s), a); }

static void testAddr(void)
{
	uint64_t a = 0;
	char t[M17_CALL_TEXT];

	CHECK(enc("AB1CD", &a) && a == 0x9FDD51u, "AB1CD (the spec's example): %llx", (unsigned long long)a);
	CHECK(enc("ab1cd", &a) && a == 0x9FDD51u, "lower case reads as upper");
	CHECK(enc("AB1CD    ", &a) && a == 0x9FDD51u, "a space-padded field encodes as its text");
	CHECK(m17_addr_encode("AB1CDXXXX", 5, &a) && a == 0x9FDD51u, "len bounds the text");
	CHECK(enc(".........", &a) && a == M17_ADDR_HASH - 1u, "the largest callsign is 40^9 - 1");
	CHECK(!enc("", &a) && !enc("   ", &a), "nothing is not an address");
	CHECK(!enc("ABCDEFGHIJ", &a), "ten characters are too many");
	CHECK(!enc("AB_CD", &a) && !enc("AB*", &a), "a character outside the alphabet is refused");
	CHECK(enc("@ALL", &a) && a == M17_ADDR_BROADCAST, "@ALL is broadcast");
	CHECK(enc("#ECHO", &a) && a == M17_ADDR_HASH + 0x0ED87Du, "#ECHO in the hash range");
	CHECK(!enc("#ABCDEFGHI", &a), "# and nine characters are too many");
	CHECK(m17_addr_decode(a, t) == 5 && !strcmp(t, "#ECHO"), "#ECHO back: '%s'", t);
	CHECK(m17_addr_decode(M17_ADDR_BROADCAST, t) == 4 && !strcmp(t, "@ALL"), "broadcast as @ALL");
	CHECK(m17_addr_decode(0, t) == 0 && t[0] == '\0', "0 as nothing");
	CHECK(m17_addr_decode(M17_ADDR_HASH_END, t) == 0 && m17_addr_decode(M17_ADDR_BROADCAST - 1u, t) == 0,
	      "the reserved range as nothing");

	CHECK(m17_addr_dest("", 0, &a) && a == M17_ADDR_BROADCAST, "a blank destination is broadcast");
	CHECK(m17_addr_dest("         ", 9, &a) && a == M17_ADDR_BROADCAST, "a padded blank destination is broadcast");
	CHECK(m17_addr_dest("ALL      ", 9, &a) && a == M17_ADDR_BROADCAST, "ALL is broadcast");
	CHECK(m17_addr_dest("all", 3, &a) && a == M17_ADDR_BROADCAST, "all is broadcast");
	CHECK(m17_addr_dest("ALLX", 4, &a) && a != M17_ADDR_BROADCAST, "ALLX is a callsign");
	CHECK(m17_addr_dest("W1AW     ", 9, &a) && a == 0x1680B7u, "a callsign destination");
	CHECK(!m17_addr_dest("W1AW!", 5, &a), "a bad destination is refused");

	uint8_t p[M17_ADDR_BYTES];
	m17_addr_put(p, 0x0286FC99B093ull);
	CHECK(p[0] == 0x02 && p[5] == 0x93 && m17_addr_get(p) == 0x0286FC99B093ull, "big-endian bytes");

	// Any callsign value decodes to text that encodes to it again
	for (int i = 0; i < 2000; i++) {
		uint64_t v = (((uint64_t)rng() << 32) | rng()) % (M17_ADDR_HASH - 1u) + 1u, back = 0;
		m17_addr_decode(v, t);
		CHECK(enc(t, &back) && back == v, "round trip %llx: '%s'", (unsigned long long)v, t);
		if (back != v)
			break;
	}
}

// ── Frames ──
static void testLsfFrame(const uint8_t lsf[M17_LSF_BYTES], const uint8_t want[M17_FRAME_BYTES], unsigned n)
{
	uint8_t frame[M17_FRAME_BYTES], got[M17_LSF_BYTES];
	int8_t soft[M17_FRAME_SYMS];
	m17_fec_work_t w;

	m17_lsf_put(frame, lsf);
	CHECK(!memcmp(frame, want, sizeof frame), "lsf %u: coded as libm17", n);
	CHECK(m17_lsf_get(want, NULL, &w, got) == 0 && !memcmp(got, lsf, sizeof got), "lsf %u: decoded (hard)", n);
	soften(want, soft, 0);
	CHECK(m17_lsf_get(want, soft, &w, got) == 0 && !memcmp(got, lsf, sizeof got), "lsf %u: decoded (soft)", n);

	// Errors spread over the frame: the code corrects them
	memcpy(frame, want, sizeof frame);
	for (uint16_t k = M17_SYNC_SYMS + 3; k < M17_FRAME_SYMS; k += 23)
		flip_sign(frame, k);
	int fixed = m17_lsf_get(frame, NULL, &w, got);
	CHECK(fixed > 0 && !memcmp(got, lsf, sizeof got), "lsf %u: 8 symbol errors corrected (%d bits)", n, fixed);
	// Noise across the frame, as a weak signal makes it: symbols near a boundary read as their neighbor. The soft
	// values say which bits to doubt
	unsigned wrong = 0;
	soften(want, soft, 36);
	for (uint16_t k = M17_SYNC_SYMS; k < M17_FRAME_SYMS; k++) {
		static const int lvl[4] = { AIR_SOFT_OUTER / 3, AIR_SOFT_OUTER, -AIR_SOFT_OUTER / 3, -AIR_SOFT_OUTER };
		int v = soft[k], best = 0;
		for (int dbit = 1; dbit < 4; dbit++)
			if (abs(v - lvl[dbit]) < abs(v - lvl[best]))
				best = dbit;
		wrong += best != air_sym_get(want, k);
	}
	fixed = m17_lsf_get(want, soft, &w, got);
	CHECK(wrong > 0 && fixed >= 0 && !memcmp(got, lsf, sizeof got), "lsf %u: soft, %u symbols misread (%d bits)", n,
	      wrong, fixed);

	// Past the code: the CRC says so
	memcpy(frame, want, sizeof frame);
	for (uint16_t k = 40; k < 80; k++)
		flip_sign(frame, k);
	CHECK(m17_lsf_get(frame, NULL, &w, got) == -1, "lsf %u: a burst past the code fails the CRC", n);
}

static void testStreamFrame(const uint8_t lsf[M17_LSF_BYTES], unsigned cnt, uint16_t fn,
			    const uint8_t pl[M17_STREAM_BYTES], const uint8_t want[M17_FRAME_BYTES], unsigned n)
{
	uint8_t frame[M17_FRAME_BYTES], lich[M17_LICH_BYTES], got_lich[M17_LICH_BYTES], got[M17_STREAM_BYTES];
	int8_t soft[M17_FRAME_SYMS];
	uint16_t got_fn = 0;
	m17_fec_work_t w;

	m17_lich_make(lsf, (uint8_t)cnt, lich);
	CHECK(m17_lich_count(lich) == cnt && !memcmp(lich, &lsf[cnt * 5], 5), "str %u: the LICH chunk", n);
	m17_stream_put(frame, lich, fn, pl);
	CHECK(!memcmp(frame, want, sizeof frame), "str %u: coded as libm17", n);
	CHECK(m17_lich_get(want, NULL, got_lich) == 0 && !memcmp(got_lich, lich, sizeof lich), "str %u: LICH back", n);
	CHECK(m17_stream_get(want, NULL, &w, &got_fn, got) == 0 && got_fn == fn && !memcmp(got, pl, sizeof got),
	      "str %u: FN %04X and payload back (FN %04X)", n, fn, got_fn);
	soften(want, soft, 0);
	CHECK(m17_lich_get(want, soft, got_lich) == 0 && !memcmp(got_lich, lich, sizeof lich), "str %u: soft LICH", n);
	CHECK(m17_stream_get(want, soft, &w, &got_fn, got) == 0 && got_fn == fn && !memcmp(got, pl, sizeof got),
	      "str %u: soft payload", n);

	memcpy(frame, want, sizeof frame);
	for (uint16_t k = M17_SYNC_SYMS + 5; k < M17_FRAME_SYMS; k += 31)
		flip_sign(frame, k);
	int lf = m17_lich_get(frame, NULL, got_lich);
	int pf = m17_stream_get(frame, NULL, &w, &got_fn, got);
	CHECK(lf >= 0 && !memcmp(got_lich, lich, sizeof lich), "str %u: LICH with errors (%d)", n, lf);
	CHECK(pf > 0 && got_fn == fn && !memcmp(got, pl, sizeof got), "str %u: payload with errors (%d)", n, pf);
}

static void testVectors(void)
{
	FILE *f = fopen("../vectors/m17-frames.txt", "r");
	char line[512], kind[8], a[128], b[128], d[128], e[128];
	unsigned n_addr = 0, n_lsf = 0, n_str = 0, cnt, fn;

	CHECK(f != NULL, "the M17 vectors open");
	if (!f)
		return;
	while (fgets(line, sizeof line, f)) {
		uint8_t lsf[M17_LSF_BYTES], frame[M17_FRAME_BYTES], pl[M17_STREAM_BYTES];

		if (line[0] == '#' || sscanf(line, "%7s", kind) != 1)
			continue;
		if (!strcmp(kind, "addr")) {
			char text[16] = "", back[16] = "", t[M17_CALL_TEXT];
			uint8_t want[M17_ADDR_BYTES], got[M17_ADDR_BYTES];
			uint64_t v = 0;
			if (sscanf(line, "addr \"%15[^\"]\" %127s \"%15[^\"]\"", text, a, back) != 3)
				continue;
			hex(a, want, sizeof want);
			CHECK(enc(text, &v), "addr '%s' encodes", text);
			m17_addr_put(got, v);
			CHECK(!memcmp(got, want, sizeof got), "addr '%s' as libm17 (%s)", text, a);
			CHECK(m17_addr_decode(m17_addr_get(want), t) == strlen(back) && !strcmp(t, back),
			      "addr %s decodes '%s', libm17 '%s'", a, t, back);
			n_addr++;
		} else if (!strcmp(kind, "lsf") && sscanf(line, "%*s %127s %127s", a, b) == 2) {
			hex(a, lsf, sizeof lsf);
			hex(b, frame, sizeof frame);
			testLsfFrame(lsf, frame, n_lsf++);
		} else if (!strcmp(kind, "str") && sscanf(line, "%*s %127s %u %x %127s %127s", a, &cnt, &fn, d, e) == 5) {
			hex(a, lsf, sizeof lsf);
			hex(d, pl, sizeof pl);
			hex(e, frame, sizeof frame);
			testStreamFrame(lsf, cnt, (uint16_t)fn, pl, frame, n_str++);
		}
	}
	fclose(f);
	CHECK(n_addr >= 10 && n_lsf >= 6 && n_str >= 12, "vectors read: %u addr, %u lsf, %u str", n_addr, n_lsf, n_str);
}

// ── The LSF from LICH chunks ──
static void testLsfAssembly(void)
{
	uint8_t lsf[M17_LSF_BYTES], other[M17_LSF_BYTES], lich[M17_LICH_BYTES];
	m17_lsf_asm_t as;
	bool done = false;

	for (int i = 0; i < M17_LSF_BYTES; i++)
		lsf[i] = (uint8_t)rng(), other[i] = (uint8_t)rng();
	m17_lsf_crc_set(lsf);
	m17_lsf_crc_set(other);

	// Joining late: chunks 3, 4, 5, 0, 1, 2
	m17_lsf_asm_reset(&as);
	for (int i = 0; i < 6; i++) {
		m17_lich_make(lsf, (uint8_t)((i + 3) % 6), lich);
		done = m17_lsf_asm_add(&as, lich);
		CHECK(done == (i == 5), "chunk %d: done %d", i, done);
	}
	CHECK(done && !memcmp(as.lsf, lsf, sizeof lsf), "the LSF put together");

	// Another stream's chunk starts the set over
	m17_lsf_asm_reset(&as);
	for (int i = 0; i < 3; i++) {
		m17_lich_make(lsf, (uint8_t)i, lich);
		m17_lsf_asm_add(&as, lich);
	}
	m17_lich_make(other, 1, lich);
	m17_lsf_asm_add(&as, lich);
	CHECK(as.have == 0x02, "a disagreeing chunk starts over: have %02X", as.have);

	// A wrong count is ignored
	lich[5] = 7 << 5;
	CHECK(!m17_lsf_asm_add(&as, lich) && as.have == 0x02, "counter 7 is ignored");
}

int main(void)
{
	testCrc();
	testAddr();
	testVectors();
	testLsfAssembly();
	printf("m17Test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
