// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The mic gain setting (core/radio/mic_gain.c, docs/spec/radio-features.md 3.6): a step 0..10 goes to the board's
// hal_radio_t.mic_gain as a dB offset, or, without the hook, to the PCM gain. A board's hal_radio_t.mic_pcm_db adds
// to the AMBE PCM gain at every step.
#include <stdio.h>
#include "radio/mic_gain.h"
#include "dmr/ambe/ambeVoice.h"
#include "p25/p25Voice.h"

static int failures;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static hal_radio_t radio;
const board_t board = { .radio = &radio };

static int hook_calls;
static int8_t hook_db;
static int8_t pcm_hook_db;
static uint16_t ambe_q8;
static uint8_t p25_step;
static uint16_t p25_target;

static void hook(int8_t db)
{
	hook_calls++;
	hook_db = db;
}

static void pcm_hook(int8_t db) { pcm_hook_db = db; }

void ambeVoiceSetMicGainQ8(uint16_t q8)
{
	ambe_q8 = q8;
}

void p25VoiceSetMicGain(uint8_t step)
{
	p25_step = step;
}

void p25VoiceSetTxTarget(uint16_t rms) { p25_target = rms; }
bool p25VoiceAgc(void) { return CONFIG_P25_TX_AGC && p25_target != 0; }

int main(void)
{
	CHECK(radio_mic_gain() == MIC_GAIN_UNITY);
	CHECK(mic_gain_offset_db(0) == -10 && mic_gain_offset_db(5) == 0 && mic_gain_offset_db(10) == 10);
	CHECK(mic_gain_offset_db(7) == 4 && mic_gain_offset_db(200) == 10);

	// No board hook: the step scales the PCM before the encoders (DMR's, pushed)
	CHECK(!mic_gain_on_board() && mic_gain_available());
	CHECK(radio_mic_pcm_db() == 0 && radio_mic_pcm_q8() == 256);
	radio_set_mic_gain(8);
	CHECK(ambe_q8 == 511 && p25_step == 8 && hook_calls == 0 && radio_mic_gain() == 8);
	CHECK(radio_mic_pcm_db() == 6 && radio_mic_pcm_q8() == ambe_q8);
	radio_set_mic_gain(0);
	CHECK(ambe_q8 == 81 && p25_step == 0 && radio_mic_pcm_db() == -10);
	radio_set_mic_gain(3);
	CHECK(ambe_q8 == 161 && p25_step == 3 && radio_mic_pcm_db() == -4);

	// A quiet board (12 dB, as one radio has): its gain at the middle step, the step around it
	radio.mic_pcm_db = 12;
	radio_set_mic_gain(MIC_GAIN_UNITY);
	CHECK(radio_mic_pcm_db() == 12 && ambe_q8 == 1019 && p25_step == 5);
	radio_set_mic_gain(MIC_GAIN_STEPS);
	CHECK(radio_mic_pcm_db() == 22 && ambe_q8 == 3223 && p25_step == 10);
	radio_set_mic_gain(0);
	CHECK(radio_mic_pcm_db() == 2 && ambe_q8 == 322 && p25_step == 0);
	radio.mic_pcm_db = 0;

	// An FM/AMBE gain hook applies its step to those modes; P25 still scales in software.
	radio.mic_gain = hook;
	CHECK(mic_gain_on_board());
	radio_set_mic_gain(8);
	CHECK(hook_calls == 1 && hook_db == 6 && ambe_q8 == 256 && p25_step == 8 && radio_mic_pcm_db() == 0);
	radio_set_mic_gain(0);
	CHECK(hook_calls == 2 && hook_db == -10 && ambe_q8 == 256 && p25_step == 0 && radio_mic_gain() == 0);
	radio_set_mic_gain(42);		// clamped
	CHECK(hook_calls == 3 && hook_db == 10 && radio_mic_gain() == MIC_GAIN_STEPS);
	// The board's hook and a PCM gain together: the step in the board's path, only the board's dB on the PCM
	radio.mic_pcm_db = 4;
	radio_set_mic_gain(8);
	CHECK(hook_db == 6 && radio_mic_pcm_db() == 4 && ambe_q8 == 406 && p25_step == 8);
	radio_p25_mic_gain_apply();
	CHECK(pcm_hook_db == 0);

	// The P25 ADC hook consumes the step exactly once, with or without an FM/AMBE gain hook.
	radio.pcm_mic_gain = pcm_hook;
	radio_set_mic_gain(8);
	radio_p25_mic_gain_apply();
	CHECK(hook_db == 6 && pcm_hook_db == 6 && ambe_q8 == 406 && p25_step == 5);
	radio.mic_gain = NULL;
	radio.mic_pcm_db = 0;
	radio_set_mic_gain(3);
	radio_p25_mic_gain_apply();
	CHECK(hook_db == 6 && pcm_hook_db == -4 && ambe_q8 == 161 && p25_step == 5);
	// Mic AGC no longer turns P25 leveling on (Digital AGC: the P25 TX level does); with a target the ADC is unity
	radio_set_mic_gain_with_agc(3, true);
	radio_p25_mic_gain_apply();
	CHECK(pcm_hook_db == -4);
	p25VoiceSetTxTarget(3277);
	radio_p25_mic_gain_apply();
	CHECK(pcm_hook_db == (CONFIG_P25_TX_AGC ? 0 : -4));
	p25VoiceSetTxTarget(0);

	if (failures) {
		printf("micGainTest: %d failures\n", failures);
		return 1;
	}
	printf("micGainTest: all passed\n");
	return 0;
}
