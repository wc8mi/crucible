# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
# Copyright (c) 2026 David Behnke
"""The TX level settings (docs/spec/radio-features.md 3.6) and Digital AGC (docs/crucible/adr/003-digital-agc.md) as
the dmr_tx_audio and audio views show them. Read only: nothing keys, nothing is written."""
import pytest

# core/dsp/tx_level.h tx_level_choice_rms(): Off (the manual gain) and -30..-12 dBFS in 2 dB steps
TX_LEVEL_RMS = {0, 1036, 1305, 1642, 2068, 2603, 3277, 4125, 5193, 6538, 8231}
P25_DEFAULT_RMS = 3277	# -20 dBFS, every radio


@pytest.mark.feature("tx_mic_limiter")
def test_tx_level_targets(radio):
    """Each profile's target is one of the settings' choices; the latest transmission's is DMR's, NXDN's or YSF's."""
    v = radio.view(["dmr_tx_audio"])
    assert v.get("voice.mic_limiter") == "1", v
    targets = {p: int(v["voice.tx_target_" + p]) for p in ("dmr", "ysf", "nxdn")}
    for p, rms in targets.items():
        assert rms in TX_LEVEL_RMS, (p, rms)
    assert v["voice.tx_profile"] in ("dmr", "ysf", "nxdn"), v["voice.tx_profile"]
    assert int(v["voice.tx_target_rms"]) in TX_LEVEL_RMS, v["voice.tx_target_rms"]


@pytest.mark.feature("digital_agc")
def test_digital_agc_levels(radio):
    """With Digital AGC TX On, DMR, NXDN and YSF share the radio's default and P25 is -20 dBFS; with RX On, every mode
    uses one RX choice."""
    a = radio.view(["audio"])
    t = radio.view(["dmr_tx_audio"])
    assert a.get("agc.rx_auto") in ("0", "1") and a.get("agc.tx_auto") in ("0", "1"), a
    if a["agc.tx_auto"] == "1":
        ambe = {int(t["voice.tx_target_" + p]) for p in ("dmr", "ysf", "nxdn")}
        assert len(ambe) == 1 and ambe <= TX_LEVEL_RMS - {0}, ambe
        if "voice.tx_target_p25" in a:
            assert int(a["voice.tx_target_p25"]) == P25_DEFAULT_RMS, a["voice.tx_target_p25"]
    if a["agc.rx_auto"] == "1" and a.get("agc.built") == "1":
        choices = {a["agc.%d.choice" % m] for m in range(4)}
        assert len(choices) == 1 and choices != {"0"}, choices
