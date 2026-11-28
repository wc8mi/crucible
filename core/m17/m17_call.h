// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// The M17 call layer, receive (docs/spec/m17.md 6): the framer's table for the mode (air_framer.h), and calls built
// from what it cuts. A call starts at its LSF, or late from the LSF its stream frames carry in LICH chunks; stream
// frames hand their payload to a sink; the call ends at the FN end bit, at EoT, or when the signal is lost. Which
// calls play is the engine's (m17_engine.h): this layer hands on every stream's voice with the call's fields.
// No RTOS or board call: the host tests run it as it is.
#ifndef RFOUNDRY_M17_CALL_H
#define RFOUNDRY_M17_CALL_H

#include <stdbool.h>
#include <stdint.h>

#include "m17.h"
#include "../air/air_framer.h"

// The air mode's sync ids (air_frame_t.sync_id)
enum { M17_SID_LSF, M17_SID_STREAM, M17_SID_PACKET, M17_SID_EOT };

#define M17_TEXT_BLOCKS		4	// META text: up to four blocks of 13 characters
#define M17_TEXT_BLOCK		13
#define M17_TEXT_MAX		(M17_TEXT_BLOCKS * M17_TEXT_BLOCK)	// 52

// TYPE's fields
static inline uint8_t m17_type_can(uint16_t t) { return (uint8_t)((t >> M17_TYPE_CAN_SHIFT) & 0x0Fu); }
static inline uint8_t m17_type_enc(uint16_t t) { return (uint8_t)((t & M17_TYPE_ENC_MASK) >> 3); }
static inline uint8_t m17_type_sub(uint16_t t) { return (uint8_t)((t >> M17_TYPE_SUB_SHIFT) & 3u); }
static inline uint16_t m17_type_data(uint16_t t) { return (uint16_t)(t & M17_TYPE_DATA_MASK); }

// META types without encryption (TYPE's subtype)
#define M17_META_TEXT		0

// A call as received
typedef struct {
	uint64_t src, dst;
	uint16_t type;			// TYPE
	uint8_t can;
	bool late;			// its LSF came from LICH chunks: the frames before were not played
	uint8_t meta[M17_META_BYTES];
	uint8_t text_have, text_want;	// META text blocks in, and the blocks the sender uses (a bit each)
	uint8_t text_raw[M17_TEXT_MAX];	// the blocks as received
	char text[M17_TEXT_MAX + 1];	// META text as far as it came, NUL terminated; spaces padding the end kept
} m17_call_t;

// What the call layer hands on, from the provider's task
typedef struct {
	void (*on_start)(const m17_call_t *c, void *ctx);	// a call began (its LSF is in)
	void (*on_update)(const m17_call_t *c, void *ctx);	// a field changed: META text
	// A stream frame's payload, in order. fn: its FN with the end bit
	void (*on_voice)(const uint8_t payload[M17_STREAM_BYTES], uint16_t fn, void *ctx);
	void (*on_end)(void *ctx);
} m17_call_sink_t;

typedef struct {
	uint32_t lsf_ok, lsf_fail;	// LSF frames whose CRC held, failed
	uint32_t lich_ok, lich_fail;	// stream frames whose LICH decoded, did not
	uint32_t frames;		// stream frames
	uint32_t fn_gaps;		// stream frames missing inside a call (FN jumped)
	uint32_t fec_bits;		// bits the codes corrected
	uint32_t auth_frames;		// a signed stream's authentication and signature frames, not played
	uint32_t eot;			// EoT seen
	uint32_t packets;		// packet LSFs and frames, dropped
	uint32_t held;			// stream frames before the LSF was known, not played
	uint32_t calls;
} m17_call_stats_t;

typedef struct {
	const m17_call_sink_t *sink;
	void *ctx;
	bool active;			// a call is in hand
	bool started_lsf;		// it began at an LSF (else from LICH)
	bool fn_known;
	uint8_t coasted;		// frames taken in a row without a LICH that decoded
	uint16_t fn;			// the last FN, end bit off
	m17_call_t call;
	m17_lsf_asm_t asm_;		// the LSF from LICH chunks
	uint32_t asm_t;			// the provider's time of the last chunk in (or of the call's end)
	uint32_t t_us;			// of the frame in hand
	uint8_t lsf[M17_LSF_BYTES];	// the call's LSF as received
	// What the head hook decoded of the frame in hand, for rx_frame: the LSF, or the LICH and the stream's FN and
	// payload; bits corrected, -1 failed
	int8_t cur_lsf_errs, cur_lich_errs;
	int16_t cur_str_errs;
	uint16_t cur_fn;
	uint8_t cur_lsf[M17_LSF_BYTES];
	uint8_t cur_lich[M17_LICH_BYTES];
	uint8_t cur_payload[M17_STREAM_BYTES];
	m17_fec_work_t work;
	m17_call_stats_t stats;
} m17_call_rx_t;

// The mode at 4800 symbols/s, RRC 0.5, +-2400 Hz (m17.md 1). Its context is an m17_call_rx_t
extern const air_mode_t m17_air_mode;

void m17_call_rx_init(m17_call_rx_t *c, const m17_call_sink_t *sink, void *ctx);
// Drop the call in hand (a retune): its end goes to the sink
void m17_call_rx_reset(m17_call_rx_t *c);

// The META text from a META field (M17_META_TEXT): block k of up to four, by the control byte. Returns false when the
// control byte names no block
bool m17_meta_text_add(m17_call_t *c, const uint8_t meta[M17_META_BYTES]);

#endif
