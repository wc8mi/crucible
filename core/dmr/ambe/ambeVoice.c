// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// DMR voice glue: call stack frames <-> FEC <-> vocoder <-> PCM blocks (ambeVoice.h)
#include "dmr/ambe/ambeVoice.h"
#include "dmr/ambe/ambeFec.h"
#include "config.h"
#include "dsp/fxp.h"
#include "dsp/mic_gain.h"
#include "dsp/tx_level.h"
#include "radio/sigtone.h"

#include <stddef.h>
#include <string.h>

#if (AMBE_VOICE_QUEUE & (AMBE_VOICE_QUEUE - 1)) != 0 || AMBE_VOICE_QUEUE > 128
#error "AMBE_VOICE_QUEUE must be a power of two up to 128"
#endif

#define LOAD(x)		__atomic_load_n(&(x), __ATOMIC_ACQUIRE)
#define STORE(x, v)	__atomic_store_n(&(x), (v), __ATOMIC_RELEASE)

typedef struct {
#if CONFIG_RX_AGC
	uint32_t source;
	uint16_t scope;
	uint8_t mode;
#endif
	uint8_t seq;		// call it belongs to
	uint8_t bad;		// 1 uncorrectable, 2 lost
	uint8_t bits[VOCODER_BITS];
} rx_entry_t;

typedef struct {
	uint8_t seq;
	uint8_t frame[AMBE_FRAME_BYTES];
} tx_entry_t;

static const hal_audio_t *aud;
static const hal_vocoder_t *voc;
static bool streaming;			// call task only

// RX queue: producer the call task (head), consumer the audio callback (tail)
static rx_entry_t rxq[AMBE_VOICE_QUEUE];
static uint8_t rx_head, rx_tail;
// TX queue: producer the audio callback (head), consumer the call task (tail)
static tx_entry_t txq[AMBE_VOICE_QUEUE];
static uint8_t tx_head, tx_tail;

// Call boundaries, written by the call task only: begin bumps the sequence, end copies it
static uint8_t rx_seq, rx_end_seq;
static uint8_t rx_place;	// the next frame's place in its burst (0-2); a call starts at a burst's first
static uint8_t rx_prev[VOCODER_BITS];	// the call's previous good frame (rx_repeats)
static bool rx_have_prev;
static uint8_t tx_seq, tx_end_seq;	// equal: no call (also at start-up)

// Audio callback state
static uint8_t cb_rx_seq, cb_tx_seq;
static bool cb_rx_playing, cb_rx_primed, cb_rx_live;
static bool cb_speaker;
// The stream just (re)started: the speaker route is what its last user left, which may be a beep's own stream that
// had it on. The first block sends cb_speaker whatever it was, or the route (and the board's amplifier) stays on
static bool cb_speaker_unsent;
static uint8_t cb_bad_run;
static uint32_t cb_call_frames;		// frames played in this call (rx_underrun_at)
static uint8_t last_bits[VOCODER_BITS];
static bool have_last;

#if CONFIG_RX_AGC
static rx_agc_t agc;
static rx_agc_mode_t rx_mode;
static uint32_t rx_source;
static uint16_t rx_scope;
static bool cb_rx_good;

void ambeVoiceRxIdentity(rx_agc_mode_t mode, uint32_t source, uint16_t scope)
{
	rx_mode = mode;
	rx_source = source;
	rx_scope = scope;
}
#endif

static ambe_voice_stats_t stats;
static ambe_voice_mixer_t mixer;
static ambe_voice_mic_gate_t mic_gate;
static bool cb_mixing;
// Simplex: nothing is decoded while a transmission runs. A platform may keep the encoder's and the decoder's state in
// one block (hal_vocoder_t), so a decode then would cost the transmission its encoder. A call heard meanwhile plays as
// silence, and the decoder starts fresh once the transmission is over (cb_dec_fresh)
static bool cb_tx_on, cb_dec_fresh;
static uint16_t mic_gain_q8 = 256;	// ambeVoiceSetMicGainQ8: the mic samples times this / 256 before encoding
#if CONFIG_TX_MIC_LIMITER
static tx_level_t mic_level;
static uint16_t mic_low_q8;		// lowest limiter gain in the current transmission, audio callback only
static uint16_t tx_target[AMBE_TX_PROFILES];	// ambeVoiceSetTxTarget: any task
static uint8_t tx_profile_next;		// ambeVoiceTxProfile: the call task's, taken by source_begin
static uint8_t tx_profile;		// the running transmission's: source_begin, before tx_seq moves
#endif
#if CONFIG_MIC_CHECK
static uint8_t check_on, check_profile;	// ambeVoiceMicCheck: any task
static uint8_t check_gen, check_gen_seen;	// bumped each time it turns on: a check the stream stopped under starts over
static ambe_mic_check_t check_view;	// published by the audio callback, a field at a time
static tx_level_t check_level;		// the rest: the audio callback's
static bool check_running;
static uint8_t check_blocks;
static uint16_t check_in_peak, check_out_peak, check_clips;
static uint32_t check_in_power, check_out_power, check_limited0;
static uint8_t check_rec_req, check_play_req;	// ambeVoiceMicCheckRecord / _Play: bumped by any task
static uint8_t check_rec_seen, check_play_seen;
static uint8_t check_phase, check_n, check_i;	// AMBE_CHECK_*, frames recorded, the next to play
static uint8_t check_frames[AMBE_MIC_CHECK_FRAMES][7];	// 49 bits each, packed MSB first
static uint32_t check_dec_power;
static uint16_t check_dec_blocks, check_dec_peak;
#endif
// The encoder's input after gain, tones or the gate: the audio callback's (off its stack). The mic check uses it too,
// only while nothing is transmitted
static int16_t scaled[AUDIO_BLOCK_SAMPLES];
static bool test_signal;		// ambeVoiceSetTestSignal

