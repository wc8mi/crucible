// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 David Behnke
// DMR voice glue (core/dmr/ambe/ambeVoice.c) against a fake audio driver and a fake vocoder
#include <stdio.h>
#include <string.h>
#include "dsp/mic_gain.h"
#include "dsp/tx_level.h"

#include "dmr/ambe/ambeVoice.h"
#include "dmr/ambe/ambeFec.h"
#include "vocoder/ambe2_flags.h"

static int failures, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

// --- fakes ---
static audio_block_cb_t cb;
static int starts, stops, speaker_on, speaker_calls;
static bool fake_start(audio_block_cb_t c, void *ctx) { (void)ctx; cb = c; starts++; return true; }
// The tone is asked for, playing, or its frames are not yet pulled by the call stack (the source's busy: the over is
// held open for it after PTT)
static bool ambeVoiceTxToneBusy(void)
{
	const dmr_voice_source_t *s = ambeVoiceSource();
	return s->busy != NULL && s->busy(s->ctx);
}

static void fake_stop(void) { stops++; cb = NULL; }
static void fake_speaker(bool on) { speaker_on = on; speaker_calls++; }
static const hal_audio_t audio = { .start = fake_start, .stop = fake_stop, .speaker_pcm = fake_speaker };

// Vocoder: encode copies the low bit of the first 49 samples; decode writes the bits into samples 0..48 and a frame
// marker into sample 159
static int resets, decodes;
static bool agc_wave;
static bool fake_present(void) { return true; }
static void fake_reset(void) { resets++; }
static void fake_encode(const int16_t pcm[VOCODER_FRAME_SAMPLES], uint8_t bits[VOCODER_BITS])
{
	for (int i = 0; i < VOCODER_BITS; i++)
		bits[i] = (uint8_t)(pcm[i] & 1);
}
static void fake_decode(const uint8_t bits[VOCODER_BITS], int16_t pcm[VOCODER_FRAME_SAMPLES])
{
	decodes++;
	if (agc_wave) {
		for (int i = 0; i < VOCODER_FRAME_SAMPLES; i++)
			pcm[i] = i & 1 ? 464 : -464;
		return;
	}
	memset(pcm, 0, VOCODER_FRAME_SAMPLES * sizeof(int16_t));
	for (int i = 0; i < VOCODER_BITS; i++)
		pcm[i] = bits[i];
	pcm[159] = 1000;
}
static const hal_vocoder_t vocoder = { fake_present, fake_reset, fake_encode, fake_decode, NULL, NULL, NULL, NULL, NULL };

// A vocoder with encode options, built as the AMBE+2 hosts are: set_options keeps the flags word, and each frame is
// two codec calls (two halves) that both pass it. Every call's flags are recorded
static uint32_t opt_flags = 0xDEADBEEFu;	// "never set" until set_options
static uint32_t opt_sets, opt_last_set;
static uint32_t calls_flags[64];
static int ncalls;
static uint32_t opt_options(void) { return AMBE2_OPTIONS; }
static void opt_set(uint32_t opts) { opt_sets++; opt_last_set = opts; opt_flags = ambe2_enc_flags(opts); }
static uint32_t opt_encode_flags(void) { return opt_flags; }
static void opt_encode(const int16_t pcm[VOCODER_FRAME_SAMPLES], uint8_t bits[VOCODER_BITS])
{
	const uint32_t flags = opt_flags;
	for (int h = 0; h < 2; h++)
		if (ncalls < 64)
			calls_flags[ncalls++] = flags;
	fake_encode(pcm, bits);
}
static const hal_vocoder_t opt_vocoder = {
	.present = fake_present, .reset = fake_reset, .encode = opt_encode, .decode = fake_decode,
	.options = opt_options, .set_options = opt_set, .encode_flags = opt_encode_flags,
};

static void makeFrame(int n, uint8_t bits[49], uint8_t frame[9])
{
	for (int i = 0; i < 49; i++)
		bits[i] = (uint8_t)(((n * 7 + i * 3) >> 2) & 1);
	bits[0] = (uint8_t)(n & 1);
	bits[1] = (uint8_t)((n >> 1) & 1);
	bits[2] = (uint8_t)((n >> 2) & 1);
	ambeFecEncode(bits, frame);
}

static int16_t mic[160], spk[160];

static void block(void)
{
	memset(spk, 0x55, sizeof spk);
	cb(mic, spk, NULL);
}

static bool spkIs(const uint8_t bits[49])
{
	if (spk[159] != 1000)
		return false;
	for (int i = 0; i < 49; i++)
		if (spk[i] != bits[i])
			return false;
	return true;
}

static bool spkSilent(void)
{
	for (int i = 0; i < 160; i++)
		if (spk[i] != 0)
			return false;
	return true;
}

static void testRx(void)
{
	const dmr_voice_sink_t *s = ambeVoiceSink();
	uint8_t bits[8][49], frame[9];

	s->begin(NULL, s->ctx);
	CHECK(starts == 1 && cb != NULL, "audio started on the first begin");
	block();
	CHECK(spkSilent() && resets == 1 && !speaker_on, "new call: reset, silent while priming");

	for (int n = 0; n < 2; n++) {
		makeFrame(n, bits[n], frame);
		s->frame(frame, s->ctx);
	}
	block();
	CHECK(spkSilent() && !speaker_on, "2 frames: still priming");
	makeFrame(2, bits[2], frame);
	frame[0] ^= 0x80;	// one corrected error
	s->frame(frame, s->ctx);
	block();
	CHECK(speaker_on && spkIs(bits[0]), "primed: speaker on, frame 0 plays");
	block();
	CHECK(spkIs(bits[1]), "frame 1");
	block();
	CHECK(spkIs(bits[2]), "frame 2 (with a corrected bit error)");

	// Underrun: repeat the last good frame up to 3 times, then silence
	block();
	CHECK(spkIs(bits[2]), "underrun 1 repeats");
	block();
	block();
	CHECK(spkIs(bits[2]), "underrun 3 repeats");
	block();
	CHECK(spkSilent(), "underrun 4 silent");

	// A frame with 4 errors in C0 and a lost frame are concealed
	makeFrame(3, bits[3], frame);
	s->frame(frame, s->ctx);
	makeFrame(4, bits[4], frame);
	for (int k = 0; k < 4; k++) {
		uint8_t a = ambeAirBitOfSeq((uint8_t)k);
		frame[a >> 3] ^= (uint8_t)(0x80 >> (a & 7));
	}
	s->frame(frame, s->ctx);
	s->lost(s->ctx);
	makeFrame(5, bits[5], frame);
	s->frame(frame, s->ctx);
	block();
	CHECK(spkIs(bits[3]), "frame 3");
	block();
	CHECK(spkIs(bits[3]), "bad frame 4 concealed with 3");
	block();
	CHECK(spkIs(bits[3]), "lost frame concealed with 3");
	block();
	CHECK(spkIs(bits[5]), "frame 5");

	ambe_voice_stats_t st;
	ambeVoiceGetStats(&st);
	CHECK(st.rx_underrun_at == 3, "underruns after the call's 3rd frame: %u", st.rx_underrun_at);
	CHECK(st.rx_peak == 1000, "decoded peak: the fake decoder's frame marker: %u", st.rx_peak);
	CHECK(st.rx_bad == 1 && st.rx_lost == 1 && st.rx_frames == 5 && st.rx_bit_errors == 1 && st.rx_underruns == 4,
	      "stats bad %u lost %u frames %u biterr %u underruns %u", st.rx_bad, st.rx_lost, st.rx_frames,
	      st.rx_bit_errors, st.rx_underruns);
	// Frames 0-2 fill a burst, frame 3 starts the next: the bad frame 4 is a burst's second
	CHECK(st.rx_bad_at[0] == 0 && st.rx_bad_at[1] == 1 && st.rx_bad_at[2] == 0, "bad by place %u,%u,%u",
	      st.rx_bad_at[0], st.rx_bad_at[1], st.rx_bad_at[2]);

	// End: drain what's queued, then speaker off
	makeFrame(6, bits[6], frame);
	s->frame(frame, s->ctx);
	s->end(s->ctx);
	block();
	CHECK(spkIs(bits[6]) && speaker_on, "drains after end");
	block();
	CHECK(spkSilent() && !speaker_on, "speaker off once drained");

	// Frames left from an old call are dropped when a new call starts
	int r0 = resets;
	makeFrame(7, bits[7], frame);
	s->begin(NULL, s->ctx);
	s->frame(frame, s->ctx);
	s->end(s->ctx);
	block();
	CHECK(resets == r0 + 1 && spkIs(bits[7]), "short call plays once ended, with a reset");
	block();
	CHECK(!speaker_on, "and ends");
}

