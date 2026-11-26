// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The DMR GET_VIEW reply budget with modem health diagnostics and a full talker alias and target name.
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "debuglink/debuglink.h"
#include "debuglink/dl_frame.h"
#include "dmr/ambe/ambeVoice.h"
#include "dmr/call/dmr_call.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

_Static_assert(DL_VIEW_DMR == 2 && DL_VIEW_DMR_TX_AUDIO == 39, "GET_VIEW section numbers");

static const ambe_voice_stats_t voice = {
	.tx_mic_peak = 32768, .tx_enc_peak = 28000, .tx_gain_q16 = 1038592,
	.tx_limited_blocks = CONFIG_TX_MIC_LIMITER ? 300 : 0,
	.tx_mic_low_q8 = CONFIG_TX_MIC_LIMITER ? 218 : 0,
	.tx_mic_level_db = 51, .tx_mic_spread_db = 21, .tx_mic_zc = 84, .tx_mic_blocks = 300, .tx_mic_muted = 4,
	.tx_profile = AMBE_TX_YSF, .tx_target_rms = 2068,
};

static void health(dmr_modem_health_t *h)
{
	*h = (dmr_modem_health_t){ .stalls = 1, .first_ms = 987654, .first_regs = { 0x1, 0x40, 0x80, 0 } };
}

static const hal_radio_t radio;
static const hal_dmr_modem_t modem = { .health = health };
static const hal_vocoder_t vocoder;
const board_t board = { .radio = &radio, .dmr = &modem, .vocoder = &vocoder };

void ambeVoiceGetStats(ambe_voice_stats_t *s) { *s = voice; }
bool ambeVoiceTestSignal(void) { return true; }
uint32_t ambeVoiceEncodeOptions(void) { return 7; }
uint32_t ambeVoiceEncodeOptionsSupported(void) { return 7; }
void ambeVoiceResetStats(void) {}
#if CONFIG_TX_MIC_LIMITER
uint16_t ambeVoiceTxTarget(ambe_tx_profile_t profile) { return profile == AMBE_TX_YSF ? 2068 : 3277; }
#endif
#if CONFIG_MIC_CHECK
// The widest a Mic check reading prints: every field at its longest
void ambeVoiceMicCheckView(ambe_mic_check_t *out)
{
	*out = (ambe_mic_check_t){ .windows = 4294967295u, .in_peak_db10 = -960, .in_rms_db10 = -960, .out_peak_db10 = -960,
		.out_rms_db10 = -960, .gain_db10 = -240, .clips = 65535, .limited = 65535, .profile = AMBE_TX_YSF, .on = 1,
		.phase = AMBE_CHECK_PLAY, .frames = 100, .played = 65535, .dec_rms_db10 = -960, .dec_peak_db10 = -960 };
}
#endif

const dmr_call_view_t *dmr_call_view(void)
{
	static const dmr_call_view_t call;
	return &call;
}

void dmr_call_get_stats(dmr_call_stats_t *s) { memset(s, 0, sizeof(*s)); }
void dmr_call_reset_stats(void) {}
void dmr_call_tx_alias(char out[DMR_TA_TEXT_MAX + 1])
{
	memset(out, 'A', DMR_TA_TEXT_MAX);
	out[DMR_TA_TEXT_MAX] = '\0';
}

int radio_mic_pcm_db(void) { return 22; }
void radio_get_sfr_settings(uint8_t *rx, uint8_t *cwid)
{
	*rx = DMR_SFR_RX_DUALPATH;
	*cwid = 60;
}

int main(void)
{
	static radio_view_t view;
	view.dmr = (radio_dmr_view_t){
		.active = true, .codec = true, .state = RDMR_IDLE, .own_id = 0xFFFFFFu,
		.cc = 15, .ts = 2, .rx_cc = 15, .rx_ts = 2, .rmo = true, .repeater_up = true,
		.target_id = 0xFFFFFFu, .target_private = true, .reply_active = true, .reply_s = 10,
		.target_manual = true, .privacy = true, .alias_tx = true, .gps_tx_channel = true,
		.hear_all = true, .hear_all_latch = true, .focus_id = 0xFFFFFFu, .sfr_role = 2, .sfr_rpt = true,
		.sfr_cwid_sent = 6, .sfr_cwid_cut = 1,
		.focus_private = true, .one_shot = true, .underruns = 3,
	};
	memset(view.dmr.target_name, 'N', CODEPLUG_NAME_LEN);

	// cmd_get_view keeps 6 bytes for the header and 16 for the truncation marker.
	char text[DL_REPLY_MAX - 6u - 16u];
	dl_text_t t;
	dl_text_init(&t, text, sizeof(text));
	dl_text(&t, "section=dmr\n");
	debuglink_view_dmr(0, &view, &t);
	CHECK(!t.full);
	CHECK(strstr(text, "dmr.tx_alias=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n") != NULL);
	CHECK(strstr(text, "dmr.target_name=NNNNNNNNNNNNNNNN\n") != NULL);
	CHECK(strstr(text, "voice.tx_enc_peak=28000\n") != NULL);
	CHECK(strstr(text, "voice.mic_pcm_db=22\n") != NULL);
	CHECK(strstr(text, CONFIG_TX_MIC_LIMITER ? "voice.tx_mic_low_db=-1\n" : "voice.tx_mic_low_db=-\n") != NULL);
	CHECK(strstr(text, "dmr.modem_lost_regs=1,40,80,0\n") != NULL);
	CHECK(strstr(text, "voice.tx_mic_level_db=51\nvoice.tx_mic_spread_db=21\nvoice.tx_mic_zc=84\n"
			   "voice.tx_mic_blocks=300\n") != NULL);
	size_t dmr_size = t.len;

	dl_text_init(&t, text, sizeof(text));
	dl_text(&t, "section=dmr_tx_audio\n");
	debuglink_view_dmr_tx_audio(&t);
	CHECK(!t.full);
	CHECK(strstr(text, "section=dmr_tx_audio\nvoice.tx_gain_q16=1038592\n") == text);
	CHECK(strstr(text, CONFIG_TX_MIC_LIMITER ? "voice.tx_limited_blocks=300\nvoice.mic_limiter=1\n" :
						   "voice.tx_limited_blocks=0\nvoice.mic_limiter=0\n") != NULL);
	CHECK(strstr(text, "voice.tx_mic_muted=4\n") != NULL);
	CHECK(strstr(text, "voice.tx_profile=ysf\nvoice.tx_target_rms=2068\n") != NULL);
#if CONFIG_TX_MIC_LIMITER
	CHECK(strstr(text, "voice.tx_target_dmr=3277\nvoice.tx_target_ysf=2068\n") != NULL);
#endif
#if CONFIG_MIC_CHECK
	CHECK(strstr(text, "mic_check.on=1\nmic_check.phase=2\nmic_check.profile=ysf\n") != NULL);
	CHECK(strstr(text, "mic_check.dec_rms_db10=-960\nmic_check.dec_peak_db10=-960\n") != NULL);
#endif
	printf("dmrViewTest: dmr %zu bytes, dmr_tx_audio %zu bytes, %d failures\n", dmr_size, t.len, failures);
	return failures != 0;
}
