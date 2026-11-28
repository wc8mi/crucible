// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// M17 voice (core/m17/m17_voice.c) and the Codec 2 benchmark (core/m17/m17_bench.c) on the host: the benchmark's
// hashes, which the radio's run is checked against; the synthesized test speech through the encoder, the transmit
// queue, the receive queue and the decoder, bit for bit as the codec alone; priming, a call shorter than the
// priming, a late frame, a new call, a full queue.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m17/m17_bench.h"
#include "m17/m17_voice.h"

static int failures, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t fnv(uint32_t h, const void *p, size_t n)
{
	const uint8_t *b = p;

	while (n--) {
		h ^= *b++;
		h *= 16777619u;
	}
	return h;
}
#define FNV0	2166136261u

// FNV-1a of the Codec 2 bits of tests/data/voice/test_transmission.pcm: a regression of the whole codec on speech
#define SPEECH_BITS_HASH	0xf7a75934u

// ── The benchmark, with a clock that ticks once per read ──
static uint32_t clock_now, pauses;
static uint32_t tick(void) { return clock_now++; }
static void pause_cb(void) { pauses++; }

static void testBench(void)
{
	static m17_bench_mem_t mem;
	m17_bench_io_t io = { tick, pause_cb };
	m17_bench_result_t r;

	m17_bench_run(&mem, &io, &r);
	printf("bench: enc %08x dec %08x\n", r.enc_hash, r.dec_hash);
	CHECK(r.enc_frames == M17_BENCH_ENC_FRAMES && r.dec_frames == M17_BENCH_DEC_FRAMES, "frames %u %u", r.enc_frames,
	      r.dec_frames);
	CHECK(r.enc_hash == M17_BENCH_ENC_HASH, "the encoder's bits as codec2-fixed-point's reference: %08x", r.enc_hash);
	CHECK(r.dec_hash == M17_BENCH_DEC_HASH, "the decoder's samples: %08x", r.dec_hash);
	CHECK(m17_bench_ok(&r), "the run is good");
	CHECK(r.enc_us == M17_BENCH_ENC_FRAMES && r.enc_max_us == 1 && r.dec_us == M17_BENCH_DEC_FRAMES,
	      "each frame timed once: %u %u %u", r.enc_us, r.enc_max_us, r.dec_us);
	CHECK(pauses == M17_BENCH_ENC_FRAMES + M17_BENCH_DEC_FRAMES, "a pause after each frame: %u", pauses);
	io.pause = NULL;
	m17_bench_run(&mem, &io, &r);
	CHECK(m17_bench_ok(&r), "without a pause");
}

// ── Voice ──
static m17_voice_mem_t vmem;
static c2fxp_encoder_t ref_enc;
static c2fxp_decoder_t ref_dec;

static int16_t *load_pcm(const char *path, size_t *blocks)
{
	FILE *f = fopen(path, "rb");
	int16_t *p = NULL;
	long n;

	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	p = malloc((size_t)n);
	if (p && fread(p, 1, (size_t)n, f) != (size_t)n) {
		free(p);
		p = NULL;
	}
	fclose(f);
	*blocks = (size_t)n / (M17_VOICE_SAMPLES * 2);
	return p;
}

static bool silent(const int16_t *s)
{
	for (int i = 0; i < M17_VOICE_SAMPLES; i++)
		if (s[i])
			return false;
	return true;
}