static void testTx(void)
{
	const dmr_voice_source_t *src = ambeVoiceSource();
	uint8_t frame[9], bits[49];
	int r0 = resets;
	CHECK(!src->next(frame, src->ctx), "nothing before begin");
	block();
	CHECK(!src->next(frame, src->ctx), "no encoding while idle");

	src->begin(src->ctx);
	for (int n = 0; n < 3; n++) {
		for (int i = 0; i < 160; i++)
			mic[i] = (int16_t)((i * 5 + n) & 1 ? 301 : 300);
		block();
	}
	CHECK(resets == r0 + 1, "encoder reset at the start of a transmission");
	for (int n = 0; n < 3; n++) {
		CHECK(src->next(frame, src->ctx), "frame %d ready", n);
		int e = ambeFecDecode(frame, bits, NULL);
		int ok = e == 0;
		for (int i = 0; i < 49; i++)
			if (bits[i] != (((i * 5 + n) & 1) ? 1 : 0))
				ok = 0;
		CHECK(ok, "TX frame %d carries the encoded mic block", n);
	}
	CHECK(!src->next(frame, src->ctx), "queue empty");

	// Overflow when the stack doesn't pull
	for (int n = 0; n < AMBE_VOICE_QUEUE + 2; n++)
		block();
	ambe_voice_stats_t st;
	ambeVoiceGetStats(&st);
	CHECK(st.tx_overflows == 2, "tx overflows %u", st.tx_overflows);
	src->end(src->ctx);
	block();
	ambeVoiceGetStats(&st);
	uint32_t f = st.tx_frames;
	block();
	ambeVoiceGetStats(&st);
	CHECK(st.tx_frames == f, "no encoding after end");

	// Stale frames of the previous transmission are dropped at the next one
	src->begin(src->ctx);
	block();
	int got = 0;
	while (src->next(frame, src->ctx))
		got++;
	CHECK(got == 1, "only the new transmission's frame (%d)", got);
	src->end(src->ctx);
}

// Simplex: a transmission started while a call plays decodes nothing (a platform may keep both codec states in one
// block, hal_vocoder_t); the call goes on as silence, and the decoder starts fresh once the transmission is over. A call
// that begins during a transmission doesn't reset the codec then (that would reset the encoder too)
static void testSimplex(void)
{
	const dmr_voice_sink_t *s = ambeVoiceSink();
	const dmr_voice_source_t *src = ambeVoiceSource();
	uint8_t bits[8][49], frame[9];

	s->begin(NULL, s->ctx);
	for (int n = 0; n < 6; n++) {
		makeFrame(n, bits[n], frame);
		s->frame(frame, s->ctx);
	}
	block();
	CHECK(spkIs(bits[0]), "simplex: the call plays");
	int d0 = decodes, r0 = resets;
	src->begin(src->ctx);
	block();
	CHECK(spkSilent() && decodes == d0, "no decode while transmitting");
	CHECK(resets == r0 + 1, "the transmission's own reset only");
	block();
	CHECK(spkSilent() && decodes == d0, "still none");
	s->begin(NULL, s->ctx);	// a new call meanwhile
	for (int n = 6; n < 8; n++) {
		makeFrame(n, bits[n], frame);
		s->frame(frame, s->ctx);
	}
	block();
	CHECK(spkSilent() && decodes == d0 && resets == r0 + 1, "a call beginning during a transmission: no reset");
	src->end(src->ctx);
	makeFrame(5, bits[5], frame);	// a third frame: the new call primes
	s->frame(frame, s->ctx);
	s->end(s->ctx);
	block();
	CHECK(decodes == d0 + 1 && resets == r0 + 2 && spkIs(bits[6]), "after the transmission: fresh decoder, plays");
	while (src->next(frame, src->ctx))
		;
	for (int n = 0; n < 4; n++)
		block();
	CHECK(!speaker_on, "and ends");
}

// One transmission of n blocks from gen(sample index), then its mic statistics
static ambe_voice_stats_t micOver(int n, int16_t (*gen)(int))
{
	const dmr_voice_source_t *src = ambeVoiceSource();
	uint8_t out[9];
	src->begin(src->ctx);
	for (int b = 0; b < n; b++) {
		for (int i = 0; i < 160; i++)
			mic[i] = gen(b * 160 + i);
		block();
		while (src->next(out, src->ctx))
			;
	}
	ambe_voice_stats_t st;
	ambeVoiceGetStats(&st);
	src->end(src->ctx);
	block();
	return st;
}

static uint32_t lcg = 1;
static int16_t genFloor(int i)	// a cut mic: the ADC's own floor, white, a couple of LSB
{
	(void)i;
	lcg = lcg * 1103515245u + 12345u;
	return (int16_t)((int)((lcg >> 16) % 5u) - 2);
}
static int16_t genTri(int i, int period, int amp)
{
	int p = i % period, h = period / 2;
	return (int16_t)((p < h ? p * 4 * amp / period - amp : (period - p) * 4 * amp / period - amp));
}
static int16_t genHum(int i)	// a connected mic in a quiet room: mains hum, steady and loud next to the floor
{
	return genTri(i, 160, 60);
}
static int16_t genTone(int i)	// a tone burst every 200 ms (UART-injected test pattern) on top of the hum
{
	return (int16_t)(genHum(i) + ((i / 800) % 2 == 0 ? genTri(i, 7, 30) : 0));
}
static int16_t genSpeech(int i)	// syllables: 400 Hz, 150 ms loud, 100 ms quiet, a 600 ms pause each second
{
	int ms = (i / 8) % 1000;
	int amp = ms >= 400 ? 3 : (ms % 250) < 150 ? 6000 : 300;
	return genTri(i, 20, amp);
}

