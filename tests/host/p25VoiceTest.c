// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 David Behnke
// The P25 voice path (core/p25/p25Voice.c) on a made-up PCM stream: the engine's hooks feed frames a LDU at a time,
// the audio callback is driven block by block. Playback waits for a whole LDU and its lead-in, plays every frame in
// order, conceals a late LDU through the decoder and mutes; bad frames, overflow, a call that ends early, the speaker
// route, the mixer and the stream's start and stop.
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "dsp/tx_level.h"
#include "p25/p25Voice.h"
#include "audio/rx_agc.h"

static int failures, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

// ── The board's PCM stream ──
static bool running, spk_on;
static bool speaker_pending;
static unsigned pending_checks;
static int starts, stops;
static bool fake_start(audio_block_cb_t cb, void *ctx)
{
	(void)cb;
	(void)ctx;
	running = true;
	starts++;
	return true;
}
static void fake_stop(void) { running = false; stops++; }
static void fake_speaker(bool on)
{
	spk_on = on;
	if (!on) speaker_pending = false;
}
static bool fake_pending(void)
{
	pending_checks++;
	return speaker_pending;
}
static const hal_audio_t audio = { .start = fake_start, .stop = fake_stop, .speaker_pcm = fake_speaker,
				   .spk_pending = fake_pending };

static p25_voice_mem_t vmem;
static const p25_engine_hooks_t *hk;

// A voiced frame: pitch code 60, mid gain, every band voiced, the rest of the bits varied by n
static void frame(uint8_t f[P25_IMBE_BYTES], int n)
{
	for (int i = 0; i < P25_IMBE_BYTES; i++)
		f[i] = (uint8_t)(0x35 * (i + 1) + 7 * n);
	f[0] = 60;
}

static void ldu(uint8_t errs)
{
	uint8_t f[P25_IMBE_BYTES];
	for (int i = 0; i < P25_VOICE_FRAMES; i++) {
		frame(f, i);
		hk->rx_voice(f, errs, NULL);
	}
}

// One audio block; true when it is not silent
static bool block(void)
{
	int16_t mic[AUDIO_BLOCK_SAMPLES] = { 0 }, spk[AUDIO_BLOCK_SAMPLES];
	memset(spk, 0x55, sizeof spk);
	p25VoiceAudioBlock(mic, spk, NULL);
	for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
		if (spk[i] != 0)
			return true;
	return false;
}

static void begin(void)
{
	p25VoiceEnd();
	p25VoiceResetStats();
	running = spk_on = false;
	speaker_pending = false;
	pending_checks = 0;
	starts = stops = 0;
	p25VoiceBegin(&vmem, P25_VOICE_PRIME, RX_AGC_P25);
}

static void testPlayback(void)
{
	p25_voice_stats_t s;
	bool heard = false;

	begin();
	p25VoicePoll();
	CHECK(!running, "the stream started before any call");
	CHECK(!block(), "sound before any call");
	hk->rx_start(NULL, NULL);
	p25VoicePoll();
	CHECK(running && starts == 1, "the stream did not start with the call");
	// Eight frames are not a LDU: nothing plays yet
	uint8_t f[P25_IMBE_BYTES];
	for (int i = 0; i < P25_VOICE_FRAMES - 1; i++) {
		frame(f, i);
		hk->rx_voice(f, 0, NULL);
	}
	CHECK(!block() && !spk_on, "played before a whole LDU was in");
	frame(f, 8);
	hk->rx_voice(f, 0, NULL);
	// The lead-in: the speaker route on, silence
	for (int i = 0; i < P25_VOICE_LEAD; i++)
		CHECK(!block(), "lead-in block %d not silent", i);
	CHECK(spk_on, "the speaker route is off while the call plays");
	for (int i = 0; i < P25_VOICE_FRAMES; i++)
		heard |= block();
	p25VoiceGetStats(&s);
	CHECK(heard, "a voiced LDU played silent");
	CHECK(s.rx_frames == 9 && s.rx_in == 9 && s.rx_calls == 1, "frames %u in %u calls %u", (unsigned)s.rx_frames,
	      (unsigned)s.rx_in, (unsigned)s.rx_calls);
	CHECK(s.rx_queue_max == 9, "queue max %u", (unsigned)s.rx_queue_max);
	CHECK(s.rx_peak > 0, "no peak");
	// The next LDU is late: the decoder conceals, then mutes
	for (int i = 0; i < MBE_REPEAT_MAX + 2; i++)
		block();
	p25VoiceGetStats(&s);
	CHECK(s.rx_underruns == MBE_REPEAT_MAX + 2 && s.rx_concealed == MBE_REPEAT_MAX + 2,
	      "underruns %u concealed %u", (unsigned)s.rx_underruns, (unsigned)s.rx_concealed);
	CHECK(s.rx_underrun_at == 9, "underrun at %u", (unsigned)s.rx_underrun_at);
	CHECK(!block(), "concealment did not mute");
	// The LDU arrives after all: it plays at once (the call is primed), then the end turns the route off
	ldu(0);
	hk->rx_end(NULL);
	heard = false;
	for (int i = 0; i < P25_VOICE_FRAMES; i++)
		heard |= block();
	CHECK(heard, "the late LDU did not play");
	CHECK(spk_on, "route off before the queue ran dry");
	CHECK(!block() && !spk_on, "the route stayed on after the call");
	p25VoiceGetStats(&s);
	CHECK(s.rx_frames == 18, "frames %u", (unsigned)s.rx_frames);
	p25VoiceEnd();
	CHECK(!running && stops == 1 && !spk_on, "end did not stop the stream");
}

static void testBadAndOverflow(void)
{
	p25_voice_stats_t s;

	begin();
	hk->rx_start(NULL, NULL);
	ldu(IMBE_ERRS(11, 2, 0));	// frames the decoder does not trust
	for (int i = 0; i < P25_VOICE_LEAD + P25_VOICE_FRAMES; i++)
		CHECK(!block(), "a bad frame with nothing to repeat sounded");
	p25VoiceGetStats(&s);
	CHECK(s.rx_bad == 9 && s.rx_concealed == 9 && s.rx_frames == 0, "bad %u concealed %u frames %u",
	      (unsigned)s.rx_bad, (unsigned)s.rx_concealed, (unsigned)s.rx_frames);
	// Corrected bits are counted on good frames
	ldu(2);
	p25VoiceGetStats(&s);
	CHECK(s.rx_bit_errors == 18, "bit errors %u", (unsigned)s.rx_bit_errors);
	// More than the queue holds: the rest is dropped and counted
	for (int i = 0; i < 4; i++)
		ldu(0);
	p25VoiceGetStats(&s);
	CHECK(s.rx_queue_max == P25_VOICE_QUEUE, "queue max %u", (unsigned)s.rx_queue_max);
	CHECK(s.rx_overflows == 5 * 9 - P25_VOICE_QUEUE, "overflows %u", (unsigned)s.rx_overflows);
}