static void testSpeech(void)
{
	size_t blocks = 0;
	int16_t *pcm = load_pcm("../data/voice/test_transmission.pcm", &blocks);
	int16_t spk[M17_VOICE_SAMPLES], want[M17_VOICE_SAMPLES];
	m17_voice_stats_t st;

	CHECK(pcm != NULL && blocks == 104, "the test speech: %zu blocks", blocks);
	if (!pcm)
		return;
	size_t nframes = blocks / 2;
	uint8_t (*pl)[M17_STREAM_BYTES] = calloc(nframes, M17_STREAM_BYTES);

	// Transmit: a block per 20 ms, the engine takes a stream frame every other block
	m17_voice_begin(&vmem);
	m17_voice_tx(true);
	c2fxp_encoder_init(&ref_enc);
	uint32_t h = FNV0;
	size_t taken = 0;
	bool same = true;
	for (size_t b = 0; b < blocks; b++) {
		uint8_t bits[M17_VOICE_BYTES];
		const int16_t *mic = &pcm[b * M17_VOICE_SAMPLES];
		c2fxp_encode(&ref_enc, bits, mic);
		h = fnv(h, bits, sizeof bits);
		m17_voice_block(mic, spk);
		CHECK(silent(spk), "the speaker is quiet while transmitting");
		if ((b & 1) && taken < nframes && m17_voice_tx_take(pl[taken]))
			taken++;
	}
	printf("speech bits %08x\n", h);
	CHECK(h == SPEECH_BITS_HASH, "the codec's bits for the test speech: %08x", h);
	CHECK(taken == nframes, "a stream frame per 40 ms: %zu of %zu", taken, nframes);
	uint8_t rest[M17_STREAM_BYTES];
	CHECK(!m17_voice_tx_take(rest), "nothing left over");
	m17_voice_stats(&st);
	CHECK(st.tx_encoded == blocks && st.tx_frames == blocks && st.tx_dropped == 0, "tx stats %u %u %u", st.tx_encoded,
	      st.tx_frames, st.tx_dropped);

	// The payloads hold the codec's own bits, in order
	c2fxp_encoder_init(&ref_enc);
	for (size_t b = 0; b < blocks; b++) {
		uint8_t bits[M17_VOICE_BYTES];
		c2fxp_encode(&ref_enc, bits, &pcm[b * M17_VOICE_SAMPLES]);
		same &= !memcmp(bits, &pl[b / 2][(b & 1) * M17_VOICE_BYTES], M17_VOICE_BYTES);
	}
	CHECK(same, "the stream frames carry the codec's frames in order");

	// Receive: a stream frame every 40 ms, a block every 20 ms. Playback starts after the priming; every block played is
	// the codec's own decode of that frame
	m17_voice_tx(false);
	c2fxp_decoder_init(&ref_dec);
	size_t played = 0, quiet_lead = 0, f = 0;
	same = true;
	for (size_t b = 0; b < blocks + 8; b++) {
		if (!(b & 1) && f < nframes) {
			m17_voice_rx(pl[f], f == 0);
			f++;
			if (f == nframes)
				m17_voice_rx_end();
		}
		m17_voice_block(NULL, spk);
		if (played < blocks && !(played == 0 && silent(spk))) {
			c2fxp_decode(&ref_dec, want, &pl[played / 2][(played & 1) * M17_VOICE_BYTES]);
			same &= !memcmp(spk, want, sizeof spk);
			played++;
		} else if (played == 0) {
			quiet_lead++;
		} else {
			CHECK(silent(spk), "silence after the call (block %zu)", b);
		}
	}
	CHECK(same && played == blocks, "every frame decoded as the codec decodes it: %zu of %zu", played, blocks);
	CHECK(quiet_lead == M17_VOICE_PRIME / 2u, "playback waits for the priming: %zu quiet blocks", quiet_lead);
	m17_voice_stats(&st);
	CHECK(st.rx_in == blocks && st.rx_frames == blocks && st.rx_calls == 1 && st.rx_underruns == 0 &&
	      st.rx_overflows == 0, "rx stats in %u played %u calls %u under %u over %u", st.rx_in, st.rx_frames,
	      st.rx_calls, st.rx_underruns, st.rx_overflows);
	m17_voice_end();
	m17_voice_block(NULL, spk);
	CHECK(silent(spk), "ended: silence");
	m17_voice_rx(pl[0], true);
	CHECK(!m17_voice_tx_take(rest), "ended: nothing taken");
	free(pl);
	free(pcm);
}

// A payload: any bits decode to something, and a frame's bits differ from the next one's
static void payload(uint8_t *p, unsigned n)
{
	for (int i = 0; i < M17_STREAM_BYTES; i++)
		p[i] = (uint8_t)(n * 37u + (unsigned)i * 11u);
}