// The mic statistics tell speech from a cut mic and from a steady hum, which the peak can't (a click is a peak)
static void testMicStats(void)
{
	ambeVoiceResetStats();
	ambe_voice_stats_t st = micOver(200, genFloor);
	CHECK(st.tx_mic_blocks == 200, "blocks %u", st.tx_mic_blocks);
	CHECK(st.tx_mic_level_db <= 6 && st.tx_mic_spread_db <= 3 && st.tx_mic_zc >= 60,
	      "cut mic: level %u spread %u zc %u (<= 6, <= 3, >= 60)", st.tx_mic_level_db, st.tx_mic_spread_db,
	      st.tx_mic_zc);
	st = micOver(200, genHum);
	CHECK(st.tx_mic_blocks == 200, "a new transmission counts from 0: %u", st.tx_mic_blocks);
	CHECK(st.tx_mic_level_db <= 6 && st.tx_mic_spread_db <= 3 && st.tx_mic_zc <= 8,
	      "hum: level %u spread %u zc %u (<= 6: the high pass, <= 3, <= 8)", st.tx_mic_level_db, st.tx_mic_spread_db,
	      st.tx_mic_zc);
	st = micOver(200, genTone);
	CHECK(st.tx_mic_level_db >= 21 && st.tx_mic_spread_db >= 18,
	      "small tone bursts over a louder hum: level %u spread %u (>= 21, >= 18)", st.tx_mic_level_db,
	      st.tx_mic_spread_db);
	st = micOver(250, genSpeech);
	CHECK(st.tx_mic_level_db >= 60 && st.tx_mic_spread_db >= 30 && st.tx_mic_zc >= 12 && st.tx_mic_zc <= 20,
	      "speech: level %u spread %u zc %u (>= 60, >= 30, 12-20)", st.tx_mic_level_db, st.tx_mic_spread_db,
	      st.tx_mic_zc);
	ambeVoiceResetStats();
	ambeVoiceGetStats(&st);
	CHECK(st.tx_mic_blocks == 0 && st.tx_mic_spread_db == 0, "reset");
}

// The option -> flags word mapping of the AMBE+2 hosts (drivers/vocoder/ambe2_flags.h)
static void testFlagsMap(void)
{
	CHECK(ambe2_enc_flags(0) == 0x0800u, "no options: noise gate only (%#x)", (unsigned)ambe2_enc_flags(0));
	CHECK(ambe2_enc_flags(VOCODER_OPT_NOISE_REDUCTION) == 0x0840u, "noise reduction");
	CHECK(ambe2_enc_flags(VOCODER_OPT_TONE_FRAMES) == 0x1800u, "tone frames");
	CHECK(ambe2_enc_flags(VOCODER_OPT_DEFAULT) == 0x1840u, "the defaults are the full pattern");
	CHECK(AMBE2_ENC_DEFAULT == ambe2_enc_flags(VOCODER_OPT_DEFAULT), "the hosts' initial word");
	CHECK(ambe2_enc_flags(VOCODER_OPT_AGC) == 0x2800u, "AGC");
	CHECK(ambe2_enc_flags(VOCODER_OPT_DEFAULT | VOCODER_OPT_AGC) == 0x3840u, "all three");
	// Nothing ever sets the DC filter off or test mode bits, whatever is asked for
	CHECK((ambe2_enc_flags(0xFFFFFFFFu) & 0x4100u) == 0, "never 0x100, 0x4000");
	CHECK(ambe2_enc_flags(0xFFFFFFFFu) == 0x3840u, "all options");
	CHECK(AMBE2_OPTIONS == (VOCODER_OPT_NOISE_REDUCTION | VOCODER_OPT_TONE_FRAMES | VOCODER_OPT_AGC), "offered");
}

// The checks a frame counter can't make: the same stale voice bytes every burst (the HR_C7000 voice-access flag on the
// wrong slot, 2026-10-22), a stuck encoder, a silent mic. Each counter starts over with a call / transmission
static void testRepeats(void)
{
	const dmr_voice_sink_t *s = ambeVoiceSink();
	uint8_t bits[49], frame[9], other[9];

	ambeVoiceResetStats();
	s->begin(NULL, s->ctx);
	makeFrame(1, bits, frame);
	for (int n = 0; n < 6; n++)
		s->frame(frame, s->ctx);	// one frame six times: the stale RAM signature
	makeFrame(2, bits, other);
	s->frame(other, s->ctx);
	s->frame(frame, s->ctx);
	s->end(s->ctx);
	ambe_voice_stats_t st;
	ambeVoiceGetStats(&st);
	CHECK(st.rx_in == 8 && st.rx_repeats == 5, "rx repeats %u of %u (5 of 8)", st.rx_repeats, st.rx_in);
	s->begin(NULL, s->ctx);
	s->frame(frame, s->ctx);	// the first frame of a new call repeats nothing
	s->end(s->ctx);
	ambeVoiceGetStats(&st);
	CHECK(st.rx_repeats == 5, "a new call starts the comparison over: %u", st.rx_repeats);
	for (int n = 0; n < 12; n++)
		block();	// play it all out

	const dmr_voice_source_t *src = ambeVoiceSource();
	uint8_t out[9];
	ambeVoiceResetStats();
	src->begin(src->ctx);
	for (int i = 0; i < 160; i++)
		mic[i] = (int16_t)(i == 7 ? -1234 : 0);
	for (int n = 0; n < 4; n++)
		block();	// the same mic block four times: three repeats
	ambeVoiceGetStats(&st);
	CHECK(st.tx_frames == 4 && st.tx_repeats == 3, "tx repeats %u of %u (3 of 4)", st.tx_repeats, st.tx_frames);
	CHECK(st.tx_mic_peak == 1234, "mic peak %u", st.tx_mic_peak);
	while (src->next(out, src->ctx))
		;
	src->end(src->ctx);

	// The test signal replaces the mic: frames differ block after block, and the mic peak still shows the mic
	CHECK(!ambeVoiceTestSignal(), "test signal off by default");
	ambeVoiceSetTestSignal(true);
	CHECK(ambeVoiceTestSignal(), "test signal on");
	ambeVoiceResetStats();
	src->begin(src->ctx);
	memset(mic, 0, sizeof mic);
	bool differs = true;
	uint8_t prev[49] = { 0 };
	for (int n = 0; n < 7; n++) {
		block();
		CHECK(src->next(out, src->ctx), "test frame %d", n);
		CHECK(ambeFecDecode(out, bits, NULL) == 0, "test frame %d decodes", n);
		if (n > 0 && memcmp(bits, prev, sizeof bits) == 0)
			differs = false;
		memcpy(prev, bits, sizeof prev);
	}
	ambeVoiceGetStats(&st);
	CHECK(differs && st.tx_repeats == 0, "the test signal's frames all differ (repeats %u)", st.tx_repeats);
	CHECK(st.tx_mic_peak == 0, "a silent mic shows as 0 with the test signal on: %u", st.tx_mic_peak);
	src->end(src->ctx);
	ambeVoiceSetTestSignal(false);

	// The new transmission starts the mic peak over
	src->begin(src->ctx);
	mic[3] = 50;
	block();
	ambeVoiceGetStats(&st);
	CHECK(st.tx_mic_peak == 50, "mic peak of the latest transmission: %u", st.tx_mic_peak);
	while (src->next(out, src->ctx))
		;
	src->end(src->ctx);
	block();
}

