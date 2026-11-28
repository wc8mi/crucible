// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// M17 (docs/spec/m17.md): the frames of a voice stream and the station addresses, from the M17 specification 2.x.
// No RTOS or board call: the host tests run it as it is.
//
// A frame is 192 symbols (40 ms at 4800 symbols/s): an 8-symbol sync, then 368 payload bits two to a symbol. Here it is
// held as the framer holds it (air_frame_t.hard): dibits, symbol 0 first, four to a byte. M17's symbols map to dibits
// as the air interface's do (hal_air.h), so the frame is the 16-bit sync and the 368 bits, MSB first, in 48 bytes.
//
// Coding, sender's side: type 1 bits (with four zero tail bits) -> rate 1/2, K=5 convolutional code (type 2) ->
// punctured (type 3) -> interleaved over the 368 bits (type 4) -> randomized -> behind the sync. The stream frame's
// LICH skips the convolutional code: four Golay(24,12) words, interleaved and randomized with the payload.
#ifndef RFOUNDRY_M17_H
#define RFOUNDRY_M17_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ── Syncs (16 bits, 8 symbols), each at the start of its frame ──
#define M17_SYNC_LSF		0x55F7u	// the link setup frame; also the end of the preamble before it
#define M17_SYNC_STREAM		0xFF5Du
#define M17_SYNC_PACKET		0x75FFu
#define M17_SYNC_BERT		0xDF55u
#define M17_EOT			0x555Du	// the end of transmission: this word over and over, a frame's worth

#define M17_FRAME_SYMS		192
#define M17_FRAME_BYTES		48	// M17_FRAME_SYMS dibits
#define M17_SYNC_SYMS		8
#define M17_PAYLOAD_BITS	368

// ── Link setup frame (LSF): DST, SRC, TYPE, META, CRC ──
#define M17_LSF_BYTES		30
#define M17_LSF_DST		0	// offsets: 6 bytes each, big-endian (m17_addr_*)
#define M17_LSF_SRC		6
#define M17_LSF_TYPE		12	// 2 bytes, big-endian: M17_TYPE_*
#define M17_LSF_META		14	// 14 bytes
#define M17_LSF_CRC		28	// 2 bytes, big-endian, over bytes 0-27
#define M17_META_BYTES		14

// TYPE, as a 16-bit word
#define M17_TYPE_STREAM		0x0001u	// else packet mode
#define M17_TYPE_DATA_MASK	0x0006u
#define M17_TYPE_DATA_VOICE	0x0004u	// voice only: Codec 2 3200
#define M17_TYPE_DATA_VD	0x0006u	// voice (Codec 2 1600) and data
#define M17_TYPE_ENC_MASK	0x0018u	// 0 = none
#define M17_TYPE_SUB_SHIFT	5	// 2 bits: the key length, or with no encryption the META type
#define M17_TYPE_CAN_SHIFT	7	// 4 bits
#define M17_TYPE_SIGNED		0x0800u	// the stream ends with an authentication frame and its signature

// ── Stream frame: the LICH (a sixth of the LSF and its counter), then FN and 16 bytes of payload ──
#define M17_LICH_BYTES		6	// 5 bytes of the LSF, then the counter in bits 7-5 of byte 5
#define M17_LICH_CHUNKS		6	// LICH counter n carries LSF bytes 5n..5n+4
#define M17_STREAM_BYTES	16	// voice only: two Codec 2 3200 frames (20 ms each)
#define M17_FN_LAST		0x8000u	// FN's top bit: the stream's last frame
#define M17_FN_MASK		0x7FFFu	// the counter, which wraps to 0
#define M17_FN_AUTH		0x7FFBu	// a signed stream's authentication frame; the four after it carry the signature

static inline uint16_t m17_be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

// ── CRC (spec, Data Link Layer: CRC): polynomial 0x5935, register loaded with ones, MSB first, nothing reflected or
// inverted. Run over a block with its CRC behind it (big-endian), it gives 0 ──
uint16_t m17_crc(const uint8_t *p, size_t n);
// Writes the CRC of bytes 0-27
void m17_lsf_crc_set(uint8_t lsf[M17_LSF_BYTES]);
bool m17_lsf_crc_ok(const uint8_t lsf[M17_LSF_BYTES]);