static void testShortCallAndNewCall(void)
{
	p25_voice_stats_t s;
	uint8_t f[P25_IMBE_BYTES];
	bool heard = false;

	begin();
	// A call of three frames that ends: they play without waiting for a LDU
	hk->rx_start(NULL, NULL);
	for (int i = 0; i < 3; i++) {
		frame(f, i);
		hk->rx_voice(f, 0, NULL);
	}
	hk->rx_end(NULL);
	for (int i = 0; i < P25_VOICE_LEAD + 3; i++)
		heard |= block();
	CHECK(heard, "a short call was not played");
	CHECK(!block() && !spk_on, "the short call did not end");
	// A call that ends before any frame: nothing
	hk->rx_start(NULL, NULL);
	hk->rx_end(NULL);
	CHECK(!block() && !spk_on, "an empty call sounded");
	// Frames left of a call that was cut off are dropped when the next starts
	hk->rx_start(NULL, NULL);
	ldu(0);
	block();
	hk->rx_start(NULL, NULL);	// (the engine starts a new call without an end: a squelch decision)
	for (int i = 0; i < 3; i++)
		CHECK(!block(), "the cut call's frames played in the next");
	p25VoiceGetStats(&s);
	CHECK(s.rx_calls == 2, "calls %u", (unsigned)s.rx_calls);
	// Frames with no call open start one
	hk->rx_end(NULL);
	ldu(0);
	heard = false;
	for (int i = 0; i < P25_VOICE_LEAD + 9; i++)
		heard |= block();
	CHECK(heard, "frames without a start did not play");
}

static bool mixing;
static bool mix(int16_t *spk)
{
	if (mixing)
		spk[0] = 1000;
	return mixing;
}

static void testMixerAndTx(void)
{
	uint8_t f[P25_IMBE_BYTES];
	p25_voice_stats_t s;

	begin();
	p25VoiceSetMixer(mix);
	mixing = true;
	CHECK(block() && spk_on, "the mixer's sound is not routed");
	mixing = false;
	CHECK(!block() && !spk_on, "the route stayed on after the mixer");
	p25VoiceSetMixer(NULL);
	// No transmission: the transmit hook has nothing (the engine sends its own silence)
	CHECK(!hk->tx_voice(f, NULL), "a tx frame with no transmission on");
	p25VoiceGetStats(&s);
	CHECK(s.tx_frames == 0, "tx frames %u", (unsigned)s.tx_frames);
	// Without memory (P25 not running) the callback is silent and safe
	p25VoiceEnd();
	CHECK(!block(), "sound after the end");
}

// A vowel-like mic block: harmonics of 125 Hz (a 64-sample period), the phase running on from block to block
static int mic_n;
static void mic_block(int16_t *mic, int amp)
{
	for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++, mic_n++) {
		double v = 0;
		for (int h = 1; h <= 20; h++)
			v += sin(2 * M_PI * h * mic_n / 64.0 + h * h * 0.3) / (1 + 0.2 * h);
		mic[i] = (int16_t)(amp * v);
	}
}

static bool gate_on(void) { return true; }

// One block while transmitting: the speaker must stay silent
static void tx_blocks(int n, int amp)
{
	int16_t mic[AUDIO_BLOCK_SAMPLES], spk[AUDIO_BLOCK_SAMPLES];

	for (int i = 0; i < n; i++) {
		mic_block(mic, amp);
		memset(spk, 0x55, sizeof spk);
		p25VoiceAudioBlock(mic, spk, NULL);
		for (int k = 0; k < AUDIO_BLOCK_SAMPLES; k++)
			if (spk[k] != 0) {
				CHECK(false, "speaker not silent while transmitting");
				return;
			}
	}
}