// core -> hal: the options reach the vocoder before the first frame and between frames, and every codec call of a
// transmission passes the same flags (also the first call after the reset at the start)
static void testEncodeOptions(void)
{
	const dmr_voice_source_t *src = ambeVoiceSource();
	uint8_t frame[AMBE_FRAME_BYTES];

	// The vocoder without options: nothing in force, nothing supported
	CHECK(ambeVoiceEncodeOptionsSupported() == 0 && ambeVoiceEncodeOptions() == 0, "no options without hooks");

	ambeVoiceInit(&audio, &opt_vocoder);
	CHECK(ambeVoiceEncodeOptionsSupported() == AMBE2_OPTIONS, "supported");
	CHECK(ambeVoiceEncodeOptions() == VOCODER_OPT_DEFAULT, "defaults in force (%#x)", (unsigned)ambeVoiceEncodeOptions());
	opt_sets = 0;
	ncalls = 0;
	int r0 = resets;
	src->begin(src->ctx);
	for (int i = 0; i < 5; i++)
		block();
	CHECK(resets == r0 + 1, "reset at the start of the transmission");
	CHECK(opt_sets == 1 && opt_last_set == VOCODER_OPT_DEFAULT, "options given once (%u)", (unsigned)opt_sets);
	CHECK(ncalls == 10, "two calls per frame (%d)", ncalls);
	for (int i = 0; i < ncalls; i++)
		CHECK(calls_flags[i] == 0x1840u, "call %d flags %#x", i, (unsigned)calls_flags[i]);
	CHECK(opt_encode_flags() == 0x1840u, "the debug word");

	// Noise reduction off mid-transmission: from the next frame on, both halves
	ambeVoiceSetEncodeOptions(VOCODER_OPT_TONE_FRAMES);
	CHECK(opt_sets == 1, "not applied outside the audio callback");
	ncalls = 0;
	block();
	block();
	CHECK(opt_sets == 2 && opt_last_set == VOCODER_OPT_TONE_FRAMES, "applied before the next frame");
	CHECK(ncalls == 4, "calls %d", ncalls);
	for (int i = 0; i < ncalls; i++)
		CHECK(calls_flags[i] == 0x1800u, "call %d flags %#x", i, (unsigned)calls_flags[i]);
	CHECK(ambeVoiceEncodeOptions() == VOCODER_OPT_TONE_FRAMES, "in force");
	src->end(src->ctx);
	while (src->next(frame, src->ctx))
		;

	// A new transmission (vocoder reset) keeps them, and the same value isn't given again
	ncalls = 0;
	src->begin(src->ctx);
	block();
	CHECK(opt_sets == 2 && ncalls == 2 && calls_flags[0] == 0x1800u && calls_flags[1] == 0x1800u,
	      "kept across the reset (%u, %#x)", (unsigned)opt_sets, (unsigned)calls_flags[0]);
	src->end(src->ctx);
	while (src->next(frame, src->ctx))
		;

	// AGC on: in force, both halves
	ambeVoiceSetEncodeOptions(VOCODER_OPT_DEFAULT | VOCODER_OPT_AGC);
	CHECK(ambeVoiceEncodeOptions() == (VOCODER_OPT_DEFAULT | VOCODER_OPT_AGC), "AGC in force");
	ncalls = 0;
	src->begin(src->ctx);
	block();
	CHECK(ncalls == 2 && calls_flags[0] == 0x3840u && calls_flags[1] == 0x3840u, "AGC flags (%#x)",
	      (unsigned)calls_flags[0]);
	src->end(src->ctx);
	while (src->next(frame, src->ctx))
		;
	// An option the vocoder doesn't take is not in force
	ambeVoiceSetEncodeOptions(VOCODER_OPT_DEFAULT | 0x80u);
	CHECK(ambeVoiceEncodeOptions() == VOCODER_OPT_DEFAULT, "unknown option not in force");
	ncalls = 0;
	src->begin(src->ctx);
	block();
	CHECK(ncalls == 2 && calls_flags[0] == 0x1840u, "unknown option dropped (%#x)", (unsigned)calls_flags[0]);
	src->end(src->ctx);
	while (src->next(frame, src->ctx))
		;
	// A local diagnostic can change HAL options while voice is stopped; the cached word is no longer authoritative.
	ambeVoiceStop();
	opt_set(VOCODER_OPT_NOISE_REDUCTION);
	ncalls = 0;
	src->begin(src->ctx);
	block();
	CHECK(ncalls == 2 && calls_flags[0] == 0x1840u && calls_flags[1] == 0x1840u,
	      "stopped-stream option cache hid diagnostic changes");
	src->end(src->ctx);
	while (src->next(frame, src->ctx))
		;
	ambeVoiceSetEncodeOptions(VOCODER_OPT_DEFAULT);
	ambeVoiceInit(&audio, &vocoder);
}

#if CONFIG_VOICE_CAPTURE
// The capture keeps the frames as received, before the FEC, lost ones marked, up to the number armed
static void testCapture(void)
{
	const dmr_voice_sink_t *s = ambeVoiceSink();
	const ambe_cap_entry_t *e;
	uint8_t want, bits[49], frame[9], sent[3][9];
	CHECK(ambeVoiceCapture(&e, &want) == 0 && want == 0, "not armed: nothing");
	s->begin(NULL, s->ctx);
	makeFrame(1, bits, frame);
	s->frame(frame, s->ctx);
	CHECK(ambeVoiceCapture(&e, &want) == 0, "nothing before arming");
	ambeVoiceCaptureArm(3);
	makeFrame(2, bits, sent[0]);
	sent[0][4] ^= 0x10;	// an error: kept as received
	s->frame(sent[0], s->ctx);
	s->lost(s->ctx);
	makeFrame(3, bits, sent[2]);
	s->frame(sent[2], s->ctx);
	s->frame(frame, s->ctx);	// past the three asked for
	uint8_t n = ambeVoiceCapture(&e, &want);
	CHECK(n == 3 && want == 3, "three captured of three: %u of %u", n, want);
	CHECK(!e[0].lost && memcmp(e[0].frame, sent[0], 9) == 0, "the first as received, error and all");
	CHECK(e[1].lost, "a lost frame is marked");
	CHECK(!e[2].lost && memcmp(e[2].frame, sent[2], 9) == 0, "the third");
	ambeVoiceCaptureArm(200);
	CHECK(ambeVoiceCapture(&e, &want) == 0 && want == AMBE_VOICE_CAP, "armed again: cleared, capped at %u", want);
	for (unsigned i = 0; i < AMBE_VOICE_CAP + 5; i++) {
		s->frame(frame, s->ctx);
	}
	CHECK(ambeVoiceCapture(&e, &want) == AMBE_VOICE_CAP, "full");
	ambeVoiceCaptureArm(0);
	s->frame(frame, s->ctx);
	CHECK(ambeVoiceCapture(&e, &want) == 0 && want == 0, "0 stops");
	s->end(s->ctx);
}
#endif

