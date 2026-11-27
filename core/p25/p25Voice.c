// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 David Behnke
// P25 voice: engine frames <-> full-rate decoder <-> PCM blocks (p25Voice.h)
#include "config.h"
#include "dsp/mic_gain.h"
#if CONFIG_P25 && CONFIG_IMBE	// the whole file
#include "p25Voice.h"

#include <stddef.h>
#include <string.h>
#include "dsp/fxp.h"
#include "dsp/tx_level.h"
#include "dsp/dc_block.h"
#if CONFIG_P25_PARROT
#include "dsp/speech_meter.h"
#endif


// the call layer packs what a frame's codes corrected as the decoder reads it
_Static_assert(P25_VOICE_ERRS(11, 2, 1) == IMBE_ERRS(11, 2, 1) && P25_VOICE_ERRS(15, 3, 1) != IMBE_ERRS_LOST,
	       "the voice error byte's layouts differ");

#if (P25_VOICE_QUEUE & (P25_VOICE_QUEUE - 1)) != 0 || P25_VOICE_QUEUE > 128 || P25_VOICE_QUEUE < 2 * P25_VOICE_FRAMES
#error "P25_VOICE_QUEUE must be a power of two up to 128 holding two LDUs"
#endif
#if (P25_VOICE_TX_QUEUE & (P25_VOICE_TX_QUEUE - 1)) != 0 || P25_VOICE_TX_QUEUE > 128
#error "P25_VOICE_TX_QUEUE must be a power of two up to 128"
#endif

#define LOAD(x)		__atomic_load_n(&(x), __ATOMIC_ACQUIRE)
#define STORE(x, v)	__atomic_store_n(&(x), (v), __ATOMIC_RELEASE)

static const hal_audio_t *aud;
static bool streaming;			// radio task only
static p25_voice_mem_t *mem;		// set before the stream starts, cleared after it stops
static imbe_mem_t dec;			// the decoder's pointers into mem
static imbe_enc_mem_t enc;		// the encoder's: the decoder's prediction state and scratch, its own history

// Queue: producer the provider's task (head), consumer the audio callback (tail)
static uint8_t rx_head, rx_tail;
// Call boundaries, written by the provider's task only: a start bumps the sequence, an end copies it
static uint8_t rx_seq, rx_end_seq;

// Transmit. The radio task counts transmissions (odd while one is on); the queue's producer is the audio callback
// (head), its consumer the provider's task (tail). Frames of an earlier transmission are dropped by their count
static uint8_t tx_gen;
static uint8_t tx_head, tx_tail;
static uint16_t tx_target;	// p25VoiceSetTxTarget: the leveling target (RMS), 0 = the manual mic gain
static tx_level_t input_level;
#if CONFIG_TX_DC_BLOCK
static dc_block_t input_dc;
#endif
static uint16_t mic_gain_q8 = 256;	// p25VoiceSetMicGain: the mic samples times this / 256 before encoding

// Audio callback state
static uint8_t cb_tx_gen;		// the transmission the encoder runs for (even: none)
static uint8_t cb_seq;
static bool cb_live, cb_primed, cb_playing, cb_speaker, cb_mixing;
// The stream just (re)started: the speaker route is what its last user left, which may be a beep's own stream that
// had it on. The first block sends cb_speaker whatever it was, or the route (and the board's amplifier) stays on
static bool cb_speaker_unsent;
static uint8_t cb_lead;			// silent blocks left before the first frame plays
static uint32_t cb_call_frames;

#if CONFIG_RX_AGC
static rx_agc_t agc;
static uint32_t rx_source;
static uint16_t rx_scope;
static bool cb_rx_good;
#endif
static p25_voice_stats_t stats;
static p25_voice_mixer_t mixer;
static p25_voice_mic_gate_t mic_gate;

// --- hooks (provider's task) ---

static void rx_start(const p25_call_t *c, void *ctx)
{
#if CONFIG_RX_AGC
	rx_source = c != NULL && c->have_lc ? c->lc.src : 0;
	rx_scope = c != NULL ? c->nac : 0;
#else
	(void)c;
#endif
	(void)ctx;
	STORE(rx_seq, (uint8_t)(rx_seq + 1));
}

static void rx_update(const p25_call_t *c, void *ctx)
{
#if CONFIG_RX_AGC
	rx_source = c != NULL && c->have_lc ? c->lc.src : 0;
	rx_scope = c != NULL ? c->nac : 0;
#else
	(void)c;
#endif
	(void)ctx;
}

static void rx_voice(const uint8_t imbe[P25_IMBE_BYTES], uint8_t errs, void *ctx)
{
	(void)ctx;
	if (rx_end_seq == rx_seq) {
		rx_start(NULL, NULL);	// frames with no call open: a new call
	}
	stats.rx_in++;
	if (IMBE_ERRS_REPEAT(errs)) {
		stats.rx_bad++;
	} else {
		stats.rx_bit_errors += IMBE_ERRS_TOTAL(errs);
	}
	uint8_t h = rx_head, used = (uint8_t)(h - LOAD(rx_tail));
	if (used >= P25_VOICE_QUEUE) {
		stats.rx_overflows++;
		return;
	}
	if (used + 1u > stats.rx_queue_max) {
		stats.rx_queue_max = used + 1u;
	}
	p25_voice_entry_t *e = &mem->q[h & (P25_VOICE_QUEUE - 1)];
#if CONFIG_RX_AGC
	e->source = rx_source;
	e->scope = rx_scope;
#endif
	e->seq = rx_seq;
	e->errs = errs;
	memcpy(e->imbe, imbe, P25_IMBE_BYTES);
	STORE(rx_head, (uint8_t)(h + 1));
}

static void rx_end(void *ctx)
{
	(void)ctx;
	STORE(rx_end_seq, rx_seq);
}