#if CONFIG_SIG_TONES
// A tone sequence sent in place of the mic (ambeVoiceTxTone): the request is handed over from the radio task with a
// sequence counter; the rest is the audio callback's, but for tone_last_*, which the call task reads (busy)
static beep_seg_t tone_req_seg[SIG_AIR_STEPS];
static uint8_t tone_req_n;
static bool tone_req_final;
static uint8_t tone_req_seq, cb_tone_seq;
static int16_t tone_stale = -1;	// the call task's: the request number the over had ended with (source_end), -1 none
static beep_gen_t tone_gen;
static beep_seg_t tone_seg[SIG_AIR_STEPS];	// the running tone's own copy of the steps: a new request may overwrite tone_req_seg
static bool tone_active;		// the generator still has samples
static bool tone_mute;			// after a final tone: silence in the mic's place until the transmission ends
static uint32_t tone_last_n;		// tx_pushed after the last frame of the tone
static uint8_t tone_last_seq;		// the transmission it belongs to
static bool tone_last_valid;
#define TONE_AMP_Q15	14000		// about -7 dBFS: a tone frame carries its level, speech-like peaks stay below it
#endif
#if CONFIG_SIG_TONES
static uint32_t tx_pushed;		// frames the audio callback queued, and the call task removed (pulled or dropped)
static uint32_t tx_pulled;
#endif
static uint16_t test_phase;		// the test signal's phase (a full cycle is 65536) and its pitch step
static uint8_t test_blocks;
static uint8_t tx_prev[VOCODER_BITS];	// the transmission's previous frame (tx_repeats)
static bool tx_have_prev;
static uint32_t enc_opts = VOCODER_OPT_DEFAULT;	// ambeVoiceSetEncodeOptions: any task
static uint32_t cb_enc_opts;		// what the vocoder was last given (audio callback)
static bool cb_enc_opts_set;		// cb_enc_opts is valid

static void ensure_streaming(void)
{
	if (!streaming && aud != NULL) {
		// Our callback isn't installed yet: its state is the call task's until the start
		cb_speaker = false;
		STORE(cb_speaker_unsent, true);
		streaming = aud->start(ambeVoiceAudioBlock, NULL);
	}
}

// --- sink (call task) ---

static void rx_push(const uint8_t bits[VOCODER_BITS], uint8_t bad)
{
	uint8_t h = rx_head;
	if ((uint8_t)(h - LOAD(rx_tail)) >= AMBE_VOICE_QUEUE) {
		stats.rx_overflows++;
		return;
	}
	if ((uint8_t)(h + 1 - LOAD(rx_tail)) > stats.rx_queue_max) {
		stats.rx_queue_max = (uint8_t)(h + 1 - LOAD(rx_tail));
	}
	rx_entry_t *e = &rxq[h & (AMBE_VOICE_QUEUE - 1)];
#if CONFIG_RX_AGC
	e->source = rx_source;
	e->scope = rx_scope;
	e->mode = (uint8_t)rx_mode;
#endif
	e->seq = rx_seq;
	e->bad = bad;
	if (bits != NULL) {
		memcpy(e->bits, bits, VOCODER_BITS);
	}
	STORE(rx_head, (uint8_t)(h + 1));
}

#if CONFIG_VOICE_CAPTURE
// Written in the call task; the debug link reads count first, then entries below it
static ambe_cap_entry_t cap[AMBE_VOICE_CAP];
static volatile uint8_t cap_count, cap_want;

static void cap_put(const uint8_t *frame)
{
	uint8_t n = cap_count;
	if (n >= cap_want) {
		return;
	}
	cap[n].lost = frame == NULL ? 1u : 0u;
	if (frame != NULL) {
		memcpy(cap[n].frame, frame, sizeof cap[n].frame);
	} else {
		memset(cap[n].frame, 0, sizeof cap[n].frame);
	}
	cap_count = (uint8_t)(n + 1u);
}

void ambeVoiceCaptureArm(uint8_t n)
{
	cap_want = 0;
	cap_count = 0;
	cap_want = n < AMBE_VOICE_CAP ? n : (uint8_t)AMBE_VOICE_CAP;
}

uint8_t ambeVoiceCapture(const ambe_cap_entry_t **entries, uint8_t *want)
{
	*entries = cap;
	*want = cap_want;
	return cap_count;
}
#else
#define cap_put(frame) ((void)0)
#endif

static void sink_begin(const dmr_caller_t *caller, void *ctx)
{
#if CONFIG_RX_AGC
	if (caller != NULL)
		ambeVoiceRxIdentity(RX_AGC_DMR, caller->src, 0);
#else
	(void)caller;
#endif
	(void)ctx;
	rx_place = 0;
	rx_have_prev = false;
	STORE(rx_seq, (uint8_t)(rx_seq + 1));
	ensure_streaming();
}

static void sink_frame(const uint8_t ambe72[DMR_AMBE_FRAME_BYTES], void *ctx)
{
	(void)ctx;
	uint8_t bits[VOCODER_BITS];
	ambe_fec_result_t r;
	if (rx_end_seq == rx_seq) {
		sink_begin(NULL, NULL);	// frames with no call open: a new call
	}
	stats.rx_in++;
	cap_put(ambe72);
	ambeFecDecode(ambe72, bits, &r);
	if (r.c0_failed) {
		stats.rx_bad++;
		stats.rx_bad_at[rx_place]++;
	} else {
		stats.rx_bit_errors += r.errors;
		if (rx_have_prev && memcmp(bits, rx_prev, VOCODER_BITS) == 0) {
			stats.rx_repeats++;
		}
		memcpy(rx_prev, bits, VOCODER_BITS);
		rx_have_prev = true;
	}
	rx_push(bits, r.c0_failed ? 1 : 0);
	rx_place = (uint8_t)((rx_place + 1u) % 3u);
}

static void sink_lost(void *ctx)
{
	(void)ctx;
	stats.rx_lost++;
	cap_put(NULL);
	rx_push(NULL, 2);
	rx_place = (uint8_t)((rx_place + 1u) % 3u);
}

static void sink_end(void *ctx)
{
	(void)ctx;
	STORE(rx_end_seq, rx_seq);
}