// A vocoder that keeps the PCM it was handed and the options in force for each encode
static int16_t cap_pcm[64][160];
static uint32_t cap_opts[64];
static int ncap;
static void cap_encode(const int16_t pcm[VOCODER_FRAME_SAMPLES], uint8_t bits[VOCODER_BITS])
{
	if (ncap < 64) {
		memcpy(cap_pcm[ncap], pcm, sizeof cap_pcm[0]);
		cap_opts[ncap] = opt_last_set;
	}
	ncap++;
	fake_encode(pcm, bits);
}
static const hal_vocoder_t cap_vocoder = {
	.present = fake_present, .reset = fake_reset, .encode = cap_encode, .decode = fake_decode,
	.options = opt_options, .set_options = opt_set, .encode_flags = opt_encode_flags,
};

static void testMicGain(void)
{
	ambeVoiceInit(&audio, &cap_vocoder);
	const dmr_voice_source_t *src = ambeVoiceSource();
	ambe_voice_stats_t st;
	ambeVoiceSetMicGain(3);
	for (unsigned i = 0; i < 160; i++) mic[i] = i & 1 ? -1000 : 1000;
	ncap = 0;
	src->begin(src->ctx); block();
	bool exact = true;
	for (unsigned i = 0; i < 160; i++) exact = exact && cap_pcm[0][i] == (int32_t)mic[i] * 161 / 256;
	CHECK(exact, "manual gain preserves the legacy below-ceiling scaling");
	src->end(src->ctx); block();

	// A calibrated board uses its +12 dB even when the codec's own AGC selects manual step 5.
	ambeVoiceSetEncodeOptions(VOCODER_OPT_DEFAULT | VOCODER_OPT_AGC);
	ambeVoiceSetMicGainQ8(mic_gain_db_q8(12));
	ncap = 0;
	src->begin(src->ctx); block();
	ambeVoiceGetStats(&st);
	CHECK(cap_pcm[0][0] == 1000 * 1019 / 256 && cap_pcm[0][1] == -1000 * 1019 / 256 &&
	      st.tx_mic_peak == 1000 && st.tx_enc_peak == (uint32_t)cap_pcm[0][0] &&
	      cap_opts[0] == (VOCODER_OPT_DEFAULT | VOCODER_OPT_AGC), "board calibration and codec AGC both apply");
	CHECK(st.tx_mic_low_q8 == (CONFIG_TX_MIC_LIMITER ? 1019 : 0), "lowest gain is available with protection");
	src->end(src->ctx); block();
	ambeVoiceSetEncodeOptions(VOCODER_OPT_DEFAULT);

	// The same calibrated board at step 10 uses +22 dB exactly once, including when the limiter is omitted.
	ambeVoiceSetMicGainQ8(mic_gain_db_q8(22));
	ncap = 0;
	src->begin(src->ctx); block();
	CHECK(cap_pcm[0][0] == 1000 * 3223 / 256 && cap_pcm[0][1] == -1000 * 3223 / 256,
	      "manual gain adds to board calibration before encoding");
	src->end(src->ctx); block();

	for (unsigned i = 0; i < 160; i++) mic[i] = i & 1 ? -32768 : 32767;
	ncap = 0;
	src->begin(src->ctx); block();
	ambeVoiceGetStats(&st);
#if CONFIG_TX_MIC_LIMITER
	bool safe = true;
	for (unsigned i = 0; i < 160; i++)
		safe = safe && cap_pcm[0][i] >= -TX_LEVEL_CEILING && cap_pcm[0][i] <= TX_LEVEL_CEILING;
	CHECK(safe && st.tx_enc_peak == TX_LEVEL_CEILING && st.tx_limited_blocks == 1, "loud PCM protected before codec");
	CHECK(st.tx_mic_peak == 32768 && st.tx_gain_q16 < TX_LEVEL_UNITY && st.tx_mic_low_q8 == st.tx_gain_q16 >> 8 &&
	      mic_gain_q8_db((uint16_t)st.tx_mic_low_q8) == -1, "raw input, applied gain, and limiter telemetry");
#else
	CHECK(cap_pcm[0][0] == INT16_MAX && cap_pcm[0][1] == INT16_MIN && st.tx_enc_peak == 32768 &&
	      st.tx_limited_blocks == 0, "manual gain saturates when protection is omitted");
#endif
	src->end(src->ctx); block();

#if CONFIG_TX_MIC_LIMITER
	ambeVoiceSetMicGain(5);
	ncap = 0;
	src->begin(src->ctx); block();
	CHECK(cap_pcm[0][0] <= TX_LEVEL_CEILING && cap_pcm[0][1] >= -TX_LEVEL_CEILING, "unity gain still protects peaks");
	src->end(src->ctx); block();
	for (unsigned i = 0; i < 160; i++) mic[i] = i & 1 ? -1234 : 1234;
	ncap = 0;
	src->begin(src->ctx); block();
	ambeVoiceGetStats(&st);
	CHECK(memcmp(cap_pcm[0], mic, sizeof mic) == 0 && st.tx_limited_blocks == 0 && st.tx_gain_q16 == TX_LEVEL_UNITY,
	      "fresh call resets limiter, clean unity stays exact");
	src->end(src->ctx); block();
#if CONFIG_SIG_TONES
	static const beep_seg_t tone_step[] = { { 1000, 200 } };
	for (unsigned i = 0; i < 160; i++) mic[i] = -32768;
	ncap = 0;
	src->begin(src->ctx);
	ambeVoiceTxTone(tone_step, 1, false);
	block();
	ambeVoiceGetStats(&st);
	CHECK(st.tx_limited_blocks == 0 && st.tx_enc_peak <= 14000,
	      "discarded mic during signaling does not alter limiter telemetry");
	src->end(src->ctx); block();
#endif
#endif
	ambeVoiceSetMicGain(5);
	ambeVoiceInit(&audio, &vocoder);
}