// The encoder's next frame (false: none ready; the engine waits while the air can spare it, then sends silence and
// counts it as filler)
static bool tx_voice(uint8_t imbe[P25_IMBE_BYTES], void *ctx)
{
	(void)ctx;
	uint8_t g = LOAD(tx_gen), t = tx_tail, h = LOAD(tx_head);

	while (t != h && mem->tq[t & (P25_VOICE_TX_QUEUE - 1)].gen != g) {
		t++;	// left over from an earlier transmission
	}
	if (t == h) {
		STORE(tx_tail, t);
		return false;
	}
	memcpy(imbe, mem->tq[t & (P25_VOICE_TX_QUEUE - 1)].imbe, P25_IMBE_BYTES);
	STORE(tx_tail, (uint8_t)(t + 1));
	stats.tx_frames++;
	return true;
}

static const p25_engine_hooks_t hooks = { .rx_start = rx_start, .rx_update = rx_update, .rx_voice = rx_voice,
					   .rx_end = rx_end, .tx_voice = tx_voice };

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

static void silence(int16_t *spk)
{
	memset(spk, 0, AUDIO_BLOCK_SAMPLES * sizeof(int16_t));
}

static void rx_block(int16_t *spk)
{
	uint8_t seq = LOAD(rx_seq);
	if (seq != cb_seq) {
		// A new call: the decoder starts over. The speaker route may be off (an end, a stop): sent again in any case
#if CONFIG_RX_AGC
		rx_agc_end(&agc);
#endif
		cb_seq = seq;
		cb_live = true;
		cb_primed = cb_playing = cb_speaker = false;
		cb_lead = 0;
		cb_call_frames = 0;
		stats.rx_peak = 0;
		imbe_dec_init(&dec);
	}
	// Drop entries of earlier calls
	uint8_t t = rx_tail, h = LOAD(rx_head);
	while (t != h && mem->q[t & (P25_VOICE_QUEUE - 1)].seq != cb_seq) {
		t++;
	}
	STORE(rx_tail, t);

	bool ended = LOAD(rx_end_seq) == cb_seq;
	uint8_t queued = (uint8_t)(h - t);
#if CONFIG_RX_AGC
	if (queued != 0) {
		const p25_voice_entry_t *first = &mem->q[t & (P25_VOICE_QUEUE - 1)];
		if (!agc.started)
			rx_agc_begin(&agc, RX_AGC_P25, first->source, first->scope);
		else
			rx_agc_identify(&agc, first->source, first->scope);
	}
#endif
	if (!cb_live) {
		silence(spk);
		return;
	}
	if (!cb_primed) {
		if (queued < P25_VOICE_PRIME && !(ended && queued > 0)) {
			if (ended) {
				cb_live = false;	// a call that ended before playing anything
			}
			silence(spk);
			return;
		}
		cb_primed = cb_playing = true;
		cb_lead = P25_VOICE_LEAD;
		stats.rx_calls++;
		speaker(true);
	}
	if (cb_lead != 0) {
		cb_lead--;
		silence(spk);
		return;
	}
	if (queued == 0) {
		if (ended) {
			cb_live = cb_playing = false;
			speaker(false);
			silence(spk);
			return;
		}
		// Nothing in time: the decoder's bad-frame path (repeat the last good frame, then mute)
		stats.rx_underruns++;
		stats.rx_underrun_at = cb_call_frames;
		stats.rx_concealed++;
		imbe_dec_frame(&dec, imbe_silence, IMBE_ERRS_LOST, spk);
		return;
	}
	const p25_voice_entry_t *e = &mem->q[t & (P25_VOICE_QUEUE - 1)];
	int run = imbe_dec_frame(&dec, e->imbe, e->errs, spk);
	STORE(rx_tail, (uint8_t)(t + 1));
	if (run != 0) {
		stats.rx_concealed++;
		return;
	}
#if CONFIG_RX_AGC
	cb_rx_good = true;
#endif
	stats.rx_frames++;
	cb_call_frames++;
	for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
		uint32_t a = (uint32_t)(spk[i] < 0 ? -spk[i] : spk[i]);
		if (a > stats.rx_peak) {
			stats.rx_peak = a;
		}
	}
}

static uint16_t input_block(const int16_t *mic, int16_t *scaled, uint16_t gain, uint16_t target,
			    tx_level_meter_t *input, bool remove_dc, tx_level_meter_t *conditioned)
{
	if (input != NULL) *input = tx_level_measure(mic, AUDIO_BLOCK_SAMPLES, true);
#if CONFIG_TX_DC_BLOCK
	if (remove_dc) {
		dc_block_process(&input_dc, mic, scaled, AUDIO_BLOCK_SAMPLES);
		mic = scaled;
	}
#else
	(void)remove_dc;
#endif
	if (conditioned != NULL) *conditioned = tx_level_measure(mic, AUDIO_BLOCK_SAMPLES, true);
	if (CONFIG_TX_MIC_LIMITER || target != 0) {
		return tx_level_process(&input_level, mic, scaled, AUDIO_BLOCK_SAMPLES, (uint32_t)gain << 8, target, NULL);
	}
	input_level.gain_q16 = (uint32_t)gain << 8;
	uint16_t peak = 0;
	for (unsigned i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
		int32_t v = fxp_sat16(((int32_t)mic[i] * gain) / 256);
		uint32_t a = (uint32_t)(v < 0 ? -v : v);
		if (a > peak) peak = (uint16_t)a;
		scaled[i] = (int16_t)v;
	}
	return peak;
}