// --- source (call task) ---

static void source_begin(void *ctx)
{
	(void)ctx;
#if CONFIG_TX_MIC_LIMITER
	STORE(tx_profile, LOAD(tx_profile_next));	// before the sequence moves: the callback reads it at the change
	STORE(tx_profile_next, (uint8_t)AMBE_TX_DMR);
#endif
	STORE(tx_seq, (uint8_t)(tx_seq + 1));
#if CONFIG_SIG_TONES
	tx_pulled += (uint8_t)(LOAD(tx_head) - tx_tail);
#endif
	STORE(tx_tail, LOAD(tx_head));	// drop what an earlier transmission left (next() also skips by sequence)
	ensure_streaming();
}

static bool source_next(uint8_t ambe72[DMR_AMBE_FRAME_BYTES], void *ctx)
{
	(void)ctx;
	for (;;) {
		uint8_t t = tx_tail;
		if (t == LOAD(tx_head)) {
			return false;
		}
		const tx_entry_t *e = &txq[t & (AMBE_VOICE_QUEUE - 1)];
		bool current = e->seq == tx_seq;
		if (current) {
			memcpy(ambe72, e->frame, AMBE_FRAME_BYTES);
		}
		STORE(tx_tail, (uint8_t)(t + 1));
#if CONFIG_SIG_TONES
		tx_pulled++;
#endif
		if (current) {
			return true;
		}
		// a frame left over from an earlier transmission: drop it
	}
}

static void source_end(void *ctx)
{
	(void)ctx;
	STORE(tx_end_seq, tx_seq);
#if CONFIG_SIG_TONES
	// A tone asked for a transmission that never went on air is not for the next one. Not a request of its own (that
	// is the radio task's: both would write tone_req_*): the number of the latest request is noted, and the audio
	// callback drops it if it has not taken it by then. A later request has another number
	STORE(tone_stale, (int16_t)LOAD(tone_req_seq));
#endif
}

#if CONFIG_SIG_TONES
// Voice still to send after PTT: the end tone's frames not yet pulled (dmr_voice_source_t.busy). Only while a
// transmission runs: a request the audio side has not taken yet (it takes them in a transmission's blocks) is nothing
// to wait for once the over has ended
static bool source_busy(void *ctx)
{
	(void)ctx;
	if (LOAD(tx_end_seq) == LOAD(tx_seq)) {
		return false;
	}
	// Asked for (a cancel is a request with no steps: nothing to wait for), or sounding
	bool playing = LOAD(tone_active) || (tone_req_n != 0 && LOAD(tone_req_seq) != LOAD(cb_tone_seq));
	if (!LOAD(tone_last_valid) || LOAD(tone_last_seq) != LOAD(tx_seq)) {
		return playing;
	}
	return playing || (int32_t)(LOAD(tone_last_n) - tx_pulled) > 0;
}
#endif

// --- audio callback (audio task) ---

static void speaker(bool on)
{
	if (cb_speaker != on || LOAD(cb_speaker_unsent)) {
		cb_speaker = on;
		STORE(cb_speaker_unsent, false);
		if (aud != NULL && aud->speaker_pcm != NULL) {
			aud->speaker_pcm(on);
		}
	}
}

static bool voc_ok(void)
{
	return voc != NULL && voc->present();
}

// The test signal: a triangle wave at about -12 dBFS whose pitch moves to the next of five values every block (one
// vocoder frame, 20 ms). Consecutive frames then differ, so a stuck encoder, or a modem that sends one frame again,
// shows as repeats
static void test_block(int16_t *out)
{
	// 300, 500, 700, 1100 and 1500 Hz at 8 kHz, a cycle being 65536
	static const uint16_t step[5] = { 2458, 4096, 5734, 9011, 12288 };
	uint16_t inc = step[test_blocks];
	for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
		int32_t tri = test_phase < 32768u ? (int32_t)test_phase - 16384 : 49151 - (int32_t)test_phase;
		out[i] = (int16_t)(tri / 2);	// +-8192
		test_phase = (uint16_t)(test_phase + inc);
	}
	test_blocks = (uint8_t)((test_blocks + 1u) % 5u);
}

// A frame to the speaker: silence while transmitting
static void decode(const uint8_t bits[VOCODER_BITS], int16_t *spk)
{
	if (cb_tx_on) {
		cb_dec_fresh = true;
		memset(spk, 0, AUDIO_BLOCK_SAMPLES * sizeof(int16_t));
		return;
	}
	if (cb_dec_fresh) {
		cb_dec_fresh = false;
		voc->reset();
	}
	voc->decode(bits, spk);
}

static void conceal(int16_t *spk)
{
	if (have_last && cb_bad_run < AMBE_VOICE_REPEAT && voc_ok()) {
		stats.rx_concealed++;
		decode(last_bits, spk);
	} else {
		stats.rx_silence++;
		memset(spk, 0, AUDIO_BLOCK_SAMPLES * sizeof(int16_t));
	}
	if (cb_bad_run < 255) {
		cb_bad_run++;
	}
}