#if CONFIG_TX_MIC_LIMITER
// Per-profile leveling: a target levels the mic and turns the codec's AGC off; the YSF profile is one-shot
static void testTxProfiles(void)
{
	ambeVoiceInit(&audio, &cap_vocoder);
	const dmr_voice_source_t *src = ambeVoiceSource();
	ambe_voice_stats_t st;
	ambeVoiceSetMicGain(5);
	ambeVoiceSetEncodeOptions(VOCODER_OPT_DEFAULT | VOCODER_OPT_AGC);
	for (unsigned i = 0; i < 160; i++) mic[i] = i & 1 ? -1000 : 1000;	// RMS 1000, about -30 dBFS

	// No target: the manual gain as before, and the codec's AGC as set
	ncap = 0;
	src->begin(src->ctx); block();
	ambeVoiceGetStats(&st);
	CHECK(cap_pcm[0][0] == 1000 && st.tx_profile == AMBE_TX_DMR && st.tx_target_rms == 0 &&
	      cap_opts[0] == (VOCODER_OPT_DEFAULT | VOCODER_OPT_AGC), "no target: manual gain, codec AGC kept");
	src->end(src->ctx); block();

	// A YSF target: only the transmission begun as YSF levels to it, the codec's AGC off for it
	ambeVoiceSetTxTarget(AMBE_TX_YSF, 3277);	// -20 dBFS
	CHECK(ambeVoiceTxTarget(AMBE_TX_YSF) == 3277 && ambeVoiceTxTarget(AMBE_TX_DMR) == 0, "targets kept per profile");
	ambeVoiceTxProfile(AMBE_TX_YSF);
	ncap = 0;
	src->begin(src->ctx);
	for (int b = 0; b < 40; b++) block();	// the gain rises at most 0.5 dB a block
	ambeVoiceGetStats(&st);
	CHECK(st.tx_profile == AMBE_TX_YSF && st.tx_target_rms == 3277, "YSF transmission takes its target");
	CHECK((cap_opts[0] & VOCODER_OPT_AGC) == 0, "codec AGC off while the leveler sets the level");
	int last = cap_pcm[ncap - 1][0];
	CHECK(last > 3000 && last < 3600, "quiet mic leveled up to the target");
	src->end(src->ctx); block();

	// The next transmission, its profile never set, is DMR again: no target, manual gain
	ncap = 0;
	src->begin(src->ctx); block();
	ambeVoiceGetStats(&st);
	CHECK(st.tx_profile == AMBE_TX_DMR && cap_pcm[0][0] == 1000, "profile is one-shot: DMR after YSF");
	src->end(src->ctx); block();

	// A loud mic with a DMR target comes down to it, peaks still under the ceiling
	ambeVoiceSetTxTarget(AMBE_TX_DMR, 3277);
	for (unsigned i = 0; i < 160; i++) mic[i] = i & 1 ? -20000 : 20000;
	ncap = 0;
	src->begin(src->ctx); block(); block();
	ambeVoiceGetStats(&st);
	CHECK(cap_pcm[ncap - 1][0] > 3000 && cap_pcm[ncap - 1][0] < 3600 && st.tx_gain_q16 < TX_LEVEL_UNITY,
	      "loud mic leveled down to the target");
	src->end(src->ctx); block();
	ambeVoiceSetTxTarget(AMBE_TX_DMR, 0);
	ambeVoiceSetTxTarget(AMBE_TX_YSF, 0);
	ambeVoiceSetEncodeOptions(VOCODER_OPT_DEFAULT);
	ambeVoiceInit(&audio, &vocoder);
}
#endif

#if CONFIG_MIC_CHECK
// The mic check meters the transmit chain while nothing is transmitted, and never encodes
static void testMicCheck(void)
{
	ambeVoiceInit(&audio, &cap_vocoder);
	ambeVoiceStart();
	ambe_mic_check_t v;
	ambeVoiceSetMicGain(5);
	for (unsigned i = 0; i < 160; i++) mic[i] = i & 1 ? -1000 : 1000;
	ncap = 0;
	for (unsigned b = 0; b < AMBE_MIC_CHECK_BLOCKS; b++) block();
	ambeVoiceMicCheckView(&v);
	CHECK(v.windows == 0 && !v.on, "off: nothing published");

	ambeVoiceSetTxTarget(AMBE_TX_YSF, 3277);
	ambeVoiceMicCheck(true, AMBE_TX_YSF);
	for (unsigned b = 0; b < AMBE_MIC_CHECK_BLOCKS; b++) block();
	ambeVoiceMicCheckView(&v);
	CHECK(v.on && v.windows == 1 && v.profile == AMBE_TX_YSF && v.target_rms == 3277, "a window published");
	CHECK(v.in_rms_db10 > -310 && v.in_rms_db10 < -290 && v.in_peak_db10 == v.in_rms_db10 && v.clips == 0,
	      "input level: -30 dBFS, no clips");
	CHECK(v.gain_db10 > 0 && v.out_rms_db10 > v.in_rms_db10, "the leveler raises a quiet mic toward the target");
	CHECK(ncap == 0, "the check never encodes");
	for (unsigned b = 0; b < 4 * AMBE_MIC_CHECK_BLOCKS; b++) block();
	ambeVoiceMicCheckView(&v);
	CHECK(v.out_rms_db10 > -210 && v.out_rms_db10 < -190, "settles at the target");

	for (unsigned i = 0; i < 160; i++) mic[i] = i & 1 ? -32768 : 32767;
	for (unsigned b = 0; b < AMBE_MIC_CHECK_BLOCKS; b++) block();
	ambeVoiceMicCheckView(&v);
	CHECK(v.clips == 160 * AMBE_MIC_CHECK_BLOCKS && v.in_peak_db10 >= -1 && v.out_peak_db10 <= -10,
	      "clipped input counted, output held under the ceiling");

	// Record, then play back: frames encoded with the codec's AGC off (a target), decoded to the speaker and metered
	for (unsigned i = 0; i < 160; i++) mic[i] = i & 1 ? -1000 : 1000;
	ambeVoiceSetEncodeOptions(VOCODER_OPT_DEFAULT | VOCODER_OPT_AGC);
	ncap = 0;
	ambeVoiceMicCheckRecord();
	for (unsigned b = 0; b < 30; b++) block();
	ambeVoiceMicCheckView(&v);
	CHECK(v.phase == AMBE_CHECK_RECORD && v.frames == 30 && ncap == 30, "recording encodes every block");
	CHECK((cap_opts[0] & VOCODER_OPT_AGC) == 0, "recorded with the codec AGC off, as a leveled transmission");
	ambeVoiceMicCheckPlay();
	agc_wave = true;
	decodes = 0;
	block();
	ambeVoiceMicCheckView(&v);
	CHECK(v.phase == AMBE_CHECK_PLAY && v.frames == 30 && decodes == 1 && spk[1] == 464, "playback decodes to the speaker");
	for (unsigned b = 0; b < 30; b++) block();	// that block recorded one more before it played: 31
	ambeVoiceMicCheckView(&v);
	CHECK(v.phase == AMBE_CHECK_LIVE && v.played == 1 && decodes == 31, "playback ends after the recording");
	CHECK(v.dec_rms_db10 > -380 && v.dec_rms_db10 < -360 && v.dec_peak_db10 == v.dec_rms_db10, "decoded level metered");
	agc_wave = false;
	// A full recording plays by itself
	ambeVoiceMicCheckRecord();
	for (unsigned b = 0; b < AMBE_MIC_CHECK_FRAMES + 1; b++) block();
	ambeVoiceMicCheckView(&v);
	CHECK(v.phase == AMBE_CHECK_PLAY, "full recording plays");
	ambeVoiceSetEncodeOptions(VOCODER_OPT_DEFAULT);

	ambeVoiceMicCheck(false, AMBE_TX_DMR);
	block();
	ambeVoiceMicCheckView(&v);
	CHECK(!v.on && v.phase == AMBE_CHECK_LIVE, "off again, playback ended");
	// Off while recording with no block in between (the stream stopped under it), then on: it starts over, live
	ambeVoiceMicCheck(true, AMBE_TX_YSF);
	block();
	ambeVoiceMicCheckRecord();
	for (unsigned b = 0; b < 5; b++) block();
	ambeVoiceMicCheckView(&v);
	CHECK(v.phase == AMBE_CHECK_RECORD, "recording");
	ambeVoiceMicCheck(false, AMBE_TX_YSF);
	ambeVoiceMicCheckView(&v);
	CHECK(!v.on && v.phase == AMBE_CHECK_LIVE, "off: the view is live at once");
	ambeVoiceMicCheck(true, AMBE_TX_YSF);
	block();
	ambeVoiceMicCheckView(&v);
	CHECK(v.on && v.phase == AMBE_CHECK_LIVE && v.windows == 0, "on again: a fresh check, not the old recording");
	ambeVoiceMicCheck(false, AMBE_TX_DMR);
	block();
	ambeVoiceSetTxTarget(AMBE_TX_YSF, 0);
	ambeVoiceStop();
	ambeVoiceInit(&audio, &vocoder);
}
#endif

