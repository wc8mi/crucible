# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
"""The TX level settings and Mic check (docs/spec/radio-features.md 3.6) as the dmr_tx_audio view shows them. Read
only: nothing keys, nothing is written."""
import pytest

# core/dsp/tx_level.h tx_level_choice_rms(): Off (the manual gain) and -30..-12 dBFS in 2 dB steps
TX_LEVEL_RMS = {0, 1036, 1305, 1642, 2068, 2603, 3277, 4125, 5193, 6538, 8231}


@pytest.mark.feature("tx_mic_limiter")
def test_tx_level_targets(radio):
    """Each profile's target is one of the settings' choices; the latest transmission's is DMR's or YSF's."""
    v = radio.view(["dmr_tx_audio"])
    assert v.get("voice.mic_limiter") == "1", v
    targets = {p: int(v["voice.tx_target_" + p]) for p in ("dmr", "ysf")}
    for p, rms in targets.items():
        assert rms in TX_LEVEL_RMS, (p, rms)
    assert v["voice.tx_profile"] in ("dmr", "ysf"), v["voice.tx_profile"]
    assert int(v["voice.tx_target_rms"]) in TX_LEVEL_RMS, v["voice.tx_target_rms"]


@pytest.mark.feature("mic_check")
def test_mic_check_view(radio):
    """The Mic check keys are there and in range, open or not."""
    v = radio.view(["dmr_tx_audio"])
    assert v["mic_check.on"] in ("0", "1"), v
    assert int(v["mic_check.phase"]) in (0, 1, 2), v["mic_check.phase"]
    assert v["mic_check.profile"] in ("dmr", "ysf"), v["mic_check.profile"]
    assert 0 <= int(v["mic_check.frames"]) <= 100, v["mic_check.frames"]
    for k in ("in_rms", "in_peak", "out_rms", "out_peak", "dec_rms", "dec_peak"):
        assert -960 <= int(v["mic_check.%s_db10" % k]) <= 0, (k, v["mic_check.%s_db10" % k])
    if v["mic_check.on"] == "0":
        assert v["mic_check.phase"] == "0", "a recording or playback outlived the screen"