static void rx_block(int16_t *spk)
{
	uint8_t seq = LOAD(rx_seq);
	if (seq != cb_rx_seq) {
		// a new call. The speaker route is off after an end or ambeVoiceStop(); re-send it in any case
#if CONFIG_RX_AGC
		rx_agc_end(&agc);
#endif
		cb_rx_seq = seq;
		cb_speaker = false;
		cb_rx_live = true;
		cb_rx_playing = false;
		cb_rx_primed = false;
		cb_bad_run = 0;
		cb_call_frames = 0;
		have_last = false;
		stats.rx_peak = 0;
		if (voc_ok()) {
			if (cb_tx_on) {
				cb_dec_fresh = true;	// not now: it would reset the encoder too (decode())
			} else {
				voc->reset();
			}
		}
	}
	// Drop entries of earlier calls
	uint8_t t = rx_tail, h = LOAD(rx_head);
	while (t != h && rxq[t & (AMBE_VOICE_QUEUE - 1)].seq != cb_rx_seq) {
		t++;
	}
	STORE(rx_tail, t);

	bool ended = LOAD(rx_end_seq) == cb_rx_seq;
	uint8_t queued = (uint8_t)(h - t);
#if CONFIG_RX_AGC
	if (queued != 0) {
		const rx_entry_t *first = &rxq[t & (AMBE_VOICE_QUEUE - 1)];
		if (!agc.started)
			rx_agc_begin(&agc, (rx_agc_mode_t)first->mode, first->source, first->scope);
		else
			rx_agc_identify(&agc, first->source, first->scope);
	}
#endif
	if (!cb_rx_live) {
		memset(spk, 0, AUDIO_BLOCK_SAMPLES * sizeof(int16_t));
		return;
	}
	if (!cb_rx_primed) {
		if (queued >= AMBE_VOICE_PRIME || (ended && queued > 0)) {
			cb_rx_primed = true;
			cb_rx_playing = true;
			stats.rx_calls++;
			speaker(true);
		} else {
			if (ended) {
				cb_rx_live = false;	// a call that ended before playing anything
			}
			memset(spk, 0, AUDIO_BLOCK_SAMPLES * sizeof(int16_t));
			return;
		}
	}
	if (queued == 0) {
		if (ended) {
			cb_rx_live = false;
			cb_rx_playing = false;
			speaker(false);
			memset(spk, 0, AUDIO_BLOCK_SAMPLES * sizeof(int16_t));
		} else {
			stats.rx_underruns++;
			stats.rx_underrun_at = cb_call_frames;
			conceal(spk);
		}
		return;
	}
	const rx_entry_t *e = &rxq[t & (AMBE_VOICE_QUEUE - 1)];
	if (e->bad == 0) {
		memcpy(last_bits, e->bits, VOCODER_BITS);
		have_last = true;
		cb_bad_run = 0;
		STORE(rx_tail, (uint8_t)(t + 1));
		stats.rx_frames++;
		cb_call_frames++;
		if (voc_ok()) {
			decode(last_bits, spk);
#if CONFIG_RX_AGC
			cb_rx_good = !cb_tx_on;
#endif
			// A decoder fed the wrong bits plays silence with every counter clean (a codec reading stale voice RAM)
			for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
				uint32_t a = (uint32_t)(spk[i] < 0 ? -spk[i] : spk[i]);
				if (a > stats.rx_peak) {
					stats.rx_peak = a;
				}
			}
		} else {
			memset(spk, 0, AUDIO_BLOCK_SAMPLES * sizeof(int16_t));
		}
	} else {
		STORE(rx_tail, (uint8_t)(t + 1));
		conceal(spk);
	}
}

// The mic statistics (ambe_voice_stats_t.tx_mic_*): histograms of the blocks' levels and zero crossings, the audio
// callback only
#define MIC_LEVEL_BINS	32u	// floor(log2(mean square)): 3 dB a bin
#define MIC_ZC_BINS	21u	// zero crossings / 8
static uint16_t mic_level_hist[MIC_LEVEL_BINS];
static uint16_t mic_zc_hist[MIC_ZC_BINS];
static int16_t mic_prev;		// the last sample of the previous block

// The bin holding the given fraction (per 1000) of the counts
static uint32_t hist_at(const uint16_t *h, uint32_t bins, uint32_t total, uint32_t permille)
{
	uint32_t want = (total * permille + 999u) / 1000u, sum = 0;
	for (uint32_t i = 0; i < bins; i++) {
		sum += h[i];
		if (sum >= want && sum > 0) {
			return i;
		}
	}
	return bins - 1u;
}

static void mic_stats(const int16_t *mic)
{
	int32_t mean = 0;
	for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
		mean += mic[i];
	}
	mean /= AUDIO_BLOCK_SAMPLES;
	if (stats.tx_mic_blocks == 0) {
		mic_prev = mic[0];
	}
	// The level from the sample-to-sample difference: a first-order high pass (gain 2 sin(pi f / 8 kHz)) that leaves
	// mains hum and rumble 30 dB down and the voice band within about 6 dB of flat
	uint64_t e = 0;
	uint32_t zc = 0;
	bool neg = mic[0] < mean;
	for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
		int32_t v = mic[i] - mean, d = (int32_t)mic[i] - mic_prev;
		mic_prev = mic[i];
		e += (uint64_t)((int64_t)d * d);
		if ((v < 0) != neg) {
			neg = v < 0;
			zc++;
		}
	}
	uint32_t ms = (uint32_t)(e / AUDIO_BLOCK_SAMPLES), bin = 0;
	while (ms > 1u && bin < MIC_LEVEL_BINS - 1u) {
		ms >>= 1;
		bin++;
	}
	if (stats.tx_mic_blocks == 0) {	// a new transmission, or ambeVoiceResetStats
		memset(mic_level_hist, 0, sizeof mic_level_hist);
		memset(mic_zc_hist, 0, sizeof mic_zc_hist);
	}
	if (stats.tx_mic_blocks >= 0xFFFFu) {
		return;	// 21 minutes: the counters would wrap
	}
	mic_level_hist[bin]++;
	mic_zc_hist[zc / 8u < MIC_ZC_BINS ? zc / 8u : MIC_ZC_BINS - 1u]++;
	uint32_t n = ++stats.tx_mic_blocks;
	uint32_t loud = hist_at(mic_level_hist, MIC_LEVEL_BINS, n, 900u);
	stats.tx_mic_level_db = 3u * loud;
	stats.tx_mic_spread_db = 3u * (loud - hist_at(mic_level_hist, MIC_LEVEL_BINS, n, 100u));
	stats.tx_mic_zc = 8u * hist_at(mic_zc_hist, MIC_ZC_BINS, n, 500u) + 4u;
}

#if CONFIG_TX_MIC_LIMITER
static uint16_t tx_target_of(uint8_t profile)
{
	return profile < AMBE_TX_PROFILES ? LOAD(tx_target[profile]) : 0u;
}
#endif