#if CONFIG_P25_PARROT
#define PARROT_FRAMES 500u
static uint8_t (*parrot_frames)[IMBE_BYTES], (*parrot_extra)[IMBE_BYTES];
static unsigned parrot_split;
static int16_t *cal_pcm;
static unsigned cal_samples;
static bool calibrating;
static uint8_t cal_stage, cal_pass, cal_startup;
static uint16_t cal_index;
static uint8_t cal_active[P25_CAL_MAX_BLOCKS];
static uint64_t cal_encoder_power, cal_decoder_power;
static uint16_t cal_active_count, cal_measure_count, cal_limited;
static const int8_t cal_targets[P25_CAL_PASSES - 1] = { -35, -30, -25, -20, -15 };

static uint8_t parrot_state, parrot_command;
static bool parrot_agc, parrot_stop;
static uint8_t parrot_lead;
static uint16_t parrot_target;
static uint16_t parrot_gain_q8;
static uint16_t parrot_progress;
static p25_parrot_view_t parrot_view = { .input_db10 = -960, .encoder_db10 = -960, .output_db10 = -960,
	.mic_ac_db10 = -960, .speech_db10 = { -960, -960, -960 } };
static uint64_t parrot_power[3], parrot_ac_power, parrot_conditioned_power;
static int64_t parrot_mic_sum;
static uint32_t parrot_speech_power[3];
static uint16_t parrot_speech_blocks[3];
static uint16_t parrot_count, parrot_played;

static uint8_t *parrot_frame(unsigned n)
{
	return n < parrot_split ? parrot_frames[n] : parrot_extra[n - parrot_split];
}

static void parrot_view_reset(void)
{
	STORE(parrot_view.mode, CODEC_DIAG_P25);
	STORE(parrot_view.dc_block, false);
	STORE(parrot_view.conditioned_db10, -960);
	STORE(parrot_view.codec_options, 0);
	STORE(parrot_view.codec_flags, 0);
	STORE(parrot_view.cal_status, P25_CAL_IDLE);
	STORE(parrot_view.cal_pass, 0);
	STORE(parrot_view.suggested_db10, -960);
	STORE(parrot_view.capture_limit, 0);
	STORE(parrot_view.startup_rail_samples, 0);
	for (unsigned i = 0; i < P25_CAL_PASSES; i++) {
		STORE(parrot_view.cal[i].target_db10, -960);
		STORE(parrot_view.cal[i].encoder_db10, -960);
		STORE(parrot_view.cal[i].decoded_db10, -960);
		STORE(parrot_view.cal[i].active_blocks, 0);
		STORE(parrot_view.cal[i].limited_blocks, 0);
		STORE(parrot_view.cal[i].gain_q16, TX_LEVEL_UNITY);
	}
	STORE(parrot_view.mic_mean, 0);
	STORE(parrot_view.mic_dc_peak, 0);
	STORE(parrot_view.mic_ac_db10, -960);
	STORE(parrot_view.input_rail_samples, 0);
	for (unsigned i = 0; i < 3; i++) {
		STORE(parrot_view.speech_db10[i], -960);
		STORE(parrot_view.speech_blocks[i], 0);
	}
	STORE(parrot_view.frames, 0);
	STORE(parrot_view.input_db10, -960);
	STORE(parrot_view.encoder_db10, -960);
	STORE(parrot_view.output_db10, -960);
	STORE(parrot_view.input_peak, 0);
	STORE(parrot_view.encoder_peak, 0);
	STORE(parrot_view.output_peak, 0);
	STORE(parrot_view.gain_q16, TX_LEVEL_UNITY);
	STORE(parrot_view.manual_gain_q16, TX_LEVEL_UNITY);
	STORE(parrot_view.limited_blocks, 0);
	STORE(parrot_view.agc, false);
	STORE(parrot_view.limiter, false);
	STORE(parrot_view.target_dbfs, -25);
	STORE(parrot_view.mic_step, 5);
}

static void parrot_meter(unsigned index, tx_level_meter_t m)
{
	parrot_power[index] += m.power;
	uint16_t *peak = index == 0 ? &parrot_view.input_peak : index == 1 ? &parrot_view.encoder_peak : &parrot_view.output_peak;
	if (m.peak > LOAD(*peak)) STORE(*peak, m.peak);
	unsigned frames = index == 2 ? parrot_played : parrot_count;
	int16_t db = tx_level_db10((uint32_t)(parrot_power[index] / frames));
	if (index == 0) STORE(parrot_view.input_db10, db);
	else if (index == 1) STORE(parrot_view.encoder_db10, db);
	else STORE(parrot_view.output_db10, db);
}


static void parrot_speech(unsigned index, const int16_t *pcm)
{
	uint32_t power = speech_power(pcm, AUDIO_BLOCK_SAMPLES, NULL);
	if (index == 0) {
		int32_t sum = 0;
		uint32_t rails = LOAD(parrot_view.input_rail_samples);
		for (unsigned i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
			sum += pcm[i];
			if (pcm[i] == INT16_MIN || pcm[i] == INT16_MAX) rails++;
		}
		parrot_mic_sum += sum;
		int32_t mean = sum / AUDIO_BLOCK_SAMPLES;
		uint16_t dc = (uint16_t)(mean < 0 ? -mean : mean);
		if (dc > LOAD(parrot_view.mic_dc_peak)) STORE(parrot_view.mic_dc_peak, dc);
		STORE(parrot_view.mic_mean, (int16_t)(parrot_mic_sum / ((int32_t)parrot_count * AUDIO_BLOCK_SAMPLES)));
		STORE(parrot_view.input_rail_samples, rails);
		parrot_ac_power += power;
		STORE(parrot_view.mic_ac_db10, tx_level_db10((uint32_t)(parrot_ac_power / parrot_count)));
	}
	if (speech_level_q8(power) < SPEECH_FLOOR_Q8) return;
	unsigned blocks = parrot_speech_blocks[index];
	parrot_speech_power[index] = speech_average(parrot_speech_power[index], power, blocks);
	parrot_speech_blocks[index]++;
	STORE(parrot_view.speech_blocks[index], parrot_speech_blocks[index]);
	STORE(parrot_view.speech_db10[index], (int16_t)(speech_level_q8(parrot_speech_power[index]) * 10 / 256));
}