// ── Frames. hard: the frame (M17_FRAME_BYTES), soft: one value per symbol (AIR_SOFT_OUTER at +3) or NULL ──
// The decoders' memory, the caller's: an LSF's depunctured soft bits and Viterbi decisions, the longest
typedef struct {
	int8_t soft[488];
	uint16_t dec[244];
} m17_fec_work_t;

// The whole frame: the LSF sync and the coded LSF
void m17_lsf_put(uint8_t frame[M17_FRAME_BYTES], const uint8_t lsf[M17_LSF_BYTES]);
// The LSF; coded bits corrected (0..), or -1 when its CRC fails
int m17_lsf_get(const uint8_t *hard, const int8_t *soft, m17_fec_work_t *w, uint8_t lsf[M17_LSF_BYTES]);
// The whole frame: the stream sync, the LICH and FN with the payload
void m17_stream_put(uint8_t frame[M17_FRAME_BYTES], const uint8_t lich[M17_LICH_BYTES], uint16_t fn,
		    const uint8_t payload[M17_STREAM_BYTES]);
// The LICH; bits the Golay code corrected (0..12), or -1 when a word was past its reach
int m17_lich_get(const uint8_t *hard, const int8_t *soft, uint8_t lich[M17_LICH_BYTES]);
// FN and the payload; coded bits corrected (0..). No CRC protects them: a large count says the frame is noise
int m17_stream_get(const uint8_t *hard, const int8_t *soft, m17_fec_work_t *w, uint16_t *fn,
		   uint8_t payload[M17_STREAM_BYTES]);

// ── The LSF in LICH chunks ──
// Chunk n (0..5) of the LSF with its counter
void m17_lich_make(const uint8_t lsf[M17_LSF_BYTES], uint8_t n, uint8_t lich[M17_LICH_BYTES]);
static inline uint8_t m17_lich_count(const uint8_t lich[M17_LICH_BYTES]) { return (uint8_t)(lich[5] >> 5); }

// The LSF put together from the chunks as they come (a receiver that missed the LSF, or joined late)
typedef struct {
	uint8_t lsf[M17_LSF_BYTES];
	uint8_t have;		// chunks in, a bit each
} m17_lsf_asm_t;

static inline void m17_lsf_asm_reset(m17_lsf_asm_t *a) { a->have = 0; }
// Adds a chunk; true when all six are in and the CRC holds (a->lsf is the LSF). A chunk that disagrees with the one
// held starts the set over: another station's stream
bool m17_lsf_asm_add(m17_lsf_asm_t *a, const uint8_t lich[M17_LICH_BYTES]);

// ── Addresses (spec, appendix A: Address Encoding): up to nine characters in base 40, the first the least significant
// digit ──
#define M17_ADDR_BYTES		6
#define M17_ADDR_BROADCAST	0xFFFFFFFFFFFFull
#define M17_ADDR_HASH		262144000000000ull	// 40^9: from here, # and up to eight characters
#define M17_ADDR_HASH_END	(M17_ADDR_HASH + 6553600000000ull)	// 40^9 + 40^8: from here to broadcast, reserved
#define M17_CALL_MAX		9
#define M17_CALL_TEXT		10	// the longest decoded text and its NUL ("#" and eight)

// Text to an address. Letters (either case), digits, '-', '/', '.' and spaces; a leading '#' for the hash range;
// "@ALL" for broadcast. Spaces at the end don't count (a space-padded field encodes as its text). False for another
// character, more than nine characters, or nothing (0 is not an address)
bool m17_addr_encode(const char *s, size_t len, uint64_t *addr);
// An address to text (out: M17_CALL_TEXT): "@ALL" for broadcast, "" for 0 and the reserved range. Returns the length
size_t m17_addr_decode(uint64_t addr, char *out);
// The destination a channel names (docs/crucible/adr/006-m17.md 6): blank or "ALL" (any case) is broadcast. False as
// m17_addr_encode otherwise
bool m17_addr_dest(const char *s, size_t len, uint64_t *addr);
void m17_addr_put(uint8_t p[M17_ADDR_BYTES], uint64_t addr);
uint64_t m17_addr_get(const uint8_t p[M17_ADDR_BYTES]);

#endif