#if CONFIG_MIC_CHECK
// dB in tenths of a gain, Q16
static int16_t gain_db10(uint32_t gain_q16)
{
	uint32_t q8 = gain_q16 >> 8;	// the ratio of two powers that fit: (gain / 256)^2 against 256^2
	return (int16_t)(tx_level_db10(q8 * q8) - tx_level_db10(256u * 256u));
}

static void check_set_phase(uint8_t phase)
{
	check_phase = phase;
	STORE(check_view.phase, phase);
}

// Encode the chain's output into the recording, as tx_block would (same options, the codec's AGC off with a target)
static void mic_check_record(const int16_t *pcm, uint16_t target)
{
	if (!voc_ok()) {
		check_set_phase(AMBE_CHECK_LIVE);
		return;
	}
	if (check_n == 0) {
		voc->reset();
		uint32_t opts = LOAD(enc_opts);
		if (target != 0) {
			opts &= ~VOCODER_OPT_AGC;
		}
		if (voc->set_options != NULL) {
			voc->set_options(opts);
		}
		cb_enc_opts_set = false;	// the next transmission sets its own again
	}
	uint8_t bits[VOCODER_BITS];
	voc->encode(pcm, bits);
	uint8_t *f = check_frames[check_n];
	memset(f, 0, 7);
	for (unsigned b = 0; b < VOCODER_BITS; b++) {
		f[b >> 3] |= (uint8_t)((bits[b] & 1u) << (7u - (b & 7u)));
	}
	check_n++;
	STORE(check_view.frames, check_n);
	if (check_n >= AMBE_MIC_CHECK_FRAMES) {
		STORE(check_play_req, (uint8_t)(LOAD(check_play_req) + 1u));	// full: play it
	}
}

// Record or playback owns the speaker block: true when it did (receive is muted meanwhile)
static bool mic_check_spk(int16_t *spk)
{
	if (!check_running) {
		return false;
	}
	uint8_t play = LOAD(check_play_req);
	if (play != check_play_seen) {
		check_play_seen = play;
		if (check_phase == AMBE_CHECK_RECORD && check_n > 0 && voc_ok()) {
			check_set_phase(AMBE_CHECK_PLAY);
			check_i = 0;
			check_dec_power = 0;
			check_dec_blocks = check_dec_peak = 0;
			voc->reset();
		}
	}
	if (check_phase == AMBE_CHECK_LIVE) {
		return false;
	}
	if (check_phase == AMBE_CHECK_RECORD) {
		memset(spk, 0, AUDIO_BLOCK_SAMPLES * sizeof(int16_t));
		return true;
	}
	uint8_t bits[VOCODER_BITS];
	const uint8_t *f = check_frames[check_i];
	for (unsigned b = 0; b < VOCODER_BITS; b++) {
		bits[b] = (uint8_t)((f[b >> 3] >> (7u - (b & 7u))) & 1u);
	}
	voc->decode(bits, spk);
	speaker(true);
	tx_level_meter_t m = tx_level_measure(spk, AUDIO_BLOCK_SAMPLES, true);
	if (m.peak > check_dec_peak) {
		check_dec_peak = m.peak;
	}
	if (tx_level_db10(m.power) > AMBE_MIC_ACTIVE_DB10) {
		check_dec_power += m.power / AMBE_MIC_CHECK_FRAMES;
		check_dec_blocks++;
	}
	check_i++;
	STORE(check_view.frames, (uint8_t)(check_n - check_i));
	if (check_i >= check_n) {
		uint32_t power = check_dec_blocks != 0 ? check_dec_power / check_dec_blocks * AMBE_MIC_CHECK_FRAMES : 0u;
		STORE(check_view.dec_rms_db10, tx_level_db10(power));
		STORE(check_view.dec_peak_db10, tx_level_db10((uint32_t)check_dec_peak * check_dec_peak));
		STORE(check_view.played, (uint16_t)(check_view.played + 1u));
		check_set_phase(AMBE_CHECK_LIVE);
		cb_dec_fresh = true;	// the next call's decoder starts fresh
		if (!cb_rx_playing && !cb_mixing) {
			speaker(false);
		}
	}
	return true;
}

// A block heard while not transmitting: the transmit chain without the encoder (ambeVoice.h, mic check)
static void mic_check_block(const int16_t *mic)
{
	if (!LOAD(check_on)) {
		if (check_running && check_phase != AMBE_CHECK_LIVE && !cb_rx_playing) {
			speaker(false);
		}
		check_running = false;
		check_set_phase(AMBE_CHECK_LIVE);
		return;
	}
	uint8_t profile = LOAD(check_profile);
	uint32_t gain = (uint32_t)LOAD(mic_gain_q8) << 8;
	uint16_t target = tx_target_of(profile);
	uint8_t gen = LOAD(check_gen);
	if (gen != check_gen_seen) {
		check_gen_seen = gen;
		check_running = false;
	}
	if (!check_running) {
		check_running = true;
		tx_level_reset(&check_level, gain);
		check_blocks = 0;
		check_in_peak = check_out_peak = check_clips = 0;
		check_in_power = check_out_power = 0;
		check_limited0 = 0;
		STORE(check_view.windows, 0u);
		STORE(check_view.played, 0u);
		check_set_phase(AMBE_CHECK_LIVE);
		check_rec_seen = LOAD(check_rec_req);
		check_play_seen = LOAD(check_play_req);
	}
	uint8_t rec = LOAD(check_rec_req);
	if (rec != check_rec_seen) {
		check_rec_seen = rec;
		if (check_phase == AMBE_CHECK_LIVE) {
			check_n = 0;
			STORE(check_view.frames, 0u);
			check_set_phase(AMBE_CHECK_RECORD);
		}
	}
	tx_level_meter_t in;
	uint16_t out_peak = tx_level_process(&check_level, mic, scaled, AUDIO_BLOCK_SAMPLES, gain, target, &in);
	if (target == 0) {
		in = tx_level_measure(mic, AUDIO_BLOCK_SAMPLES, true);	// the manual path measures only the peak
	}
	for (unsigned i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
		if (mic[i] >= AMBE_MIC_CLIP || mic[i] <= -AMBE_MIC_CLIP) {
			check_clips++;
		}
	}
	if (in.peak > check_in_peak) {
		check_in_peak = in.peak;
	}
	if (out_peak > check_out_peak) {
		check_out_peak = out_peak;
	}
	check_in_power += in.power / AMBE_MIC_CHECK_BLOCKS;
	check_out_power += tx_level_measure(scaled, AUDIO_BLOCK_SAMPLES, true).power / AMBE_MIC_CHECK_BLOCKS;
	if (check_phase == AMBE_CHECK_RECORD && check_n < AMBE_MIC_CHECK_FRAMES) {
		mic_check_record(scaled, target);
	}
	if (++check_blocks < AMBE_MIC_CHECK_BLOCKS) {
		return;
	}
	STORE(check_view.in_peak_db10, tx_level_db10((uint32_t)check_in_peak * check_in_peak));
	STORE(check_view.in_rms_db10, tx_level_db10(check_in_power));
	STORE(check_view.out_peak_db10, tx_level_db10((uint32_t)check_out_peak * check_out_peak));
	STORE(check_view.out_rms_db10, tx_level_db10(check_out_power));
	STORE(check_view.gain_db10, gain_db10(check_level.gain_q16));
	STORE(check_view.clips, check_clips);
	STORE(check_view.limited, (uint16_t)(check_level.limited_blocks - check_limited0));
	STORE(check_view.target_rms, target);
	STORE(check_view.profile, profile);
	STORE(check_view.windows, check_view.windows + 1u);
	check_limited0 = check_level.limited_blocks;
	check_blocks = 0;
	check_in_peak = check_out_peak = check_clips = 0;
	check_in_power = check_out_power = 0;
}
#endif