// Whole-sample energy, gated on the same conditioned-input mask for every candidate and both codec sides.
// Pad activity by one block on each side for the analysis window and synthesis overlap; this is not a VAD.
static bool cal_mask(unsigned n)
{
	return cal_active[n] || cal_active[(n + parrot_count - 1u) % parrot_count] || cal_active[(n + 1u) % parrot_count];
}

static uint16_t parrot_rms(int target)
{
	static const uint16_t rms[] = { 583, 654, 734, 823, 924, 1036, 1163, 1305, 1464, 1642, 1843, 2068, 2320, 2603, 2920, 3277, 3677, 4125, 4629, 5193, 5827 };
	return rms[target + 35];
}

static void cal_begin_pass(int target)
{
	STORE(parrot_view.cal[cal_pass].target_db10, target * 10);
	parrot_target = parrot_rms(target);
	cal_stage = 1;
	cal_index = cal_active_count = cal_measure_count = cal_limited = 0;
	cal_encoder_power = cal_decoder_power = 0;
	tx_level_reset(&input_level, TX_LEVEL_UNITY);
	imbe_enc_init(&enc);
}

static void cal_finish(uint8_t status)
{
	STORE(parrot_view.cal_status, status);
	// The last candidate's encoded frames survive for ordinary playback. Raw PCM never survives the sweep.
	memset(cal_pcm, 0, cal_samples * sizeof *cal_pcm);
	if (status != P25_CAL_OK) {
		speaker(false);
		STORE(parrot_state, P25_PARROT_DONE);
		return;
	}
	cal_stage = 3;
	parrot_played = 0;
	imbe_dec_init(&dec);
	parrot_lead = aud != NULL ? (aud->speaker_lead_ms + AUDIO_BLOCK_MS - 1u) / AUDIO_BLOCK_MS : 0;
	speaker(true);
}

static bool cal_usable(const p25_cal_result_t *r)
{
	return LOAD(r->active_blocks) != 0 && LOAD(r->limited_blocks) * 10u <= LOAD(r->active_blocks);
}

static void cal_next(void)
{
	p25_cal_result_t *r = &parrot_view.cal[cal_pass];
	STORE(r->encoder_db10, tx_level_db10((uint32_t)(cal_encoder_power / cal_measure_count)));
	STORE(r->decoded_db10, tx_level_db10((uint32_t)(cal_decoder_power / cal_measure_count)));
	STORE(r->active_blocks, cal_active_count);
	STORE(r->limited_blocks, cal_limited);
	STORE(r->gain_q16, input_level.gain_q16);
	STORE(parrot_view.cal_pass, cal_pass + 1u);
	if (cal_pass < P25_CAL_PASSES - 2u) {
		cal_pass++;
		cal_begin_pass(cal_targets[cal_pass]);
		return;
	}
	if (cal_pass == P25_CAL_PASSES - 2u) {
		// Prefer a measured target already within tolerance; otherwise interpolate an increasing bracket.
		int target = 0, closest = 32767;
		for (unsigned i = 0; i < P25_CAL_PASSES - 1u; i++) {
			int error = LOAD(parrot_view.cal[i].decoded_db10) + 250;
			if (error < 0) error = -error;
			if (cal_usable(&parrot_view.cal[i]) && error < closest) { closest = error; target = LOAD(parrot_view.cal[i].target_db10) / 10; }
		}
		if (closest > 10) {
			target = 0;
			for (unsigned i = 1; i < P25_CAL_PASSES - 1u; i++) {
				p25_cal_result_t *lo = &parrot_view.cal[i - 1u], *hi = &parrot_view.cal[i];
				int a = LOAD(lo->decoded_db10), b = LOAD(hi->decoded_db10);
				if (a <= -250 && b >= -250 && b > a) {
					int t = LOAD(lo->target_db10) + (-250 - a) * (LOAD(hi->target_db10) - LOAD(lo->target_db10)) / (b - a);
					target = (t - 5) / 10;
					break;
				}
			}
		}
		if (target != 0) {
			cal_pass++;
			cal_begin_pass(target);
			return;
		}
		cal_finish(P25_CAL_RANGE);
		return;
	}
	int error = LOAD(r->decoded_db10) + 250;
	if (error < -10 || error > 10) { cal_finish(P25_CAL_RANGE); return; }
	if (!cal_usable(r)) { cal_finish(P25_CAL_LIMITED); return; }
	STORE(parrot_view.suggested_db10, LOAD(r->target_db10));
	cal_finish(P25_CAL_OK);
}

