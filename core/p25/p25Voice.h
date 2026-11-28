// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 David Behnke
/*
 * P25 voice between the engine (p25_engine.h, 88-bit full-rate voice frames) and the board's PCM audio
 * (hal_audio_t). docs/spec/p25.md 5.
 *
 *  RX: the engine's rx_voice hook queues each frame; the audio block callback takes one per 20 ms and decodes it
 *      with the full-rate decoder (core/mbe/imbe.h). Frames come nine at a time (one LDU every 180 ms), so playback
 *      starts once a whole LDU is queued and P25_VOICE_LEAD more blocks have passed: the next LDU then arrives with
 *      that much to spare. A frame the FEC could not trust, and a block with nothing queued during a call, go to the
 *      decoder as bad: it repeats the last good frame, then mutes.
 *  TX: while a transmission is on (p25VoiceTx) the audio block callback encodes each mic block with the full-rate
 *      encoder and queues the frame; the engine's tx_voice hook takes them one at a time. With none ready the hook
 *      says so and the engine sends silence (its tx_filler). The speaker is silent meanwhile.
 *
 * Threading: p25VoiceBegin, p25VoicePoll, p25VoiceTx and p25VoiceEnd run in the radio core's task, the hooks in the
 * provider's task, the decoder and the encoder only in the audio callback. Each queue is single producer / single
 * consumer and call boundaries are counters each side only reads from the other, as in core/dmr/ambe/ambeVoice.c.
 * No RTOS call.
 *
 * Memory is the caller's (p25_voice_mem_t): it may be lent by something idle while P25 runs (hal_vocoder_t.lend).
 */
#ifndef RFOUNDRY_P25_VOICE_H
#define RFOUNDRY_P25_VOICE_H

#include "audio/rx_agc.h"
#include <stdbool.h>
#include <stdint.h>
#include "hal_audio.h"
#include "hal_vocoder.h"
#include "../mbe/imbe.h"
#include "../mbe/mbe_est.h"
#include "p25_engine.h"
#include "config.h"
#include "p25_parrot.h"

#define P25_VOICE_QUEUE		32	// frames (640 ms), power of two: two LDUs and room for jitter
#define P25_VOICE_PRIME		P25_VOICE_FRAMES	// P25's frames per burst (p25VoiceBegin): one LDU
#define P25_VOICE_LEAD		2	// blocks of silence after that, the margin for the next LDU
#define P25_VOICE_TX_QUEUE	8	// encoded frames waiting for the engine (160 ms), power of two

typedef struct {
	#if CONFIG_RX_AGC
	uint32_t source;
	uint16_t scope;
#endif
	uint8_t seq;		// the call it belongs to
	uint8_t errs;		// what the FEC corrected, packed (IMBE_ERRS)
	uint8_t imbe[P25_IMBE_BYTES];
} p25_voice_entry_t;

typedef struct {
	uint8_t gen;		// the transmission it belongs to
	uint8_t imbe[P25_IMBE_BYTES];
} p25_voice_tx_entry_t;

// Everything the voice path needs while P25 runs: the queues, and the decoder's and the encoder's kept and scratch
// memory. A radio transmits or receives, so the two share the prediction state and the scratch; only the estimator's
// history is the encoder's own
typedef struct {
	p25_voice_entry_t q[P25_VOICE_QUEUE];
	p25_voice_tx_entry_t tq[P25_VOICE_TX_QUEUE];
	mbe_est_state_t est;
	imbe_dec_state_t st;
	mbe_params_t pr;
	mbe_state_t ms;
	mbe_frame_t prev, cur;
	int32_t buf[MBE_BUF_WORDS];
} p25_voice_mem_t;

typedef struct {
	uint32_t rx_in;		// frames the engine handed in
	uint32_t rx_frames;	// good frames decoded and played
	uint32_t rx_bad;	// frames the FEC could not trust (played as concealment)
	uint32_t rx_concealed;	// blocks the decoder filled from a bad or missing frame (repeat, then mute)
	uint32_t rx_calls;	// calls that started playing
	uint32_t rx_underruns;	// blocks with nothing queued during a call
	uint32_t rx_underrun_at;	// frames the call had played at its latest underrun
	uint32_t rx_overflows;	// frames dropped, queue full
	uint32_t rx_bit_errors;	// bits corrected by the FEC in good frames
	uint32_t rx_queue_max;	// most frames queued at once (of P25_VOICE_QUEUE)
	uint32_t rx_peak;	// largest decoded sample (absolute) in the latest call that played
	uint32_t tx_frames;	// frames handed to the engine for transmission
	uint32_t tx_encoded;	// mic blocks encoded
	uint32_t tx_dropped;	// encoded frames dropped, queue full (the engine took none in time)
	uint32_t tx_silent;	// of tx_encoded, the frames the encoder made silence frames (no signal at all)
	uint32_t tx_mic_peak;	// largest mic sample (absolute, after the gain) in the latest transmission
	uint32_t tx_gain_q16;	// final software gain of the latest transmit block
	uint32_t tx_limited_blocks;	// blocks peak limited in the latest transmission
	uint32_t tx_mic_muted;	// blocks of it sent as silence: the mic gate was on (a beep, p25VoiceSetMicGate)
} p25_voice_stats_t;