static void testTransmit(void)
{
	static mbe_params_t pr;
	imbe_state_t st;
	uint8_t f[P25_IMBE_BYTES];
	p25_voice_stats_t s;
	int got = 0, voiced = 0, pitch_ok = 0;

	begin();
	CHECK(!running, "the stream runs before anything happened");
	p25VoiceTx(true);
	CHECK(running && starts == 1, "the transmission did not start the stream");
	p25VoiceTx(true);
	CHECK(starts == 1, "a second start");
	// The engine takes each frame as it is made: all of them arrive, and they say what the mic heard
	imbe_state_reset(&st);
	for (int i = 0; i < 30; i++) {
		tx_blocks(1, 6000);
		while (hk->tx_voice(f, NULL)) {
			got++;
			if (imbe_unpack(&st, f, &pr) == IMBE_FRAME_OK && i >= 4) {
				// (125 Hz: w0 = 2 pi / 64 in the core's units, 32 harmonics by the standard's count)
				pitch_ok += pr.L >= 28 && pr.L <= 30;
				for (int k = 1; k <= 6; k++)
					voiced += pr.v[k] != 0;
			}
		}
	}
	CHECK(got == 30, "frames taken %d of 30", got);
	CHECK(pitch_ok >= 24, "the pitch was found in %d of 26 frames", pitch_ok);
	CHECK(voiced >= 24 * 6, "low harmonics voiced %d of %d", voiced, 26 * 6);
	p25VoiceGetStats(&s);
	CHECK(s.tx_frames == 30 && s.tx_encoded == 30 && s.tx_dropped == 0, "tx counts %u %u %u", (unsigned)s.tx_frames,
	      (unsigned)s.tx_encoded, (unsigned)s.tx_dropped);
	CHECK(s.tx_mic_peak > 6000 && s.tx_mic_peak < 32768, "mic peak %u", (unsigned)s.tx_mic_peak);
	// The engine not taking them: the queue fills, the rest are dropped, the encoder keeps running
	tx_blocks(P25_VOICE_TX_QUEUE + 3, 6000);
	p25VoiceGetStats(&s);
	CHECK(s.tx_encoded == 30 + P25_VOICE_TX_QUEUE + 3 && s.tx_dropped == 3, "full queue: %u encoded, %u dropped",
	      (unsigned)s.tx_encoded, (unsigned)s.tx_dropped);
	// The end: what is still queued is not sent, in this transmission or the next
	p25VoiceTx(false);
	CHECK(!hk->tx_voice(f, NULL), "a frame after the end");
	p25VoiceTx(true);
	CHECK(!hk->tx_voice(f, NULL), "a frame of the last transmission in a new one");
	// An all-zero mic (the route off) gives silence frames
	{
		int16_t mic[AUDIO_BLOCK_SAMPLES] = { 0 }, spk[AUDIO_BLOCK_SAMPLES];
		p25VoiceAudioBlock(mic, spk, NULL);
	}
	CHECK(hk->tx_voice(f, NULL) && memcmp(f, imbe_silence, sizeof f) == 0, "a zero block is not a silence frame");
	p25VoiceGetStats(&s);
	CHECK(s.tx_silent == 1, "silent frames %u", (unsigned)s.tx_silent);
	// The mic gate on (a beep on the speaker): the mic's block goes out as a silence frame, and is counted
	p25VoiceSetMicGate(gate_on);
	tx_blocks(1, 6000);
	p25VoiceSetMicGate(NULL);
	CHECK(hk->tx_voice(f, NULL) && memcmp(f, imbe_silence, sizeof f) == 0, "a gated block is not a silence frame");
	p25VoiceGetStats(&s);
	CHECK(s.tx_silent == 2 && s.tx_mic_muted == 1, "gated: silent %u, muted %u", (unsigned)s.tx_silent,
	      (unsigned)s.tx_mic_muted);
	tx_blocks(1, 6000);
	CHECK(hk->tx_voice(f, NULL) && memcmp(f, imbe_silence, sizeof f) != 0, "the gate held after it went off");
	// The mic gain: 6 dB up (three steps) doubles what the encoder sees
	p25VoiceTx(false);
	p25VoiceTx(true);
	mic_n = 0;
	tx_blocks(2, 1000);
	p25VoiceGetStats(&s);
	uint32_t unity = s.tx_mic_peak;
	p25VoiceTx(false);
	p25VoiceSetMicGain(8);
	p25VoiceTx(true);
	mic_n = 0;
	tx_blocks(2, 1000);
	p25VoiceGetStats(&s);
	CHECK(unity > 1000 && s.tx_mic_peak >= 2 * unity - 16 && s.tx_mic_peak <= 2 * unity, "mic peak %u, with gain %u",
	      (unsigned)unity, (unsigned)s.tx_mic_peak);
	p25VoiceSetMicGain(5);
	p25VoiceTx(false);
	// Reception after it: a call plays as before (the decoder starts over)
	hk->rx_start(NULL, NULL);
	ldu(0);
	for (int i = 0; i < P25_VOICE_LEAD; i++)
		CHECK(!block(), "sound in the lead");
	CHECK(block(), "no sound in a call after a transmission");
	p25VoiceEnd();
	p25VoiceTx(true);	// not running: nothing happens
	CHECK(!running, "a transmission started the stream with P25 off");
}

// A beep's own stream left the speaker route on and the transmission took the running stream over (#705): the
// first block gives the route back
static void testRouteTakeover(void)
{
	begin();
	spk_on = true;		// the beep's callback routed the speaker
	p25VoiceTx(true);
	block();
	CHECK(!spk_on, "the beep's route left on after the takeover");
	p25VoiceTx(false);
	p25VoiceEnd();
}


// YSF VW bursts are one frame's 5 voice frames, not a 9-frame LDU (p25VoiceBegin's prime): playback starts after 5
static void testFiveFramePriming(void)
{
	p25_voice_stats_t s;
	uint8_t f[P25_IMBE_BYTES];
	bool heard = false;

	p25VoiceEnd();
	p25VoiceResetStats();
	running = spk_on = false;
	p25VoiceBegin(&vmem, 5, RX_AGC_YSF);
	hk->rx_start(NULL, NULL);
	p25VoicePoll();
	for (int i = 0; i < 4; i++) {
		frame(f, i);
		hk->rx_voice(f, 0, NULL);
	}
	CHECK(!block() && !spk_on, "played before 5 frames were in");
	frame(f, 4);
	hk->rx_voice(f, 0, NULL);
	for (int i = 0; i < P25_VOICE_LEAD; i++)
		CHECK(!block(), "lead-in block %d not silent", i);
	CHECK(spk_on, "5 frames did not start playback");
	for (int i = 0; i < 5; i++)
		heard |= block();
	p25VoiceGetStats(&s);
	CHECK(heard && s.rx_calls == 1 && s.rx_frames == 5, "5-frame burst: heard %d calls %u frames %u", heard,
	      (unsigned)s.rx_calls, (unsigned)s.rx_frames);
	hk->rx_end(NULL);
	// P25 again: 9 frames, as before
	begin();
	hk->rx_start(NULL, NULL);
	p25VoicePoll();
	for (int i = 0; i < 5; i++) {
		frame(f, i);
		hk->rx_voice(f, 0, NULL);
	}
	CHECK(!block() && !spk_on, "P25 played after 5 frames: the YSF priming was left over");
	p25VoiceEnd();
}

#if CONFIG_RX_AGC
static void testReceiveAgc(void)
{
	begin();
	rx_agc_reset();
	rx_agc_configure(RX_AGC_P25, 5);
	p25_call_t c = { .nac = 0x293, .have_lc = true };
	c.lc.src = 12345;
	hk->rx_start(&c, NULL);
	p25VoicePoll();
	for (int n = 0; n < 12; n++) {
		ldu(0);
		for (int i = 0; i < P25_VOICE_FRAMES; i++) block();
	}
	rx_agc_view_t v;
	rx_agc_view(RX_AGC_P25, &v);
	CHECK(v.enabled && v.valid, "P25 callback measures decoded speech");
	CHECK(v.output_dbfs > v.input_dbfs + 6 && v.output_dbfs <= -22,
	      "P25 playback applies bounded positive gain: input %d output %d", v.input_dbfs, v.output_dbfs);
	hk->rx_end(NULL);
	for (int i = 0; i < 30; i++) block();
	hk->rx_start(&c, NULL);
	ldu(0);
	for (int i = 0; i <= P25_VOICE_LEAD; i++) block();
	rx_agc_view(RX_AGC_P25, &v);
	CHECK(v.cached, "P25 caller metadata reaches the history");
	p25VoiceEnd();
	rx_agc_reset();
}