static void tx_block(const int16_t *mic)
{
	uint8_t seq = LOAD(tx_seq);
	cb_tx_on = LOAD(tx_end_seq) != seq;
	if (!cb_tx_on) {
#if CONFIG_MIC_CHECK
		mic_check_block(mic);
#endif
		return;	// not transmitting
	}
#if CONFIG_MIC_CHECK
	if (check_running && check_phase == AMBE_CHECK_PLAY) {
		cb_dec_fresh = true;
	}
	check_running = false;	// a check after the transmission starts its windows again
	check_set_phase(AMBE_CHECK_LIVE);
#endif
	if (seq != cb_tx_seq) {
		cb_tx_seq = seq;
		stats.tx_mic_level_db = stats.tx_mic_spread_db = stats.tx_mic_zc = stats.tx_mic_blocks = 0;
		if (voc_ok()) {
			voc->reset();	// fresh encoder: the first frame of a transmission
		}
		stats.tx_mic_peak = stats.tx_enc_peak = stats.tx_limited_blocks = 0;
		stats.tx_mic_muted = 0;
		uint16_t gain_q8 = LOAD(mic_gain_q8);
		stats.tx_gain_q16 = (uint32_t)gain_q8 << 8;
#if CONFIG_TX_MIC_LIMITER
		tx_level_reset(&mic_level, stats.tx_gain_q16);
		stats.tx_mic_low_q8 = mic_low_q8 = gain_q8;
		stats.tx_profile = LOAD(tx_profile);
#endif
		tx_have_prev = false;
		test_blocks = 0;
#if CONFIG_SIG_TONES
		tone_active = tone_mute = false;	// the request below may be for this transmission
#endif
	}
#if CONFIG_SIG_TONES
	// A tone sequence asked for: it starts with this block. n == 0 cancels the one running
	uint8_t tseq = LOAD(tone_req_seq);
	if (tseq != cb_tone_seq) {
		cb_tone_seq = tseq;
		tone_mute = false;
		tone_active = tone_req_n != 0 && LOAD(tone_stale) != (int16_t)tseq;
		if (tone_active) {
			memcpy(tone_seg, tone_req_seg, tone_req_n * sizeof(beep_seg_t));
			beep_gen_start(&tone_gen, tone_seg, tone_req_n, TONE_AMP_Q15);
		}
	}
	// The queue is full (the call stack is in its headers, or behind): the tone waits for room rather than losing
	// its first frames to an overflow, which the mic's blocks would
	if (tone_active && (uint8_t)(tx_head - LOAD(tx_tail)) >= AMBE_VOICE_QUEUE) {
		return;
	}
#endif
	mic_stats(mic);	// the board's samples, before the gain scaling
	// Options change only between frames, in this task, so both halves of a frame see the same ones. A tone is sent
	// as a tone frame, and the noise reduction (which would take a steady tone for noise) is off for it
	uint32_t opts = LOAD(enc_opts);
#if CONFIG_TX_MIC_LIMITER
	uint16_t target = tx_target_of((uint8_t)stats.tx_profile);
	stats.tx_target_rms = target;
	if (target != 0) {
		opts &= ~VOCODER_OPT_AGC;	// the leveler sets the level: the codec's AGC would undo it
	}
#endif
#if CONFIG_SIG_TONES
	if (tone_active) {
		opts = (opts | VOCODER_OPT_TONE_FRAMES) & ~VOCODER_OPT_NOISE_REDUCTION;
	}
#endif
	if (voc_ok() && voc->set_options != NULL && (!cb_enc_opts_set || opts != cb_enc_opts)) {
		voc->set_options(opts);
		cb_enc_opts = opts;
		cb_enc_opts_set = true;
	}
	uint16_t mic_peak = tx_level_measure(mic, AUDIO_BLOCK_SAMPLES, false).peak;
	if (mic_peak > stats.tx_mic_peak) {
		stats.tx_mic_peak = mic_peak;
	}
	uint8_t bits[VOCODER_BITS];
	// A beep on the speaker is in the mic's samples too: the block goes out as silence. Not a block the test signal or
	// a signaling tone replaces (below): that one isn't the mic's, so it isn't counted either
	bool replaced = LOAD(test_signal);
#if CONFIG_SIG_TONES
	replaced = replaced || tone_active || tone_mute;
#endif
	ambe_voice_mic_gate_t gate = LOAD(mic_gate);
	bool gated = false;
	if (!replaced && gate != NULL && gate()) {
		memset(scaled, 0, sizeof(scaled));
		mic = scaled;
		stats.tx_mic_muted++;
		gated = true;
	}
	if (LOAD(test_signal)) {
		test_block(scaled);
		mic = scaled;
	}
#if CONFIG_SIG_TONES
	// The tone's last block: tone_active stays set until its frame is queued and tone_last_* published below, so
	// source_busy() never reads false with the end tone's frames still to go. A full queue drops this frame and the
	// block is made again next time (the generator is spent: silence)
	bool tone_last = false;
	if (tone_active) {
		bool more = beep_gen_block(&tone_gen, scaled, AUDIO_BLOCK_SAMPLES);
		mic = scaled;
		tone_last = !more;
	} else if (tone_mute) {
		memset(scaled, 0, sizeof(scaled));
		mic = scaled;
	}
#endif
	uint16_t enc_peak;
#if CONFIG_SIG_TONES
	if (tone_active || tone_mute) {
		// Generated signaling bypasses mic gain; discarded microphone blocks must not alter recovery or counts.
		stats.tx_gain_q16 = TX_LEVEL_UNITY;
		enc_peak = tx_level_measure(mic, AUDIO_BLOCK_SAMPLES, false).peak;
	} else
#endif
	if (gated) {
		// Silence for a beep's echo: the limiter's gain, its lowest-gain record and the limited count stay as they
		// were, so the speech after the beep starts at the gain it had before it
		enc_peak = 0;
	} else {
		uint16_t gain_q8 = LOAD(mic_gain_q8);
		stats.tx_gain_q16 = (uint32_t)gain_q8 << 8;
#if CONFIG_TX_MIC_LIMITER
		enc_peak = tx_level_process(&mic_level, mic, scaled, AUDIO_BLOCK_SAMPLES, stats.tx_gain_q16, target, NULL);
		stats.tx_gain_q16 = mic_level.gain_q16;
		stats.tx_limited_blocks = mic_level.limited_blocks;
		uint16_t applied_q8 = (uint16_t)(mic_level.gain_q16 >> 8);
		if (applied_q8 < mic_low_q8) {
			mic_low_q8 = applied_q8;
		}
		stats.tx_mic_low_q8 = mic_low_q8;
		mic = scaled;
#else
		if (gain_q8 != 256) {
			for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
				scaled[i] = fxp_sat16((int32_t)mic[i] * gain_q8 / 256);
			}
			mic = scaled;
		}
		enc_peak = tx_level_measure(mic, AUDIO_BLOCK_SAMPLES, false).peak;
#endif
	}
	if (enc_peak > stats.tx_enc_peak) {
		stats.tx_enc_peak = enc_peak;
	}
	if (voc_ok()) {
		voc->encode(mic, bits);
	} else {
		memset(bits, 0, sizeof bits);
	}
	if (tx_have_prev && memcmp(bits, tx_prev, VOCODER_BITS) == 0) {
		stats.tx_repeats++;
	}
	memcpy(tx_prev, bits, VOCODER_BITS);
	tx_have_prev = true;
	uint8_t h = tx_head;
	if ((uint8_t)(h - LOAD(tx_tail)) >= AMBE_VOICE_QUEUE) {
		stats.tx_overflows++;
		return;
	}
	tx_entry_t *e = &txq[h & (AMBE_VOICE_QUEUE - 1)];
	e->seq = seq;
	ambeFecEncode(bits, e->frame);
	STORE(tx_head, (uint8_t)(h + 1));