static int zeroCrossings(const int16_t *p, int n)
{
	int c = 0;
	for (int i = 1; i < n; i++)
		c += (p[i - 1] < 0) != (p[i] < 0);
	return c;
}

static bool isMic(const int16_t *p)
{
	for (int i = 0; i < 160; i++)
		if (p[i] != 5000)
			return false;
	return true;
}

// A tone sequence in place of the mic (ambeVoiceTxTone): from the first block, as a tone frame with the noise reduction
// off, then the mic again; paced by the queue; final mutes the rest; busy until the call stack has the frames; a
// cancelled or unused request does not leak into the next transmission
static void testTxTone(void)
{
	ambeVoiceInit(&audio, &cap_vocoder);
	ambeVoiceSetEncodeOptions(VOCODER_OPT_DEFAULT);
	const dmr_voice_source_t *src = ambeVoiceSource();
	uint8_t frame[9];
	for (int i = 0; i < 160; i++)
		mic[i] = 5000;
	// the request comes before the transmission: it is for the first blocks
	beep_seg_t one[] = { { 1000, 40 } };
	ncap = 0;
	ambeVoiceTxTone(one, 1, false);
	CHECK(!ambeVoiceTxToneBusy(), "no transmission: nothing to wait for");
	src->begin(src->ctx);
	CHECK(ambeVoiceTxToneBusy(), "asked for: busy");
	for (int n = 0; n < 4; n++)
		block();
	CHECK(ncap == 4, "four blocks encoded (%d)", ncap);
	int c = zeroCrossings(cap_pcm[0] + 16, 128);
	CHECK(!isMic(cap_pcm[0]) && c >= 2 * 1000 * 128 / 8000 - 2 && c <= 2 * 1000 * 128 / 8000 + 2, "block 0 is the 1 kHz tone (%d)", c);
	CHECK(!isMic(cap_pcm[1]), "block 1 is the tone's second half");
	CHECK(isMic(cap_pcm[2]) && isMic(cap_pcm[3]), "the mic again after the tone");
	CHECK((cap_opts[0] & VOCODER_OPT_TONE_FRAMES) && !(cap_opts[0] & VOCODER_OPT_NOISE_REDUCTION), "tone frame, no noise reduction (%#x)", (unsigned)cap_opts[0]);
	CHECK(cap_opts[2] == VOCODER_OPT_DEFAULT, "the options back after it (%#x)", (unsigned)cap_opts[2]);
	CHECK(ambeVoiceTxToneBusy(), "frames not pulled: busy");
	int got = 0;
	while (src->next(frame, src->ctx))
		got++;
	CHECK(got == 4 && !ambeVoiceTxToneBusy(), "all pulled: not busy (%d)", got);
	src->end(src->ctx);
	block();

	// paced: 200 ms is ten frames and the queue holds eight; none is lost to an overflow
	ambeVoiceResetStats();
	beep_seg_t ten[] = { { 1500, 200 } };
	ncap = 0;
	ambeVoiceTxTone(ten, 1, false);
	src->begin(src->ctx);
	for (int n = 0; n < 14; n++)
		block();
	ambe_voice_stats_t st;
	ambeVoiceGetStats(&st);
	CHECK(ncap == AMBE_VOICE_QUEUE && st.tx_overflows == 0, "waits for room: %d encoded, %u overflows", ncap, st.tx_overflows);
	got = 0;
	for (int n = 0; n < 6; n++) {
		while (src->next(frame, src->ctx))
			got++;
		block();
		block();
		block();
	}
	CHECK(got >= 10, "the whole tone comes out (%d)", got);
	bool allTone = true;
	for (int n = 0; n < 10; n++)
		allTone = allTone && !isMic(cap_pcm[n]);
	CHECK(allTone && ncap >= 10, "ten blocks of tone");
	src->end(src->ctx);
	block();

	// final: the mic is muted after the tone until the transmission ends
	ncap = 0;
	src->begin(src->ctx);
	block();
	ambeVoiceTxTone(one, 1, true);
	for (int n = 0; n < 4; n++)
		block();
	CHECK(isMic(cap_pcm[0]) && !isMic(cap_pcm[1]) && !isMic(cap_pcm[2]), "mic, then the tone");
	bool muted = true;
	for (int i = 0; i < 160; i++)
		muted = muted && cap_pcm[3][i] == 0 && cap_pcm[4][i] == 0;
	CHECK(muted, "muted after a final tone");
	while (src->next(frame, src->ctx))
		;
	CHECK(!ambeVoiceTxToneBusy(), "pulled: not busy");
	src->end(src->ctx);
	block();

	// a request for a transmission that never started does not play in the next one
	ambeVoiceTxTone(ten, 1, false);
	src->begin(src->ctx);
	src->end(src->ctx);		// the stack's end of the over cancels it
	ncap = 0;
	src->begin(src->ctx);
	block();
	block();
	CHECK(ncap == 2 && isMic(cap_pcm[0]) && isMic(cap_pcm[1]) && !ambeVoiceTxToneBusy(), "no stale tone");
	// cancelled while it plays
	ambeVoiceTxTone(ten, 1, false);
	block();
	ambeVoiceTxToneCancel();
	block();
	CHECK(!isMic(cap_pcm[2]) && isMic(cap_pcm[3]), "the tone stops when cancelled");
	while (src->next(frame, src->ctx))
		;
	src->end(src->ctx);
	block();
	ambeVoiceInit(&audio, &vocoder);
}

// A beep's own stream left the speaker route on, and DMR voice took the running stream over (the first key-up after
// power-on or an FM channel, #705): the voice stream's first block gives the route back, though its own state says off
static void testRouteTakeover(void)
{
	ambeVoiceStop();
	fake_speaker(true);	// the beep's callback routed the speaker
	int calls = speaker_calls;
	ambeVoiceStart();
	block();
	CHECK(!speaker_on && speaker_calls == calls + 1, "the beep's route left on after the takeover (%d, %d calls)",
	      speaker_on, speaker_calls - calls);
	block();
	CHECK(speaker_calls == calls + 1, "the route sent again with nothing changed");
	ambeVoiceStop();
	ambeVoiceStart();
	block();
	CHECK(!speaker_on, "a restart with the route off");
	ambeVoiceStop();
	starts = stops = 0;
}