// YSF VW through this path: YSF's receive AGC settings and the caller set by p25VoiceRxIdentity
static void testReceiveAgcYsf(void)
{
	rx_agc_view_t v;

	p25VoiceEnd();
	p25VoiceBegin(&vmem, 5, RX_AGC_YSF);
	rx_agc_reset();
	rx_agc_configure(RX_AGC_YSF, 5);
	hk->rx_start(NULL, NULL);
	p25VoiceRxIdentity(0x4E30u, 12);
	p25VoicePoll();
	for (int n = 0; n < 20; n++) {
		ldu(0);
		for (int i = 0; i < P25_VOICE_FRAMES; i++) block();
	}
	rx_agc_view(RX_AGC_YSF, &v);
	CHECK(v.enabled && v.valid, "YSF VW: the YSF receive AGC measured nothing");
	rx_agc_view(RX_AGC_P25, &v);
	CHECK(!v.valid, "YSF VW: the P25 receive AGC ran");
	hk->rx_end(NULL);
	p25VoiceEnd();
	rx_agc_reset();
}
#endif

static void testTxManualCeiling(void)
{
	int16_t mic[AUDIO_BLOCK_SAMPLES], out[AUDIO_BLOCK_SAMPLES];
	p25_voice_stats_t s;
	begin();
	p25VoiceSetTxTarget(0);
	for (uint8_t step = 5; step <= 10; step += 5) {
		p25VoiceSetMicGain(step);
		p25VoiceTx(true);
		for (unsigned i = 0; i < AUDIO_BLOCK_SAMPLES; i++) mic[i] = i & 1u ? INT16_MAX : INT16_MIN;
		p25VoiceAudioBlock(mic, out, NULL);
		p25VoiceGetStats(&s);
#if CONFIG_TX_MIC_LIMITER
		CHECK(s.tx_mic_peak == TX_LEVEL_CEILING && s.tx_limited_blocks == 1 && s.tx_gain_q16 < TX_LEVEL_UNITY,
		      "manual peak protection %u/%u/%u", s.tx_mic_peak, s.tx_limited_blocks, s.tx_gain_q16);
#else
		CHECK(s.tx_mic_peak == 32768 && s.tx_limited_blocks == 0,
		      "manual legacy peak %u/%u", s.tx_mic_peak, s.tx_limited_blocks);
#endif
		p25VoiceTx(false);
	}
	p25VoiceTx(true);
	for (unsigned i = 0; i < AUDIO_BLOCK_SAMPLES; i++) mic[i] = 1000;
	p25VoiceAudioBlock(mic, out, NULL);
	p25VoiceGetStats(&s);
	CHECK(s.tx_mic_peak == (CONFIG_TX_DC_BLOCK ? 0 : 1000 * 810 / 256) && s.tx_gain_q16 == 810u << 8 && s.tx_limited_blocks == 0,
	      "manual gain did not reset on new TX (%u/%u/%u)", s.tx_mic_peak, s.tx_limited_blocks, s.tx_gain_q16);
	p25VoiceSetMicGain(5);
	p25VoiceEnd();
}