#if CONFIG_SIG_TONES
	tx_pushed++;
	if (tone_last) {
		tone_last_n = tx_pushed;
		STORE(tone_last_seq, seq);
		STORE(tone_last_valid, true);
		tone_mute = tone_req_final;
		tone_active = false;
	}
#endif
	stats.tx_frames++;
}

void ambeVoiceAudioBlock(const int16_t *mic, int16_t *spk, void *ctx)
{
	(void)ctx;
	// DMR is simplex: TX first (a fresh transmission resets the vocoder), then RX playback (silent while transmitting)
	tx_block(mic);
#if CONFIG_MIC_CHECK
	if (mic_check_spk(spk)) {
		if (LOAD(cb_speaker_unsent)) {
			speaker(cb_speaker);
		}
		return;	// the check's recording or playback has the speaker
	}
#endif
#if CONFIG_RX_AGC
	cb_rx_good = false;
#endif
	rx_block(spk);
#if CONFIG_RX_AGC
	if (agc.started) {
		if (!cb_rx_live || cb_tx_on)
			rx_agc_end(&agc);
		else if (cb_rx_playing)
			rx_agc_process(&agc, spk, cb_rx_good);
	}
#endif
	ambe_voice_mixer_t mix = LOAD(mixer);
	if (mix != NULL && mix(spk)) {
		cb_mixing = true;
		speaker(true);
	} else if (cb_mixing) {
		cb_mixing = false;
		if (!cb_rx_playing) {
			speaker(false);
		}
	}
	if (LOAD(cb_speaker_unsent)) {
		speaker(cb_speaker);
	}
}

// --- set-up ---

void ambeVoiceSetMicGainQ8(uint16_t q8)
{
	STORE(mic_gain_q8, q8);
}

void ambeVoiceSetTxTarget(ambe_tx_profile_t profile, uint16_t target_rms)
{
#if CONFIG_TX_MIC_LIMITER
	if (profile < AMBE_TX_PROFILES) {
		STORE(tx_target[profile], target_rms);
	}
#else
	(void)profile;
	(void)target_rms;
#endif
}

uint16_t ambeVoiceTxTarget(ambe_tx_profile_t profile)
{
#if CONFIG_TX_MIC_LIMITER
	return tx_target_of((uint8_t)profile);
#else
	(void)profile;
	return 0;
#endif
}