#if CONFIG_RX_AGC
static void testReceiveAgc(void)
{
	const dmr_voice_sink_t *s = ambeVoiceSink();
	uint8_t bits[49], frame[9];
	ambeVoiceStop();
	rx_agc_reset();
	agc_wave = true;
	for (unsigned mode = RX_AGC_DMR; mode <= RX_AGC_NXDN; mode += 2) {
		rx_agc_configure((rx_agc_mode_t)mode, 5);
		ambeVoiceRxIdentity((rx_agc_mode_t)mode, 12345, 7);
		s->begin(NULL, s->ctx);
		makeFrame(1, bits, frame);
		for (int i = 0; i < 3; i++)
			s->frame(frame, s->ctx);
		for (int i = 0; i < 65; i++) {
			s->frame(frame, s->ctx);
			block();
		}
		rx_agc_view_t v;
		rx_agc_view((rx_agc_mode_t)mode, &v);
		CHECK(v.valid && v.input_dbfs >= -38 && v.input_dbfs <= -36,
		      "mode %u measures decoded input: %d", mode, v.input_dbfs);
		CHECK(v.output_dbfs >= -26 && v.output_dbfs <= -24 && spk[159] > 1600,
		      "mode %u normalizes playback: %d, sample %d", mode, v.output_dbfs, spk[159]);
		s->end(s->ctx);
		for (int i = 0; i < 6; i++) block();
		ambeVoiceRxIdentity((rx_agc_mode_t)mode, 12345, 7);
		s->begin(NULL, s->ctx);
		for (int i = 0; i < 3; i++) s->frame(frame, s->ctx);
		block();
		rx_agc_view((rx_agc_mode_t)mode, &v);
		CHECK(v.cached && spk[159] > 1600, "mode %u recalls caller at first playback", mode);
		ambeVoiceStop();
	}
	agc_wave = false;
	rx_agc_reset();
}
#endif

// The mic gate (a beep on the speaker, which the mic hears): the gated block goes to the encoder as silence and is
// counted; the mic's own figures still see the board's samples
static bool gate;
static bool gateOn(void) { return gate; }

static void testMicGate(void)
{
	const dmr_voice_source_t *src = ambeVoiceSource();
	uint8_t frame[9], bits[49];
	ambe_voice_stats_t st;

	ambeVoiceSetMicGate(gateOn);
	src->begin(src->ctx);
	for (int n = 0; n < 2; n++) {
		gate = n == 0;
		for (int i = 0; i < 160; i++)
			mic[i] = (int16_t)(i & 1 ? 301 : 300);
		block();
		CHECK(src->next(frame, src->ctx) && ambeFecDecode(frame, bits, NULL) == 0, "frame %d", n);
		int ones = 0;
		for (int i = 0; i < 49; i++)
			ones += bits[i];
		CHECK(gate ? ones == 0 : ones == 24, "frame %d: %d bits set, gate %d", n, ones, gate);
	}
	ambeVoiceGetStats(&st);
	CHECK(st.tx_mic_muted == 1 && st.tx_mic_peak == 301, "muted %u, mic peak %u", (unsigned)st.tx_mic_muted,
	      (unsigned)st.tx_mic_peak);
	src->end(src->ctx);
	block();
	// A new transmission counts from 0
	src->begin(src->ctx);
	block();
	ambeVoiceGetStats(&st);
	CHECK(st.tx_mic_muted == 0, "muted %u in a new transmission", (unsigned)st.tx_mic_muted);
	src->end(src->ctx);
	block();
	ambeVoiceSetMicGate(NULL);
}

#if CONFIG_TX_MIC_LIMITER
// A gated block holds the limiter: the gain it cut to, and the limited count, are what the speech after the beep starts
// from. Control: the same number of quiet ungated blocks lets the gain recover
static void gateBlocks(const dmr_voice_source_t *src, int n, int16_t amp)
{
	uint8_t frame[9];
	for (int b = 0; b < n; b++) {
		for (int i = 0; i < 160; i++)
			mic[i] = (int16_t)(i & 1 ? -amp : amp);
		block();
		CHECK(src->next(frame, src->ctx), "frame queued");
	}
}

static void testMicGateHoldsLimiter(void)
{
	const dmr_voice_source_t *src = ambeVoiceSource();
	ambe_voice_stats_t cut, st;
	ambeVoiceSetMicGainQ8(mic_gain_db_q8(12));
	ambeVoiceSetMicGate(gateOn);
	for (int run = 0; run < 2; run++) {
		gate = false;
		src->begin(src->ctx);
		gateBlocks(src, 1, 32767);
		ambeVoiceGetStats(&cut);
		CHECK(cut.tx_limited_blocks == 1 && cut.tx_gain_q16 < (uint32_t)mic_gain_db_q8(12) << 8,
		      "run %d: full scale cuts the gain", run);
		gate = run == 0;
		gateBlocks(src, 5, 200);
		ambeVoiceGetStats(&st);
		if (gate)
			CHECK(st.tx_gain_q16 == cut.tx_gain_q16 && st.tx_limited_blocks == cut.tx_limited_blocks &&
			      st.tx_mic_low_q8 == cut.tx_mic_low_q8 && st.tx_mic_muted == 5,
			      "gated: gain %u (was %u), muted %u", (unsigned)st.tx_gain_q16, (unsigned)cut.tx_gain_q16,
			      (unsigned)st.tx_mic_muted);
		else
			CHECK(st.tx_gain_q16 > cut.tx_gain_q16 && st.tx_mic_muted == 0, "quiet ungated blocks recover: %u > %u",
			      (unsigned)st.tx_gain_q16, (unsigned)cut.tx_gain_q16);
		src->end(src->ctx);
		block();
	}
	gate = false;
	ambeVoiceSetMicGate(NULL);
	ambeVoiceSetMicGain(5);
}
#endif

int main(void)
{
	ambeVoiceInit(&audio, &vocoder);
	testRouteTakeover();
	testRx();
	testTx();
	testSimplex();
	testMicStats();
	testFlagsMap();
	testEncodeOptions();
	testMicGain();
#if CONFIG_TX_MIC_LIMITER
	testTxProfiles();
#endif
#if CONFIG_MIC_CHECK
	testMicCheck();
#endif
	testTxTone();
	testRepeats();
	testMicGate();
#if CONFIG_TX_MIC_LIMITER
	testMicGateHoldsLimiter();
#endif
#if CONFIG_VOICE_CAPTURE
	testCapture();
#endif
	int previous_stops = stops, previous_starts = starts;
	ambeVoiceStop();
	CHECK(stops == previous_stops + 1 && !speaker_on, "stop");
	ambeVoiceSink()->begin(NULL, NULL);
	CHECK(starts == previous_starts + 1, "restarted after stop");
#if CONFIG_RX_AGC
	testReceiveAgc();
#endif
	printf("# ambeVoiceTest: %d checks, %s\n", checks, failures ? "FAILED" : "PASSED");
	return failures ? 1 : 0;
}