static void testQueue(void)
{
	uint8_t p[M17_STREAM_BYTES];
	int16_t spk[M17_VOICE_SAMPLES];
	m17_voice_stats_t st, st0;

	// A call of one stream frame, shorter than the priming: it plays once it ended
	m17_voice_begin(&vmem);
	m17_voice_stats(&st0);
	payload(p, 1);
	m17_voice_rx(p, true);
	m17_voice_block(NULL, spk);
	CHECK(silent(spk), "one frame queued: priming");
	m17_voice_rx_end();
	int n = 0;
	for (int i = 0; i < 4; i++) {
		m17_voice_block(NULL, spk);
		n += !silent(spk);
	}
	m17_voice_stats(&st);
	CHECK(n == 2 && st.rx_frames - st0.rx_frames == 2 && st.rx_underruns == st0.rx_underruns, "a short call plays out: %u",
	      st.rx_frames - st0.rx_frames);

	// A late frame: the last one plays again once, then silence, then the call goes on
	st0 = st;
	for (unsigned k = 0; k < 2; k++) {
		payload(p, 10 + k);
		m17_voice_rx(p, k == 0);
	}
	for (int i = 0; i < 4; i++)
		m17_voice_block(NULL, spk);
	m17_voice_stats(&st);
	CHECK(st.rx_frames - st0.rx_frames == 4, "primed and played: %u", st.rx_frames - st0.rx_frames);
	m17_voice_block(NULL, spk);
	CHECK(!silent(spk), "the first late block repeats the last frame");
	m17_voice_block(NULL, spk);
	CHECK(silent(spk), "the second is silence");
	m17_voice_stats(&st);
	CHECK(st.rx_underruns - st0.rx_underruns == 2 && st.rx_concealed - st0.rx_concealed == 2, "two underruns: %u",
	      st.rx_underruns - st0.rx_underruns);
	payload(p, 12);
	m17_voice_rx(p, false);
	m17_voice_block(NULL, spk);
	m17_voice_stats(&st);
	CHECK(st.rx_frames - st0.rx_frames == 5, "the call goes on without priming again");

	// A new call while the last one's frames wait: they are dropped, the new one primes
	st0 = st;
	m17_voice_rx(p, false);
	payload(p, 20);
	m17_voice_rx(p, true);
	m17_voice_block(NULL, spk);	// the old call's frames are dropped for the new call
	m17_voice_block(NULL, spk);
	m17_voice_stats(&st);
	CHECK(st.rx_frames == st0.rx_frames && st.rx_calls == st0.rx_calls, "the new call waits for its priming");
	payload(p, 21);
	m17_voice_rx(p, false);
	m17_voice_block(NULL, spk);
	m17_voice_stats(&st);
	CHECK(st.rx_calls == st0.rx_calls + 1 && st.rx_frames == st0.rx_frames + 1, "the new call plays: calls %u frames %u",
	      st.rx_calls - st0.rx_calls, st.rx_frames - st0.rx_frames);

	// A full queue drops what doesn't fit
	m17_voice_begin(&vmem);
	for (unsigned k = 0; k < M17_VOICE_QUEUE / 2 + 2; k++) {
		payload(p, k);
		m17_voice_rx(p, k == 0);
	}
	m17_voice_stats(&st);
	CHECK(st.rx_overflows - st0.rx_overflows == 4, "a full queue drops 4: %u", st.rx_overflows - st0.rx_overflows);

	// Transmit: the engine falling behind drops frames, and one frame is not a stream frame
	m17_voice_stats(&st0);
	m17_voice_tx(true);
	for (int i = 0; i < M17_VOICE_TX_QUEUE + 3; i++)
		m17_voice_block(NULL, spk);
	m17_voice_stats(&st);
	CHECK(st.tx_dropped - st0.tx_dropped == 3, "tx drops 3: %u", st.tx_dropped - st0.tx_dropped);
	for (int i = 0; i < M17_VOICE_TX_QUEUE / 2; i++)
		CHECK(m17_voice_tx_take(p), "take %d", i);
	CHECK(!m17_voice_tx_take(p), "empty");
	m17_voice_block(NULL, spk);
	CHECK(!m17_voice_tx_take(p), "one frame: not yet");
	m17_voice_block(NULL, spk);
	CHECK(m17_voice_tx_take(p), "two frames: a stream frame");
	m17_voice_end();
}

int main(void)
{
	testBench();
	testSpeech();
	testQueue();
	printf("m17VoiceTest: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