// The board's PCM audio (NULL: frames are counted, nothing plays). Once, at start-up
void p25VoiceInit(const hal_audio_t *audio);
// Start on mem (radio task, before p25_engine_begin); the stream starts with the first call (p25VoicePoll). prime: the
// frames a burst brings at once, queued before playback starts (P25_VOICE_PRIME, one LDU; YSF VW 5, one frame's);
// agc: the receive AGC's mode, whose settings apply (RX_AGC_P25, or RX_AGC_YSF for YSF VW)
void p25VoiceBegin(p25_voice_mem_t *mem, uint8_t prime, rx_agc_mode_t agc);
// The caller and its scope for the receive AGC's memory, for a mode whose calls are not P25's (YSF VW): after the
// rx_start hook, which clears them, and again as the fields fill in. Provider's task, as the hooks
void p25VoiceRxIdentity(uint32_t source, uint16_t scope);
// Radio task, every tick while P25 runs: starts the audio stream once a call is heard
void p25VoicePoll(void);
// A transmission starts (the stream starts if it was not running; from the next block the mic is encoded) or ends.
// Radio task, around the engine's transmit calls
void p25VoiceTx(bool on);
// The mic samples' gain step (radio/mic_gain.h: the Mic gain setting; the board's mic_pcm_db is DMR's only). Any task
void p25VoiceSetMicGain(uint8_t step);
// The transmit leveling target, an RMS (dsp/tx_level.h: tx_level_choice_rms; the P25 TX level setting, Digital AGC),
// 0 = none: the manual mic gain. p25VoiceAgc: whether leveling is on in this build. Any task
void p25VoiceSetTxTarget(uint16_t rms);
bool p25VoiceAgc(void);
uint16_t p25VoiceTxTarget(void);
#if CONFIG_P25_PARROT
// Radio task controls; codec state and recording belong only to the audio callback.
// Borrow two frame buffers totaling 500 entries until p25VoiceEnd; they must not overlap the voice workspace.
void p25VoiceParrotReady(uint8_t (*frames)[IMBE_BYTES], unsigned count, uint8_t (*extra)[IMBE_BYTES]);
// Record the chosen gain step. When the board applies the step, software gain starts at unity.
bool p25VoiceParrotCapture(bool agc, int8_t target_dbfs, uint8_t mic_step, bool hardware_gain);
// Caller-owned signed 16-bit PCM, retained only until close; disjoint from codec/frame workspace.
void p25VoiceParrotPcm(int16_t *pcm, unsigned samples);
bool p25VoiceParrotCaptureMode(uint8_t mode, bool dc, bool agc, int8_t target_dbfs,
	uint8_t mic_step, bool hardware_gain);
void p25VoiceParrotStop(void);
// Stop the stalled stream, retaining its report and the workspace for another capture.
void p25VoiceParrotFail(void);
uint8_t p25VoiceParrotState(void);
// Advances during capture, speaker lead-in, and decode. Waiting on queued speaker PCM leaves it unchanged.
uint16_t p25VoiceParrotProgress(void);
void p25VoiceParrotView(p25_parrot_view_t *out);
#endif
// Stop (radio task, after p25_engine_end): the stream stops and mem is free again
void p25VoiceEnd(void);
// Whether the voice stream runs (its callback owns hal_audio_t). Radio task only
bool p25VoiceStreaming(void);
// Something else to hear while the stream runs (beeps), as ambeVoiceSetMixer
typedef bool (*p25_voice_mixer_t)(int16_t *spk);
void p25VoiceSetMixer(p25_voice_mixer_t mixer);
// While transmitting, asked before each mic block is encoded: true sends the block as silence (a beep on the speaker
// is in the mic too), as ambeVoiceSetMicGate. NULL = none
typedef bool (*p25_voice_mic_gate_t)(void);
void p25VoiceSetMicGate(p25_voice_mic_gate_t gate);
// The engine's hooks into this module (ctx unused)
const p25_engine_hooks_t *p25VoiceHooks(void);
void p25VoiceGetStats(p25_voice_stats_t *out);
void p25VoiceResetStats(void);

// The audio block callback (exposed for tests): one 20 ms block in, one out
void p25VoiceAudioBlock(const int16_t *mic, int16_t *spk, void *ctx);

#endif
