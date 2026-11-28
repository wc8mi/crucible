// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
/*
 * M17 voice between the engine (stream frames, two Codec 2 3200 frames each) and 20 ms blocks of PCM (hal_audio.h's
 * block). docs/spec/m17.md; the design is p25Voice's (core/p25/p25Voice.h).
 *
 *  RX: the engine hands in each stream frame's payload (m17_voice_rx); the audio block callback takes one Codec 2
 *      frame per 20 ms and decodes it. Playback starts once M17_VOICE_PRIME frames are queued. A block with nothing
 *      queued during a call plays the last frame again once, then silence.
 *  TX: while a transmission is on (m17_voice_tx), the block callback encodes each mic block and queues the frame;
 *      the engine takes them two at a time for a stream frame (m17_voice_tx_take).
 *
 * Threading: m17_voice_begin, m17_voice_tx and m17_voice_end run in the radio core's task, m17_voice_rx and
 * m17_voice_tx_take in the provider's, the codec only in the block callback (m17_voice_block). Each queue is single
 * producer / single consumer, counters each side only reads from the other. No RTOS call.
 *
 * Memory is the caller's (m17_voice_mem_t): it may be lent by something idle while M17 runs. A radio transmits or
 * receives, so the encoder and the decoder share theirs; each is set up again when its direction starts.
 */
#ifndef RFOUNDRY_M17_VOICE_H
#define RFOUNDRY_M17_VOICE_H

#include <stdbool.h>
#include <stdint.h>

#include <codec2/codec2_fxp.h>

#include "m17.h"

#define M17_VOICE_BYTES		C2FXP_BYTES_PER_FRAME	// a Codec 2 3200 frame, 20 ms
#define M17_VOICE_SAMPLES	C2FXP_SAMPLES_PER_FRAME
#define M17_VOICE_QUEUE		16	// frames (320 ms), power of two: priming and jitter
#define M17_VOICE_PRIME		4	// queued before playback starts: two stream frames (80 ms)
#define M17_VOICE_TX_QUEUE	8	// encoded frames waiting for the engine (160 ms), power of two

_Static_assert(M17_STREAM_BYTES == 2 * M17_VOICE_BYTES, "a stream frame carries two Codec 2 frames");

typedef struct {
	uint8_t call;		// the call it belongs to (m17_voice_rx's new_call counts them)
	uint8_t bits[M17_VOICE_BYTES];
} m17_voice_entry_t;

typedef struct {
	union {
		c2fxp_encoder_t enc;
		c2fxp_decoder_t dec;
	} c;
	m17_voice_entry_t q[M17_VOICE_QUEUE];
	uint8_t tq[M17_VOICE_TX_QUEUE][M17_VOICE_BYTES];
	uint8_t last[M17_VOICE_BYTES];	// the frame played last, for one repeat
} m17_voice_mem_t;

typedef struct {
	uint32_t rx_in;		// Codec 2 frames the engine handed in
	uint32_t rx_frames;	// frames decoded and played
	uint32_t rx_concealed;	// blocks played from the last frame again, or as silence
	uint32_t rx_calls;	// calls that started playing
	uint32_t rx_underruns;	// blocks with nothing queued during a call
	uint32_t rx_overflows;	// frames dropped, queue full
	uint32_t tx_encoded;	// mic blocks encoded
	uint32_t tx_frames;	// frames handed to the engine
	uint32_t tx_dropped;	// encoded frames dropped, queue full
} m17_voice_stats_t;

// Start on mem (radio task, before the engine starts): receiving, nothing queued
void m17_voice_begin(m17_voice_mem_t *mem);
// A stream frame's payload (provider's task). new_call: the first frame of another call (a new LSF): what is queued
// of the last call is dropped and playback primes again
void m17_voice_rx(const uint8_t payload[M17_STREAM_BYTES], bool new_call);
// The call ended (its last frame, or the signal lost): what is queued plays out, then silence
void m17_voice_rx_end(void);
// A transmission starts (the encoder is set up again; from the next block the mic is encoded) or ends (the decoder
// is set up again). Radio task
void m17_voice_tx(bool on);
// Two encoded frames for a stream frame (provider's task); false with fewer queued
bool m17_voice_tx_take(uint8_t payload[M17_STREAM_BYTES]);
// The 20 ms block (the audio callback): mic in while transmitting, speaker out while receiving. True while a call
// plays (the block is its sound, or the one repeat that covers a late frame): the caller routes the speaker by it
bool m17_voice_block(const int16_t *mic, int16_t *spk);
// Stop (radio task, after the engine stops): mem is free again
void m17_voice_end(void);
void m17_voice_stats(m17_voice_stats_t *out);

#endif
