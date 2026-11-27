// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Microphone gain (mic_gain.h; docs/spec/radio-features.md 3.6)
#include "radio/mic_gain.h"
#include "dsp/mic_gain.h"
#if CONFIG_DMR
#include "dmr/ambe/ambeVoice.h"
#endif
#if CONFIG_P25 && CONFIG_IMBE
#include "p25/p25Voice.h"
#endif

static volatile uint8_t mic_step = MIC_GAIN_UNITY;
#if CONFIG_P25 && CONFIG_IMBE
static volatile uint8_t p25_step = MIC_GAIN_UNITY;

static bool p25_mic_gain_on_board(void)
{
	return board.radio != NULL && board.radio->pcm_mic_gain != NULL;
}
#endif

static void apply_steps(uint8_t step, bool ambe_agc)
{
	if (step > MIC_GAIN_STEPS) {
		step = MIC_GAIN_STEPS;
	}
	mic_step = ambe_agc ? MIC_GAIN_UNITY : step;
	bool hw = mic_gain_on_board();
	if (hw) {
		board.radio->mic_gain(mic_gain_offset_db(mic_step));
	}
#if CONFIG_DMR
	ambeVoiceSetMicGainQ8(radio_mic_pcm_q8());
#endif
#if CONFIG_P25 && CONFIG_IMBE
	p25_step = step;
	p25VoiceSetMicGain(p25_mic_gain_on_board() ? MIC_GAIN_UNITY : step);
#endif
}

void radio_set_mic_gain(uint8_t step)
{
	apply_steps(step, false);
}

void radio_set_mic_gain_with_agc(uint8_t step, bool agc)
{
	bool ambe_agc = CONFIG_DMR && agc && board.vocoder != NULL && board.vocoder->options != NULL &&
		(board.vocoder->options() & VOCODER_OPT_AGC) != 0;
	apply_steps(step, ambe_agc);
}

#if CONFIG_P25 && CONFIG_IMBE
void radio_p25_mic_gain_apply(void)
{
	if (p25_mic_gain_on_board())
		board.radio->pcm_mic_gain(mic_gain_offset_db(p25VoiceAgc() ? MIC_GAIN_UNITY : p25_step));
}
#endif

uint8_t radio_mic_gain(void)
{
	return mic_step;
}

int radio_mic_pcm_db(void)
{
	// With the board's gain the step isn't in the samples: scaling them too would count it twice
	return mic_gain_board_pcm_db() + (mic_gain_on_board() ? 0 : mic_gain_offset_db(mic_step));
}

uint16_t radio_mic_pcm_q8(void)
{
	return radio_mic_pcm_gain_q8(radio_mic_gain(), mic_gain_on_board());
}

uint16_t radio_mic_pcm_gain_q8(uint8_t step, bool hardware_gain)
{
	int board_db = mic_gain_board_pcm_db();
	uint8_t pcm_step = hardware_gain ? MIC_GAIN_UNITY : step;
	if (board_db == 0) {
		return mic_gain_step_q8(pcm_step);
	}
	return mic_gain_db_q8(board_db + mic_gain_offset_db(pcm_step));
}