void ambeVoiceTxProfile(ambe_tx_profile_t profile)
{
#if CONFIG_TX_MIC_LIMITER
	STORE(tx_profile_next, (uint8_t)(profile < AMBE_TX_PROFILES ? profile : AMBE_TX_DMR));
#else
	(void)profile;
#endif
}

void ambeVoiceMicCheck(bool on, ambe_tx_profile_t profile)
{
#if CONFIG_MIC_CHECK
	STORE(check_profile, (uint8_t)(profile < AMBE_TX_PROFILES ? profile : AMBE_TX_DMR));
	if (on && !LOAD(check_on)) {
		STORE(check_gen, (uint8_t)(LOAD(check_gen) + 1u));
	}
	if (!on) {
		STORE(check_view.phase, (uint8_t)AMBE_CHECK_LIVE);	// the stream may stop before a block sees it off
	}
	STORE(check_view.on, (uint8_t)on);
	STORE(check_on, (uint8_t)on);
#else
	(void)on;
	(void)profile;
#endif
}

void ambeVoiceMicCheckView(ambe_mic_check_t *out)
{
#if CONFIG_MIC_CHECK
	out->windows = LOAD(check_view.windows);
	out->in_peak_db10 = LOAD(check_view.in_peak_db10);
	out->in_rms_db10 = LOAD(check_view.in_rms_db10);
	out->out_peak_db10 = LOAD(check_view.out_peak_db10);
	out->out_rms_db10 = LOAD(check_view.out_rms_db10);
	out->gain_db10 = LOAD(check_view.gain_db10);
	out->clips = LOAD(check_view.clips);
	out->limited = LOAD(check_view.limited);
	out->target_rms = LOAD(check_view.target_rms);
	out->profile = LOAD(check_view.profile);
	out->on = LOAD(check_view.on);
	out->phase = LOAD(check_view.phase);
	out->frames = LOAD(check_view.frames);
	out->played = LOAD(check_view.played);
	out->dec_rms_db10 = LOAD(check_view.dec_rms_db10);
	out->dec_peak_db10 = LOAD(check_view.dec_peak_db10);
#else
	memset(out, 0, sizeof *out);
#endif
}

void ambeVoiceMicCheckRecord(void)
{
#if CONFIG_MIC_CHECK
	STORE(check_rec_req, (uint8_t)(LOAD(check_rec_req) + 1u));
#endif
}

void ambeVoiceMicCheckPlay(void)
{
#if CONFIG_MIC_CHECK
	STORE(check_play_req, (uint8_t)(LOAD(check_play_req) + 1u));
#endif
}

void ambeVoiceSetMicGain(uint8_t step)
{
	ambeVoiceSetMicGainQ8(mic_gain_step_q8(step));
}

void ambeVoiceSetEncodeOptions(uint32_t opts)
{
	STORE(enc_opts, opts);
}

uint32_t ambeVoiceEncodeOptions(void)
{
	return LOAD(enc_opts) & ambeVoiceEncodeOptionsSupported();
}

uint32_t ambeVoiceEncodeOptionsSupported(void)
{
	return voc != NULL && voc->options != NULL && voc->set_options != NULL ? voc->options() : 0;
}

static const dmr_voice_sink_t sink = {
	.begin = sink_begin, .frame = sink_frame, .lost = sink_lost, .end = sink_end, .ctx = NULL,
};
static const dmr_voice_source_t source = {
	.begin = source_begin, .next = source_next, .end = source_end,
#if CONFIG_SIG_TONES
	.busy = source_busy,
#endif
	.ctx = NULL,
};

void ambeVoiceInit(const hal_audio_t *audio, const hal_vocoder_t *vocoder)
{
	aud = audio;
	voc = vocoder;
	cb_enc_opts_set = false;	// a new vocoder is told the options before its first frame
}

const dmr_voice_sink_t *ambeVoiceSink(void)
{
	return &sink;
}

const dmr_voice_source_t *ambeVoiceSource(void)
{
	return &source;
}

void ambeVoiceStop(void)
{
	STORE(rx_end_seq, rx_seq);
	STORE(tx_end_seq, tx_seq);
	if (streaming && aud != NULL) {
		aud->stop();
		if (aud->speaker_pcm != NULL) {
			aud->speaker_pcm(false);
		}
	}
	streaming = false;
	cb_enc_opts_set = false; // another local codec user may change HAL options while stopped
#if CONFIG_RX_AGC
	rx_agc_end(&agc);
	ambeVoiceRxIdentity(RX_AGC_DMR, 0, 0);
#endif
}

void ambeVoiceStart(void)
{
	ensure_streaming();
}

bool ambeVoiceStreaming(void)
{
	return streaming;
}

void ambeVoiceSetMixer(ambe_voice_mixer_t m)
{
	STORE(mixer, m);
}

void ambeVoiceSetMicGate(ambe_voice_mic_gate_t g)
{
	STORE(mic_gate, g);
}

#if CONFIG_SIG_TONES
void ambeVoiceTxTone(const beep_seg_t *steps, uint8_t n, bool final)
{
	if (n > SIG_AIR_STEPS) {
		n = SIG_AIR_STEPS;
	}
	if (steps != NULL && n != 0) {
		memcpy(tone_req_seg, steps, n * sizeof(beep_seg_t));
	}
	tone_req_n = steps != NULL ? n : 0;
	tone_req_final = final;
	STORE(tone_last_valid, false);
	STORE(tone_req_seq, (uint8_t)(tone_req_seq + 1));
}

void ambeVoiceTxToneCancel(void)
{
	ambeVoiceTxTone(NULL, 0, false);
}
#endif

void ambeVoiceSetTestSignal(bool on)
{
	STORE(test_signal, on);
}

bool ambeVoiceTestSignal(void)
{
	return LOAD(test_signal);
}

void ambeVoiceResetStats(void)
{
	memset(&stats, 0, sizeof(stats));
}

void ambeVoiceGetStats(ambe_voice_stats_t *out)
{
	*out = stats;
}