#if CONFIG_P25_PARROT
static bool feed_tone(int amplitude, unsigned count)
{
	bool heard = false;
	for (unsigned f = 0; f < count; f++) {
		int16_t mic[AUDIO_BLOCK_SAMPLES], out[AUDIO_BLOCK_SAMPLES];
		for (unsigned i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
			mic[i] = (int16_t)lrint(amplitude * cos(2 * M_PI * 200 * i / AUDIO_RATE_HZ));
		p25VoiceAudioBlock(mic, out, NULL);
		for (unsigned i = 0; i < AUDIO_BLOCK_SAMPLES && !heard; i++) heard = out[i] != 0;
	}
	return heard;
}
#if CONFIG_TX_DC_BLOCK
static bool cal_test_drain;
static uint8_t cal_test_silence_from_pass;
static int16_t cal_speech[16000];
static p25_parrot_view_t runCalibration(int amplitude, int bias, bool clipped, bool startup_rails, unsigned blocks)
{
	static struct { int16_t before; int16_t pcm[P25_CAL_MAX_BLOCKS * AUDIO_BLOCK_SAMPLES]; int16_t after; } recording;
	static uint8_t frames[500][IMBE_BYTES];
	p25_parrot_view_t v;
	begin();
	p25VoiceParrotReady(frames, 500, NULL);
	recording.before = 1234; recording.after = -1234;
	p25VoiceParrotPcm(recording.pcm, sizeof recording.pcm / sizeof recording.pcm[0]);
	CHECK(p25VoiceParrotCaptureMode(CODEC_DIAG_CALIBRATE, false, false, -35, 10, false), "calibration refused");
	int16_t mic[AUDIO_BLOCK_SAMPLES], out[AUDIO_BLOCK_SAMPLES];
	for (unsigned b = 0; b < blocks + 5; b++) {
		for (unsigned n = 0; n < AUDIO_BLOCK_SAMPLES; n++)
			mic[n] = bias + (amplitude < 0 ? cal_speech[((b >= 5 ? b - 5 : 0) * AUDIO_BLOCK_SAMPLES + n) % 16000] / 4 :
				(int16_t)lrint(amplitude * cos(2 * M_PI * n / 40)));
		if (b < 5 && startup_rails) mic[0] = INT16_MAX;
		if (b >= 5 && clipped) mic[0] = INT16_MAX;
		p25VoiceAudioBlock(mic, out, NULL);
	}
	p25VoiceParrotStop();
	unsigned n;
	for (n = 0; n < 5000 && p25VoiceParrotState() != P25_PARROT_DONE; n++) {
		p25VoiceParrotView(&v);
		if (cal_test_drain && v.cal_status == P25_CAL_OK) {
			speaker_pending = true;
			for (unsigned j = 0; j < 3 * blocks + 10; j++) block();
			uint16_t progress = p25VoiceParrotProgress();
			for (unsigned j = 0; j < 5; j++) block();
			CHECK(p25VoiceParrotState() == P25_PARROT_PLAY && p25VoiceParrotProgress() == progress,
			      "stalled calibration drain advances watchdog progress");
			speaker_pending = false;
			cal_test_drain = false;
		}
		block();
		if (cal_test_silence_from_pass != 0 && v.cal_pass + 1u >= cal_test_silence_from_pass)
			for (unsigned j = 0; j < 500; j++) memcpy(frames[j], imbe_silence, IMBE_BYTES);
	}
	CHECK(n < 5000, "calibration did not finish");
	p25VoiceParrotView(&v);
	CHECK(recording.before == 1234 && recording.after == -1234, "PCM recording overrun");
	CHECK(v.agc && v.dc_block && v.manual_gain_q16 == TX_LEVEL_UNITY, "calibration accepted manual/AMBE settings");
	for (unsigned j = 0; j < sizeof recording.pcm / sizeof recording.pcm[0]; j++)
		if (recording.pcm[j] != 0) { CHECK(false, "raw PCM retained after calibration"); break; }
	p25VoiceEnd();
	return v;
}

static void testCalibration(void)
{
	cal_test_drain = true;
	p25_parrot_view_t a = runCalibration(600, 2400, false, true, 100);
	CHECK(a.cal_status == P25_CAL_OK && a.cal_pass == P25_CAL_PASSES, "valid calibration status %u, passes %u", a.cal_status, a.cal_pass);
	CHECK(a.input_rail_samples == 0 && a.startup_rail_samples == 5, "startup rails mixed with recording");
	CHECK(a.suggested_db10 >= -350 && a.suggested_db10 <= -150 && a.suggested_db10 % 10 == 0, "suggested target %d", a.suggested_db10);
	CHECK(abs(a.cal[5].decoded_db10 + 250) <= 10 && a.cal[5].target_db10 == a.suggested_db10, "recommendation not verified");
	for (unsigned i = 0; i < P25_CAL_PASSES; i++) {
		CHECK(a.cal[i].active_blocks == 100 && a.cal[i].limited_blocks == 0, "candidate gate or limit %u", i);
		CHECK(abs(a.cal[i].encoder_db10 - a.cal[i].target_db10) <= 3, "candidate failed target %u %d/%d", i, a.cal[i].encoder_db10, a.cal[i].target_db10);
	}
	p25_parrot_view_t b = runCalibration(600, 0, false, false, 100);
	CHECK(b.cal_status == P25_CAL_OK && abs(a.suggested_db10 - b.suggested_db10) <= 10, "bias changed calibration %d/%d", a.suggested_db10, b.suggested_db10);
	b = runCalibration(0, 2400, false, false, 100);
	CHECK(b.cal_status == P25_CAL_SHORT && b.cal_pass == 0 && b.suggested_db10 == -960, "silence yielded recommendation");
	b = runCalibration(600, 2400, false, false, 20);
	CHECK(b.cal_status == P25_CAL_SHORT && b.cal_pass == 0, "short recording yielded recommendation");
	b = runCalibration(600, 2400, true, false, 100);
	CHECK(b.cal_status == P25_CAL_CLIPPED && b.cal_pass == 0, "railed recording yielded recommendation");
	cal_test_silence_from_pass = 1;
	b = runCalibration(600, 2400, false, false, 100);
	CHECK(b.cal_status == P25_CAL_RANGE && b.cal_pass == 5 && b.suggested_db10 == -960,
	      "codec output without a bracket yielded recommendation");
	cal_test_silence_from_pass = 6;
	b = runCalibration(600, 2400, false, false, 100);
	CHECK(b.cal_status == P25_CAL_RANGE && b.cal_pass == 6 && b.suggested_db10 == -960,
	      "failed verification yielded recommendation");
	cal_test_silence_from_pass = 0;
	FILE *speech = fopen("../data/voice/test_transmission.pcm", "rb");
	if (speech == NULL) speech = fopen("tests/data/voice/test_transmission.pcm", "rb");
	CHECK(speech != NULL, "speech fixture unavailable");
	if (speech != NULL) {
		CHECK(fread(cal_speech, sizeof *cal_speech, 16000, speech) == 16000, "speech fixture too short");
		fclose(speech);
		b = runCalibration(-1, 2400, false, false, 100);
		printf("P25 speech calibration: status %u, target %.1f, decoded %.1f\n", b.cal_status, b.suggested_db10 / 10.0, b.cal[5].decoded_db10 / 10.0);
		for (unsigned i = 0; i < b.cal_pass; i++) printf("speech pass%u: target %d enc %d dec %d limited %u/%u gain %u\n", i, b.cal[i].target_db10, b.cal[i].encoder_db10, b.cal[i].decoded_db10, b.cal[i].limited_blocks, b.cal[i].active_blocks, b.cal[i].gain_q16);
		CHECK(b.cal_status == P25_CAL_OK && abs(b.cal[5].decoded_db10 + 250) <= 10 &&
		      b.cal[5].limited_blocks * 10u <= b.cal[5].active_blocks, "speech target not verified");
	}
	printf("P25 calibration: verified target %d dBFS, decoded %.1f dBFS\n", a.suggested_db10 / 10, a.cal[5].decoded_db10 / 10.0);
}
#endif

static void testCalibrationUnavailable(void)
{
	static uint8_t frames[500][IMBE_BYTES];
	static int16_t tiny[160];
	p25_parrot_view_t v;
	begin();
	p25VoiceParrotReady(frames, 500, NULL);
	p25VoiceParrotPcm(tiny, 160);
	CHECK(!p25VoiceParrotCaptureMode(CODEC_DIAG_CALIBRATE, true, true, -25, 5, false), "short PCM workspace accepted");
	p25VoiceParrotView(&v);
	CHECK(v.cal_status == P25_CAL_UNAVAILABLE && v.state == P25_PARROT_ERROR && v.suggested_db10 == -960,
	      "unavailable workspace retained old report");
	p25VoiceEnd();
}

static void testParrot(void)
{
	static uint8_t frames[500][IMBE_BYTES];
	p25_parrot_view_t v;
	begin();
	p25VoiceParrotReady(frames, 217, frames + 217);
	CHECK(p25VoiceParrotCapture(true, -25, 10, false), "parrot did not start");
	p25VoiceParrotView(&v);
	CHECK(v.state == P25_PARROT_RECORD && v.frames == 0, "capture was not published immediately");
	CHECK(!p25VoiceParrotCapture(false, -18, 5, false), "second capture accepted before the first audio block");
	feed_tone(500, 500);
	p25VoiceParrotView(&v);
	CHECK(v.state == P25_PARROT_RECORD && v.frames == 500, "ten second capture %u/%u", v.state, v.frames);
	CHECK(!spk_on, "speaker on while recording");
	CHECK(v.agc && v.limiter && v.target_dbfs == -25 && v.gain_q16 > TX_LEVEL_UNITY && v.mic_step == 10 &&
	      v.manual_gain_q16 == 810u << 8,
	      "missing recorded settings/gain");
	CHECK(v.input_peak > 0 && v.encoder_peak > v.input_peak, "missing input peaks");
	CHECK(v.input_db10 < -380 && v.encoder_db10 > -260 && v.encoder_db10 < -245,
	      "quiet input/encoder %d/%d", v.input_db10, v.encoder_db10);
	feed_tone(0, 1);
	CHECK(spk_on, "speaker did not open for playback");
	feed_tone(0, 501);
	p25VoiceParrotView(&v);
	CHECK(v.state == P25_PARROT_DONE && !spk_on, "playback did not finish");
	p25_parrot_view_t held = v;
	feed_tone(0, 5);
	p25VoiceParrotView(&v);
	CHECK(v.input_db10 == held.input_db10 && v.output_peak == held.output_peak && v.frames == held.frames, "completed report changed");
	CHECK(abs(v.output_db10 - v.encoder_db10) < 15, "codec output %d encoder %d", v.output_db10, v.encoder_db10);
	CHECK(p25VoiceParrotCapture(false, -25, 7, true), "repeat capture refused");
	feed_tone(500, 50);
	p25VoiceParrotStop();
	feed_tone(0, 52);
	p25VoiceParrotView(&v);
	CHECK(!v.agc && v.limiter == CONFIG_TX_MIC_LIMITER && v.mic_step == 7 &&
	      v.manual_gain_q16 == TX_LEVEL_UNITY && v.gain_q16 == TX_LEVEL_UNITY && v.limited_blocks == 0,
	      "manual report incorrect");
	CHECK(v.state == P25_PARROT_DONE && v.frames == 50, "early stop %u/%u", v.state, v.frames);
	CHECK(abs(v.input_db10 - v.encoder_db10) <= 1, "AGC off changed level %d/%d", v.input_db10, v.encoder_db10);
	CHECK(p25VoiceParrotCapture(true, -25, 5, false), "loud capture refused");
	feed_tone(12000, 100);
	p25VoiceParrotStop();
	feed_tone(0, 102);
	p25VoiceParrotView(&v);
	CHECK(abs(v.encoder_db10 + 250) <= 3, "loud encoder target %d", v.encoder_db10);
	CHECK(p25VoiceParrotCapture(true, -18, 5, false), "target capture refused");
	feed_tone(12000, 100);
	p25VoiceParrotView(&v);
	CHECK(v.agc && v.target_dbfs == -18, "report target was not captured");
	CHECK(abs(v.encoder_db10 + 180) <= 3, "adjusted target %d", v.encoder_db10);
	p25VoiceEnd();
	p25VoiceParrotView(&v);
	CHECK(v.state == P25_PARROT_OFF && !running && !spk_on, "exit failed to release stream");
	begin();
	p25VoiceParrotReady(frames, 217, frames + 217);
	p25VoiceParrotStop(); // no capture yet: harmless, a new recording overwrites the command
	CHECK(p25VoiceParrotCapture(false, -25, 5, false), "capture after idle stop refused");
	p25VoiceParrotStop(); // stop before the first callback must not erase the pending start
	feed_tone(500, 2);
	p25VoiceParrotView(&v);
	CHECK(v.state == P25_PARROT_DONE && v.frames == 0, "pending capture ignored immediate stop");
	p25VoiceEnd();
}

static void testParrotMatchesTx(void)
{
	static uint8_t frames[500][IMBE_BYTES];
	uint8_t bits[IMBE_BYTES];
	p25_voice_stats_t s;
	p25_parrot_view_t v;
	begin();
	p25VoiceSetTxTarget(0);
	p25VoiceSetMicGain(10);
	p25VoiceTx(true);
	feed_tone(12000, 1);
	CHECK(hk->tx_voice(bits, NULL), "no transmit frame to compare with capture");
	p25VoiceGetStats(&s);
	p25VoiceEnd();
	p25VoiceBegin(&vmem, P25_VOICE_PRIME, RX_AGC_P25);
	p25VoiceParrotReady(frames, 217, frames + 217);
	p25VoiceSetMicGain(5);
	CHECK(p25VoiceParrotCapture(false, -25, 10, false), "manual parrot capture refused");
	feed_tone(12000, 1);
	p25VoiceParrotView(&v);
	CHECK(v.input_peak == 12000 && v.encoder_peak == s.tx_mic_peak && v.gain_q16 == s.tx_gain_q16 &&
	      v.limited_blocks == s.tx_limited_blocks && v.manual_gain_q16 == 810u << 8,
	      "manual parrot input differs from transmit");
	CHECK(memcmp(bits, frames[0], sizeof bits) == 0, "parrot and transmit encoded different bits");
	p25VoiceEnd();
}

// Matching settings must exercise identical encoding in the diagnostic and live TX.
static void testParrotAgcMatchesTx(void)
{
#if CONFIG_P25_TX_AGC
	static uint8_t frames[500][IMBE_BYTES], transmitted[60][IMBE_BYTES];
	int16_t mic[160], out[160];
	begin();
	p25VoiceSetTxTarget(3277);
	p25VoiceTx(true);
	for (unsigned b = 0; b < 60; b++) {
		for (unsigned i = 0; i < 160; i++) mic[i] = (int16_t)((i % 40 < 20 ? 700 : -700) + (b < 30 ? 2500 : -2500));
		p25VoiceAudioBlock(mic, out, NULL);
		CHECK(hk->tx_voice(transmitted[b], NULL), "missing live AGC frame");
	}
	p25VoiceEnd();
	p25VoiceBegin(&vmem, P25_VOICE_PRIME, RX_AGC_P25);
	p25VoiceParrotReady(frames, 217, frames + 217);
	CHECK(p25VoiceParrotCapture(true, -20, 5, false), "AGC parity capture refused");
	for (unsigned b = 0; b < 60; b++) {
		for (unsigned i = 0; i < 160; i++) mic[i] = (int16_t)((i % 40 < 20 ? 700 : -700) + (b < 30 ? 2500 : -2500));
		p25VoiceAudioBlock(mic, out, NULL);
		CHECK(memcmp(transmitted[b], frames[b], IMBE_BYTES) == 0, "AGC frame %u differs from live TX", b);
	}
	p25VoiceEnd();
#endif
}

// Compare the diagnostic's decoded estimate with the real receive controller, with RX gain Off.
static void testParrotMeters(void)
{
	static uint8_t frames[500][IMBE_BYTES];
	p25_parrot_view_t v;
	for (int polarity = -1; polarity <= 1; polarity += 2) {
		begin();
		p25VoiceParrotReady(frames, 217, frames + 217);
		CHECK(p25VoiceParrotCapture(false, -25, 5, false), "meter capture refused");
		int16_t mic[160], out[160];
		for (unsigned block = 0; block < 40; block++) {
			for (unsigned i = 0; i < 160; i++) mic[i] = (int16_t)(polarity * 2500 + (i % 40 < 20 ? 1000 : -1000));
			p25VoiceAudioBlock(mic, out, NULL);
		}
		p25VoiceParrotView(&v);
		CHECK(v.mic_mean == polarity * 2500 && v.mic_dc_peak == 2500, "DC %d/%u", v.mic_mean, v.mic_dc_peak);
		CHECK(abs(v.mic_ac_db10 + 303) <= 2, "centered RMS %d", v.mic_ac_db10);
		CHECK(v.input_rail_samples == 0 && v.speech_blocks[0] == 40, "capture count");
		int16_t speech = v.speech_db10[0], ac = v.mic_ac_db10;
		memset(mic, 0, sizeof mic);
		for (unsigned block = 0; block < 20; block++) p25VoiceAudioBlock(mic, out, NULL);
		p25VoiceParrotView(&v);
		CHECK(v.speech_blocks[0] == 40 && v.speech_db10[0] == speech, "pauses changed speech estimate");
		CHECK(v.mic_ac_db10 < ac - 15, "whole-capture RMS did not include pauses");
		p25VoiceParrotStop();
		p25VoiceAudioBlock(mic, out, NULL);
#if CONFIG_RX_AGC
		rx_agc_reset();
		rx_agc_t reference;
		rx_agc_begin(&reference, RX_AGC_P25, 0, 0);
#endif
		for (unsigned block = 0; block < 60; block++) {
			p25VoiceAudioBlock(mic, out, NULL);
#if CONFIG_RX_AGC
			rx_agc_process(&reference, out, true);
#endif
		}
		p25VoiceParrotView(&v);
#if CONFIG_RX_AGC
		CHECK(v.speech_blocks[2] >= 15, "decoded comparison never acquired speech");
		CHECK(v.speech_blocks[2] == reference.speech_blocks, "RX qualifying count mismatch");
		CHECK(v.speech_db10[2] == reference.input_q8 * 10 / 256, "RX estimator mismatch %d/%d", v.speech_db10[2], reference.input_q8);
#endif
		p25VoiceEnd();
	}
	// A changing bias must remain visible even when its signed recording mean cancels.
	begin();
	p25VoiceParrotReady(frames, 217, frames + 217);
	CHECK(p25VoiceParrotCapture(false, -25, 5, false), "rail capture refused");
	int16_t mic[160], out[160];
	for (unsigned b = 0; b < 2; b++) {
		for (unsigned i = 0; i < 160; i++) mic[i] = b ? -32768 : 32767;
		p25VoiceAudioBlock(mic, out, NULL);
	}
	p25VoiceParrotView(&v);
	CHECK(v.mic_mean == 0 && v.mic_dc_peak == 32768 && v.input_rail_samples == 320, "changing DC/rail report");
	CHECK(v.mic_ac_db10 == -960 && v.speech_blocks[0] == 0, "DC alone counted as speech");
	CHECK(p25VoiceParrotCapture(false, -25, 5, false) == false, "capture unexpectedly restarted");
	p25VoiceEnd();
	begin();
	p25VoiceParrotReady(frames, 217, frames + 217);
	CHECK(p25VoiceParrotCapture(false, -25, 5, false), "full capture refused");
	for (unsigned i = 0; i < 160; i++) mic[i] = INT16_MIN;
	for (unsigned b = 0; b < 500; b++) p25VoiceAudioBlock(mic, out, NULL);
	p25VoiceParrotView(&v);
	CHECK(v.mic_mean == INT16_MIN && v.input_rail_samples == 80000 && v.mic_dc_peak == 32768,
	      "full capture overflow %d/%u", v.mic_mean, (unsigned)v.input_rail_samples);
	CHECK(v.mic_ac_db10 == -960 && v.speech_blocks[0] == 0, "constant full capture counted as speech");
	p25VoiceEnd();
}

// A steady microphone bias must not consume the encoder AGC target.
static void testParrotBiasedSpeech(void)
{
#if CONFIG_TX_DC_BLOCK
	static uint8_t frames[500][IMBE_BYTES];
	p25_parrot_view_t v;
	int16_t mic[160], out[160];
	int16_t reference = -960;
	for (int dc = -2400; dc <= 2400; dc += 2400) {
		begin();
		p25VoiceParrotReady(frames, 217, frames + 217);
		CHECK(p25VoiceParrotCapture(true, -25, 5, false), "biased capture refused");
		for (unsigned b = 0; b < 300; b++) {
			for (unsigned i = 0; i < 160; i++) mic[i] = dc + (int16_t)lrint(500 * cos(2 * M_PI * i / 40));
			p25VoiceAudioBlock(mic, out, NULL);
		}
		p25VoiceParrotView(&v);
		CHECK(abs(v.mic_mean - dc) <= 1, "raw DC lost");
		CHECK(abs(v.speech_db10[1] + 250) < 10, "DC stole speech target: dc=%d enc=%d", dc, v.speech_db10[1]);
		p25VoiceParrotStop();
		feed_tone(0, 302);
		p25VoiceParrotView(&v);
		if (reference == -960) reference = v.speech_db10[2];
		CHECK(v.speech_blocks[2] >= 15 && abs(v.speech_db10[2] - reference) <= 10,
		      "decoded biased speech differs from zero-offset case %d/%d", v.speech_db10[2], reference);
		p25VoiceEnd();
	}
#endif
}

static void testParrotDrain(void)
{
	static uint8_t frames[500][IMBE_BYTES];
	p25_parrot_view_t v;
	begin();
	p25VoiceParrotReady(frames, 217, frames + 217);
	for (unsigned pass = 0; pass < 3; pass++) {
		CHECK(p25VoiceParrotCapture(false, -25, 5, false), "drain test could not start a capture");
		CHECK(p25VoiceParrotProgress() == 0, "progress from the last capture was retained");
		CHECK(!feed_tone(3000, 8), "scaled microphone samples leaked into the speaker buffer");
		p25VoiceParrotStop();
		CHECK(!block() && spk_on && p25VoiceParrotState() == P25_PARROT_PLAY, "playback did not start");
		CHECK(feed_tone(0, 8), "no audio came out of the decoder");
		speaker_pending = true;
		unsigned polls = pending_checks;
		uint16_t progress = p25VoiceParrotProgress();
		CHECK(!block() && !block(), "playback drain did not fill silence");
		CHECK(spk_on && p25VoiceParrotState() == P25_PARROT_PLAY && pending_checks == polls + 2,
		      "route closed while the speaker still held PCM");
		CHECK(p25VoiceParrotProgress() == progress, "callbacks that only waited for drain counted as progress");
		if (pass == 0) {
			speaker_pending = false;
			CHECK(!block() && !spk_on && p25VoiceParrotState() == P25_PARROT_DONE,
			      "route stayed open after the PCM drained");
			p25VoiceParrotView(&v);
			CHECK(v.frames == 8 && v.output_peak != 0, "report was lost after playback drained");
		} else if (pass == 1) {
			CHECK(!feed_tone(0, 12) && p25VoiceParrotProgress() == progress,
			      "callbacks kept the watchdog alive while the drain was stuck");
			p25VoiceParrotFail();
			p25VoiceParrotView(&v);
			CHECK(!running && !spk_on && !speaker_pending && v.state == P25_PARROT_ERROR,
			      "failure did not stop the stalled playback");
			CHECK(v.frames == 8 && v.output_peak != 0, "failure discarded the playback report");
		} else {
			p25VoiceEnd();
			CHECK(!running && !spk_on && !speaker_pending && p25VoiceParrotState() == P25_PARROT_OFF,
			      "exit waited for queued speaker PCM");
		}
	}
}

static void testParrotFailure(void)
{
	static uint8_t frames[500][IMBE_BYTES];
	p25_parrot_view_t v;
	begin();
	p25VoiceParrotReady(frames, 217, frames + 217);
	CHECK(p25VoiceParrotCapture(false, -25, 5, false), "stall test could not start a capture");
	feed_tone(1000, 3);
	p25VoiceParrotFail();
	p25VoiceParrotView(&v);
	CHECK(!running && !spk_on && v.state == P25_PARROT_ERROR, "stalled stream was not stopped");
	CHECK(v.frames == 3 && v.input_peak == 1000, "failure discarded the partial capture report");
	CHECK(p25VoiceParrotCapture(false, -25, 5, false), "could not restart after stall");
	p25VoiceParrotView(&v);
	CHECK(v.state == P25_PARROT_RECORD && v.frames == 0 && p25VoiceParrotProgress() == 0,
	      "restarted capture retained stale progress");
	p25VoiceEnd();
}
#endif

#if CONFIG_P25_TX_AGC
static void testTxAgc(void)
{
	p25_voice_stats_t s;
	uint8_t bits[IMBE_BYTES];
	int16_t mic[160], out[160];
	for (unsigned i = 0; i < 160; i++)
		mic[i] = (int16_t)(500 * sin(2 * M_PI * i / 40));
	begin();
	p25VoiceSetTxTarget(3277);
	p25VoiceTx(true);
	for (unsigned f = 0; f < 150; f++) {
		p25VoiceAudioBlock(mic, out, NULL);
		(void)hk->tx_voice(bits, NULL);
	}
	// Restart peak accounting after settling, without restarting the AGC.
	p25VoiceResetStats();
	p25VoiceAudioBlock(mic, out, NULL);
	p25VoiceGetStats(&s);
	CHECK(s.tx_mic_peak > 4500 && s.tx_mic_peak < 4800, "AGC did not reach -20 dBFS for the reference tone: peak %u", (unsigned)s.tx_mic_peak);
	CHECK(s.tx_gain_q16 > TX_LEVEL_UNITY && s.tx_limited_blocks == 0, "AGC did not report quiet speech boost");
	p25VoiceTx(false);
	p25VoiceSetTxTarget(0);
	p25VoiceEnd();
}
#endif

#if CONFIG_P25_TX_AGC && CONFIG_TX_DC_BLOCK
// A gated block holds the leveling: the AGC gain, its limited count and the DC blocker's estimate carry over to the
// speech after the beep. Run B is run A with 5 gated blocks in the middle; the gated blocks don't take mic samples
// (mic_n holds), so both runs feed the controller the same speech and must end at the same gain
static void gate_run(int gated, p25_voice_stats_t *s)
{
	int16_t mic[AUDIO_BLOCK_SAMPLES], spk[AUDIO_BLOCK_SAMPLES];
	uint8_t f[P25_IMBE_BYTES];
	begin();
	p25VoiceSetTxTarget(3277);
	p25VoiceTx(true);
	mic_n = 0;
	for (int b = 0; b < 10 + gated + 4; b++) {
		bool g = b >= 10 && b < 10 + gated;
		p25VoiceSetMicGate(g ? gate_on : NULL);
		if (!g) {
			mic_block(mic, 300);
			for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
				mic[i] = (int16_t)(mic[i] + 2000);	// the mic's bias, for the DC blocker to remove
		}
		p25VoiceAudioBlock(mic, spk, NULL);
		CHECK(hk->tx_voice(f, NULL), "block %d: a frame", b);
		// (the encoder's analysis window still holds the speech for the first two gated blocks)
		if (g && b >= 12)
			CHECK(memcmp(f, imbe_silence, sizeof f) == 0, "block %d: a gated block is not a silence frame", b);
	}
	p25VoiceSetMicGate(NULL);
	p25VoiceGetStats(s);
	p25VoiceTx(false);
	p25VoiceSetTxTarget(0);
	p25VoiceEnd();
}

static void testMicGateHoldsLeveling(void)
{
	p25_voice_stats_t a, b;
	gate_run(0, &a);
	gate_run(5, &b);
	CHECK(a.tx_gain_q16 > TX_LEVEL_UNITY, "quiet speech not boosted: %u", (unsigned)a.tx_gain_q16);
	CHECK(b.tx_gain_q16 == a.tx_gain_q16 && b.tx_limited_blocks == a.tx_limited_blocks,
	      "gate changed the leveling: gain %u vs %u, limited %u vs %u", (unsigned)b.tx_gain_q16,
	      (unsigned)a.tx_gain_q16, (unsigned)b.tx_limited_blocks, (unsigned)a.tx_limited_blocks);
	CHECK(a.tx_mic_muted == 0 && b.tx_mic_muted == 5, "muted %u, %u", (unsigned)a.tx_mic_muted,
	      (unsigned)b.tx_mic_muted);
}
#endif

int main(void)
{
	p25VoiceInit(&audio);
	hk = p25VoiceHooks();
	testPlayback();
	testBadAndOverflow();
	testShortCallAndNewCall();
	testMixerAndTx();
	testTransmit();
	testRouteTakeover();
	testFiveFramePriming();
#if CONFIG_RX_AGC
	testReceiveAgc();
	testReceiveAgcYsf();
#endif
	testTxManualCeiling();
#if CONFIG_P25_TX_AGC
	testTxAgc();
#endif
#if CONFIG_P25_TX_AGC && CONFIG_TX_DC_BLOCK
	testMicGateHoldsLeveling();
#endif
#if CONFIG_P25_PARROT
#if CONFIG_TX_DC_BLOCK
	testCalibration();
#endif
	testCalibrationUnavailable();
	testParrot();
	testParrotMatchesTx();
	testParrotMeters();
	testParrotAgcMatchesTx();
	testParrotBiasedSpeech();
	testParrotDrain();
	testParrotFailure();
#endif
	printf("p25VoiceTest: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