static bool calibration_block(const int16_t *mic, int16_t *spk)
{
	silence(spk);
	if (p25VoiceParrotState() == P25_PARROT_RECORD) {
		if (cal_startup < 5u) {
			uint32_t rails = LOAD(parrot_view.startup_rail_samples);
			for (unsigned i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
				if (mic[i] == INT16_MIN || mic[i] == INT16_MAX) rails++;
			STORE(parrot_view.startup_rail_samples, rails);
#if CONFIG_TX_DC_BLOCK
			dc_block_process(&input_dc, mic, spk, AUDIO_BLOCK_SAMPLES);
#endif
			cal_startup++;
		} else if (parrot_count < LOAD(parrot_view.capture_limit) && !LOAD(parrot_stop)) {
			int16_t *pcm = cal_pcm + parrot_count * AUDIO_BLOCK_SAMPLES;
#if CONFIG_TX_DC_BLOCK
			dc_block_process(&input_dc, mic, pcm, AUDIO_BLOCK_SAMPLES);
#else
			memcpy(pcm, mic, AUDIO_BLOCK_SAMPLES * sizeof *pcm);
#endif
			cal_active[parrot_count] = speech_level_q8(speech_power(pcm, AUDIO_BLOCK_SAMPLES, NULL)) >= -50 * 256;
			parrot_count++;
			parrot_speech(0, mic);
			parrot_meter(0, tx_level_measure(mic, AUDIO_BLOCK_SAMPLES, true));
			parrot_conditioned_power += tx_level_measure(pcm, AUDIO_BLOCK_SAMPLES, true).power;
			STORE(parrot_view.conditioned_db10, tx_level_db10((uint32_t)(parrot_conditioned_power / parrot_count)));
			STORE(parrot_view.frames, parrot_count);
		} else {
			unsigned active = 0;
			for (unsigned i = 0; i < parrot_count; i++) active += cal_active[i];
			if (parrot_count < 50u || active < 25u || LOAD(parrot_view.input_rail_samples) != 0) {
				STORE(parrot_view.cal_status, LOAD(parrot_view.input_rail_samples) ? P25_CAL_CLIPPED : P25_CAL_SHORT);
				memset(cal_pcm, 0, cal_samples * sizeof *cal_pcm);
				STORE(parrot_state, P25_PARROT_DONE);
			} else {
				STORE(parrot_state, P25_PARROT_PLAY);
				cal_pass = 0;
				cal_begin_pass(cal_targets[0]);
			}
		}
	} else if (cal_stage == 1) {
		unsigned i = cal_index % parrot_count;
		uint32_t limited = input_level.limited_blocks;
		tx_level_process(&input_level, cal_pcm + i * AUDIO_BLOCK_SAMPLES, spk, AUDIO_BLOCK_SAMPLES,
			TX_LEVEL_UNITY, parrot_target, NULL);
		imbe_enc_frame(&enc, spk, parrot_frame(cal_index));
		if (cal_index >= 2u * parrot_count && cal_mask(i)) {
			cal_encoder_power += speech_power(spk, AUDIO_BLOCK_SAMPLES, NULL);
			cal_measure_count++;
		}
		if (cal_index >= 2u * parrot_count && cal_active[i]) {
			cal_active_count++;
			cal_limited += input_level.limited_blocks != limited;
		}
		if (++cal_index == 3u * parrot_count) { cal_stage = 2; cal_index = 0; imbe_dec_init(&dec); }
	} else if (cal_stage == 2) {
		imbe_dec_frame(&dec, parrot_frame(cal_index), 0, spk);
		if (cal_index >= 2u * parrot_count && cal_mask(cal_index % parrot_count))
			cal_decoder_power += speech_power(spk, AUDIO_BLOCK_SAMPLES, NULL);
		if (++cal_index == 3u * parrot_count) cal_next();
	} else if (cal_stage == 3) {
		if (parrot_lead != 0) { parrot_lead--; silence(spk); }
		else if (parrot_played < 3u * parrot_count) {
			imbe_dec_frame(&dec, parrot_frame(parrot_played++), 0, spk);
			// Warmup is decoded for state continuity, but only the measurement phrase is audible.
			if (parrot_played <= 2u * parrot_count) silence(spk);
			else return true;
		} else if (aud != NULL && aud->spk_pending != NULL && aud->spk_pending()) return false;
		else { speaker(false); STORE(parrot_state, P25_PARROT_DONE); }
	}
	silence(spk);
	return true;
}

static void parrot_block(const int16_t *mic, int16_t *spk)
{
	uint8_t command = LOAD(parrot_command);
	if (command == 1) {
		uint32_t manual = (uint32_t)parrot_gain_q8 << 8;
		memset(parrot_power, 0, sizeof parrot_power);
		memset(parrot_speech_power, 0, sizeof parrot_speech_power);
		memset(parrot_speech_blocks, 0, sizeof parrot_speech_blocks);
		parrot_mic_sum = 0;
		parrot_ac_power = parrot_conditioned_power = 0;
		parrot_count = parrot_played = 0;
		imbe_enc_init(&enc);
		tx_level_reset(&input_level, parrot_agc && parrot_view.mode == CODEC_DIAG_P25 ? TX_LEVEL_UNITY : manual);
#if CONFIG_TX_DC_BLOCK
		dc_block_reset(&input_dc);
#endif
		STORE(parrot_command, 0);
	}
	if (calibrating) {
		if (p25VoiceParrotState() == P25_PARROT_RECORD || p25VoiceParrotState() == P25_PARROT_PLAY) {
			if (calibration_block(mic, spk))
				STORE(parrot_progress, (uint16_t)(LOAD(parrot_progress) + 1u));
		} else silence(spk);
		return;
	}
	uint8_t state = p25VoiceParrotState();
	if (state == P25_PARROT_RECORD && (parrot_count == PARROT_FRAMES || LOAD(parrot_stop))) {
		imbe_dec_init(&dec);
		STORE(parrot_state, P25_PARROT_PLAY);
		speaker(true);
		parrot_lead = aud != NULL ? (aud->speaker_lead_ms + AUDIO_BLOCK_MS - 1u) / AUDIO_BLOCK_MS : 0;
		// Allow the amplifier to settle before the first decoded block.
		silence(spk);
	} else if (state == P25_PARROT_RECORD) {
		tx_level_meter_t raw, conditioned;
		speaker(false);
		// The driver consumes this buffer after the callback. Use it as scratch and return silence.
		input_block(mic, spk, parrot_gain_q8,
			parrot_agc && parrot_view.mode == CODEC_DIAG_P25 ? parrot_target : 0,
			&raw, parrot_view.dc_block, &conditioned);
		imbe_enc_frame(&enc, spk, parrot_frame(parrot_count));
		parrot_count++;
		parrot_conditioned_power += conditioned.power;
		STORE(parrot_view.conditioned_db10, tx_level_db10((uint32_t)(parrot_conditioned_power / parrot_count)));
		parrot_speech(0, mic);
		parrot_speech(1, spk);
		parrot_meter(0, raw);
		parrot_meter(1, tx_level_measure(spk, AUDIO_BLOCK_SAMPLES, true));
		STORE(parrot_view.frames, parrot_count);
		STORE(parrot_view.gain_q16, input_level.gain_q16);
		STORE(parrot_view.limited_blocks, input_level.limited_blocks);
		silence(spk);
	} else if (state == P25_PARROT_PLAY && parrot_lead != 0) {
		parrot_lead--;
		silence(spk);
	} else if (state == P25_PARROT_PLAY && parrot_played < parrot_count) {
		imbe_dec_frame(&dec, parrot_frame(parrot_played), 0, spk);
		parrot_played++;
		parrot_speech(2, spk);
		parrot_meter(2, tx_level_measure(spk, AUDIO_BLOCK_SAMPLES, true));
	} else {
		silence(spk);
		// The driver still has audio to play. Leave the route open and let the radio task detect a stalled drain.
		if (state == P25_PARROT_PLAY && aud != NULL && aud->spk_pending != NULL && aud->spk_pending()) return;
		speaker(false);
		if (state == P25_PARROT_PLAY) STORE(parrot_state, P25_PARROT_DONE);
	}
	if (state == P25_PARROT_RECORD || state == P25_PARROT_PLAY) {
		STORE(parrot_progress, (uint16_t)(LOAD(parrot_progress) + 1u));
	}
}
#endif

// One mic block into the transmission g: encoded and queued for the engine. mic NULL: a silent block (the mic gate);
// the leveling (gain, limiter, DC blocker) is not run, so it holds its state for the speech after it
static void tx_block(uint8_t g, const int16_t *mic, int16_t *scaled)
{
	if (mic == NULL) {
		memset(scaled, 0, AUDIO_BLOCK_SAMPLES * sizeof scaled[0]);
	} else {
		uint32_t limited = input_level.limited_blocks;
		uint16_t target = CONFIG_P25_TX_AGC ? LOAD(tx_target) : 0;
		uint16_t peak = input_block(mic, scaled, LOAD(mic_gain_q8), target, NULL, true, NULL);
		stats.tx_gain_q16 = input_level.gain_q16;
		stats.tx_limited_blocks += input_level.limited_blocks - limited;
		if (peak > stats.tx_mic_peak) {
			stats.tx_mic_peak = peak;
		}
	}
	uint8_t h = tx_head;
	if ((uint8_t)(h - LOAD(tx_tail)) >= P25_VOICE_TX_QUEUE) {
		// The engine is not taking frames (not started yet, or ending): the encoder still runs, to keep its history
		uint8_t drop[P25_IMBE_BYTES];

		imbe_enc_frame(&enc, scaled, drop);
		stats.tx_encoded++;
		stats.tx_dropped++;
		return;
	}
	p25_voice_tx_entry_t *e = &mem->tq[h & (P25_VOICE_TX_QUEUE - 1)];
	e->gen = g;
	imbe_enc_frame(&enc, scaled, e->imbe);
	stats.tx_encoded++;
	if (memcmp(e->imbe, imbe_silence, P25_IMBE_BYTES) == 0) {
		stats.tx_silent++;
	}
	STORE(tx_head, (uint8_t)(h + 1));
}

void p25VoiceAudioBlock(const int16_t *mic, int16_t *spk, void *ctx)
{
	(void)ctx;
	if (LOAD(mem) == NULL) {
		silence(spk);
		return;
	}
#if CONFIG_RX_AGC
	cb_rx_good = false;
#endif
#if CONFIG_P25_PARROT
	if (LOAD(parrot_state) != P25_PARROT_OFF) {
		parrot_block(mic, spk);
		return;
	}
#endif
	uint8_t g = LOAD(tx_gen);
	if ((g & 1u) != 0) {
		if (g != cb_tx_gen) {
			cb_tx_gen = g;
			stats.tx_mic_peak = 0;
			stats.tx_limited_blocks = 0;
			stats.tx_mic_muted = 0;
			imbe_enc_init(&enc);
			uint32_t initial = p25VoiceAgc() ? TX_LEVEL_UNITY : (uint32_t)LOAD(mic_gain_q8) << 8;
			tx_level_reset(&input_level, initial);
			stats.tx_gain_q16 = initial;	// what a block the mic gate mutes reports: the gain held for the speech
#if CONFIG_TX_DC_BLOCK
			dc_block_reset(&input_dc);
#endif
		}
		p25_voice_mic_gate_t gate = LOAD(mic_gate);
		if (gate != NULL && gate()) {
			mic = NULL;	// a beep on the speaker is in the mic too: silence (tx_block)
			stats.tx_mic_muted++;
		}
		// The speaker block doubles as the encoder input scratch while transmitting.
		tx_block(g, mic, spk);
		silence(spk);
	} else {
		if ((cb_tx_gen & 1u) != 0) {
			// The encoder used the decoder's prediction state: a call still coming in starts from none
			cb_tx_gen = g;
			imbe_dec_init(&dec);
		}
		rx_block(spk);
	}
#if CONFIG_RX_AGC
	if (agc.started) {
		if (!cb_live || (g & 1u) != 0)
			rx_agc_end(&agc);
		else if (cb_playing)
			rx_agc_process(&agc, spk, cb_rx_good);
	}
#endif
	p25_voice_mixer_t mix = LOAD(mixer);
	if (mix != NULL && mix(spk)) {
		cb_mixing = true;
		speaker(true);
	} else if (cb_mixing) {
		cb_mixing = false;
		if (!cb_playing) {
			speaker(false);
		}
	}
	if (LOAD(cb_speaker_unsent)) {
		speaker(cb_speaker);
	}
}

// --- radio task ---

static void stream_start(void)
{
	STORE(cb_speaker_unsent, true);	// before the callback can run
	streaming = aud != NULL && aud->start != NULL && aud->start(p25VoiceAudioBlock, NULL);
}

#if CONFIG_P25_PARROT
void p25VoiceParrotReady(uint8_t (*frames)[IMBE_BYTES], unsigned count, uint8_t (*extra)[IMBE_BYTES])
{
	if (aud != NULL) {
		if (aud->stop != NULL) aud->stop();
		if (aud->speaker_pcm != NULL) aud->speaker_pcm(false);
	}
	streaming = false;
	parrot_frames = frames;
	parrot_split = count;
	parrot_extra = extra;
	STORE(parrot_stop, false);
	STORE(parrot_progress, 0);
	parrot_view_reset();
	STORE(parrot_state, P25_PARROT_READY);
	STORE(parrot_command, 0);
}
void p25VoiceParrotPcm(int16_t *pcm, unsigned samples)
{
	cal_pcm = pcm;
	cal_samples = samples;
	unsigned blocks = samples / AUDIO_BLOCK_SAMPLES;
	STORE(parrot_view.capture_limit, blocks > P25_CAL_MAX_BLOCKS ? P25_CAL_MAX_BLOCKS : blocks);
}

bool p25VoiceParrotCapture(bool agc_on, int8_t target_dbfs, uint8_t mic_step, bool hardware_gain)
{
	return p25VoiceParrotCaptureMode(CODEC_DIAG_P25, CONFIG_TX_DC_BLOCK, agc_on, target_dbfs, mic_step, hardware_gain);
}

bool p25VoiceParrotCaptureMode(uint8_t mode, bool dc, bool agc_on, int8_t target_dbfs,
	uint8_t mic_step, bool hardware_gain)
{
	uint8_t state = p25VoiceParrotState();
	if (state != P25_PARROT_READY && state != P25_PARROT_DONE && state != P25_PARROT_ERROR) return false;
	if (LOAD(mem) == NULL || mode > CODEC_DIAG_CALIBRATE) return false;
	bool calibration = mode == CODEC_DIAG_CALIBRATE;
	if (calibration && (!CONFIG_TX_DC_BLOCK || cal_pcm == NULL || cal_samples < 50u * AUDIO_BLOCK_SAMPLES)) {
		if (streaming) { aud->stop(); streaming = false; }
		parrot_view_reset();
		STORE(parrot_view.mode, CODEC_DIAG_CALIBRATE);
		STORE(parrot_view.cal_status, P25_CAL_UNAVAILABLE);
		p25VoiceParrotFail();
		return false;
	}
	if (streaming) { aud->stop(); streaming = false; }
	int target = target_dbfs < -35 ? -35 : target_dbfs > -15 ? -15 : target_dbfs;
	parrot_target = parrot_rms(target);
	parrot_gain_q8 = mic_gain_step_q8(hardware_gain ? 5 : mic_step);
	parrot_view_reset();
	STORE(parrot_view.mode, mode);
	STORE(parrot_view.dc_block, dc && CONFIG_TX_DC_BLOCK);
	calibrating = calibration;
	if (calibration) {
		agc_on = true;
		parrot_gain_q8 = 256;
		cal_startup = cal_stage = 0;
		unsigned blocks = cal_samples / AUDIO_BLOCK_SAMPLES;
		if (blocks > P25_CAL_MAX_BLOCKS) blocks = P25_CAL_MAX_BLOCKS;
		STORE(parrot_view.capture_limit, blocks);
		STORE(parrot_view.cal_status, P25_CAL_RUNNING);
		STORE(parrot_view.dc_block, true);
	}
	parrot_agc = agc_on;
	STORE(parrot_view.agc, agc_on);
	STORE(parrot_view.limiter, agc_on || CONFIG_TX_MIC_LIMITER);
	STORE(parrot_view.target_dbfs, (int8_t)target);
	STORE(parrot_view.mic_step, mic_step > 10 ? 10 : mic_step);
	STORE(parrot_view.manual_gain_q16, (uint32_t)parrot_gain_q8 << 8);
	STORE(parrot_stop, false);
	STORE(parrot_progress, 0);
	STORE(parrot_command, 1);
	STORE(parrot_state, P25_PARROT_RECORD);
	stream_start();
	if (!streaming) p25VoiceParrotFail();
	return streaming;
}
void p25VoiceParrotStop(void) { STORE(parrot_stop, true); }
void p25VoiceParrotFail(void)
{
	if (streaming) {
		aud->stop();
		streaming = false;
	}
	if (aud != NULL && aud->speaker_pcm != NULL) aud->speaker_pcm(false);
	if (cal_pcm != NULL) memset(cal_pcm, 0, cal_samples * sizeof *cal_pcm);
	STORE(parrot_command, 0);
	STORE(parrot_state, P25_PARROT_ERROR);
}

uint8_t p25VoiceParrotState(void) { return LOAD(parrot_state); }
uint16_t p25VoiceParrotProgress(void) { return LOAD(parrot_progress); }

void p25VoiceParrotView(p25_parrot_view_t *out)
{
	out->mode = LOAD(parrot_view.mode);
	out->dc_block = LOAD(parrot_view.dc_block);
	out->conditioned_db10 = LOAD(parrot_view.conditioned_db10);
	out->codec_options = LOAD(parrot_view.codec_options);
	out->codec_flags = LOAD(parrot_view.codec_flags);
	out->cal_status = LOAD(parrot_view.cal_status);
	out->cal_pass = LOAD(parrot_view.cal_pass);
	out->suggested_db10 = LOAD(parrot_view.suggested_db10);
	out->capture_limit = LOAD(parrot_view.capture_limit);
	out->startup_rail_samples = LOAD(parrot_view.startup_rail_samples);
	for (unsigned i = 0; i < P25_CAL_PASSES; i++) {
		out->cal[i].target_db10 = LOAD(parrot_view.cal[i].target_db10);
		out->cal[i].encoder_db10 = LOAD(parrot_view.cal[i].encoder_db10);
		out->cal[i].decoded_db10 = LOAD(parrot_view.cal[i].decoded_db10);
		out->cal[i].active_blocks = LOAD(parrot_view.cal[i].active_blocks);
		out->cal[i].limited_blocks = LOAD(parrot_view.cal[i].limited_blocks);
		out->cal[i].gain_q16 = LOAD(parrot_view.cal[i].gain_q16);
	}
	out->mic_mean = LOAD(parrot_view.mic_mean);
	out->mic_ac_db10 = LOAD(parrot_view.mic_ac_db10);
	out->mic_dc_peak = LOAD(parrot_view.mic_dc_peak);
	out->input_rail_samples = LOAD(parrot_view.input_rail_samples);
	for (unsigned i = 0; i < 3; i++) {
		out->speech_db10[i] = LOAD(parrot_view.speech_db10[i]);
		out->speech_blocks[i] = LOAD(parrot_view.speech_blocks[i]);
	}
	out->frames = LOAD(parrot_view.frames);
	out->input_db10 = LOAD(parrot_view.input_db10);
	out->encoder_db10 = LOAD(parrot_view.encoder_db10);
	out->output_db10 = LOAD(parrot_view.output_db10);
	out->input_peak = LOAD(parrot_view.input_peak);
	out->encoder_peak = LOAD(parrot_view.encoder_peak);
	out->output_peak = LOAD(parrot_view.output_peak);
	out->gain_q16 = LOAD(parrot_view.gain_q16);
	out->manual_gain_q16 = LOAD(parrot_view.manual_gain_q16);
	out->limited_blocks = LOAD(parrot_view.limited_blocks);
	out->agc = LOAD(parrot_view.agc);
	out->limiter = LOAD(parrot_view.limiter);
	out->target_dbfs = LOAD(parrot_view.target_dbfs);
	out->mic_step = LOAD(parrot_view.mic_step);
	out->state = p25VoiceParrotState();
}
#endif

void p25VoiceInit(const hal_audio_t *audio)
{
	aud = audio;
}

void p25VoiceBegin(p25_voice_mem_t *m)
{
#if CONFIG_RX_AGC
	rx_agc_end(&agc);
#endif
#if CONFIG_P25_PARROT
	STORE(parrot_state, P25_PARROT_OFF);
#endif
	// The stream is stopped (p25VoiceEnd) and the engine not started: nothing else touches this state now
	rx_head = rx_tail = 0;
	rx_seq = rx_end_seq = 0;
	tx_head = tx_tail = 0;
	tx_gen = cb_tx_gen = 0;
	cb_seq = 0;
	cb_live = cb_primed = cb_playing = cb_speaker = cb_mixing = false;
	dec.s = &m->st;
	dec.p = &m->pr;
	dec.mbe.s = &m->ms;
	dec.mbe.prev = &m->prev;
	dec.mbe.cur = &m->cur;
	dec.mbe.buf = m->buf;
	imbe_dec_init(&dec);
	enc.s = &m->st.q;
	enc.est = &m->est;
	enc.p = &m->pr;
	enc.buf = m->buf;
	STORE(mem, m);
}

void p25VoicePoll(void)
{
	if (!streaming && aud != NULL && LOAD(mem) != NULL && LOAD(rx_seq) != 0) {
		stream_start();
	}
}

void p25VoiceTx(bool on)
{
	if (LOAD(mem) == NULL || on == ((tx_gen & 1u) != 0)) {
		return;
	}
	STORE(tx_gen, (uint8_t)(tx_gen + 1));
	if (on && !streaming && aud != NULL) {
		stream_start();
	}
}

void p25VoiceSetTxTarget(uint16_t rms) { STORE(tx_target, rms); }
bool p25VoiceAgc(void) { return CONFIG_P25_TX_AGC && LOAD(tx_target) != 0; }
uint16_t p25VoiceTxTarget(void) { return CONFIG_P25_TX_AGC ? LOAD(tx_target) : 0; }

void p25VoiceSetMicGain(uint8_t step)
{
	STORE(mic_gain_q8, mic_gain_step_q8(step));
}

void p25VoiceEnd(void)
{
	if (streaming && aud != NULL) {
		aud->stop();
		if (aud->speaker_pcm != NULL) {
			aud->speaker_pcm(false);
		}
	}
	streaming = false;
#if CONFIG_RX_AGC
	rx_agc_end(&agc);
#endif
#if CONFIG_P25_PARROT
	if (cal_pcm != NULL) memset(cal_pcm, 0, cal_samples * sizeof *cal_pcm);
	STORE(parrot_state, P25_PARROT_OFF);
	if (parrot_frames != NULL) memset(parrot_frames, 0, parrot_split * IMBE_BYTES);
	if (parrot_extra != NULL) memset(parrot_extra, 0, (PARROT_FRAMES - parrot_split) * IMBE_BYTES);
	cal_pcm = NULL;
	cal_samples = 0;
	calibrating = false;
	parrot_frames = parrot_extra = NULL;
#endif
	STORE(mem, (p25_voice_mem_t *)NULL);
}

bool p25VoiceStreaming(void)
{
	return streaming;
}

void p25VoiceSetMixer(p25_voice_mixer_t m)
{
	STORE(mixer, m);
}

void p25VoiceSetMicGate(p25_voice_mic_gate_t g)
{
	STORE(mic_gate, g);
}

const p25_engine_hooks_t *p25VoiceHooks(void)
{
	return &hooks;
}

void p25VoiceGetStats(p25_voice_stats_t *out)
{
	*out = stats;
}

void p25VoiceResetStats(void)
{
	memset(&stats, 0, sizeof(stats));
}

#endif	// CONFIG_P25 && CONFIG_IMBE
