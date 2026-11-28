# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 David Behnke
"""M17 on the radio (docs/spec/m17.md).

test_m17_bench: the Codec 2 benchmark (debuglink.md 3.8 op 45, m17.md 5), the CPU gate of ADR 006 5. The channel on
view becomes FM by a temporary mode change if it isn't (the benchmark runs in memory another air mode holds), the
benchmark runs to its end, and the channel's mode is put back. Pass: every frame run and both hashes as a host
computes them (m17.bench_ok=1). The RTOS heap (the tasks view: free now, and the least since start, which then counts
the benchmark task's 2 KB stack) is printed before and after the run. The share of the 20 ms frame is printed, not
judged: the owner records it per radio in the bring-up records, and the gate decides M17 per platform (encode at
most 50 %; the receive path, step 2, at most 60 %). Nothing is sent.
"""
import pytest

from rfoundry_hil import as_int, skip

BENCH_S = 60.0


@pytest.mark.feature("m17")
def test_m17_bench(radio):
    radio.go_home()
    if radio.core().get("hotspot") == "1":
        skip("state", "the radio is in hotspot mode")
    if radio.view(["m17"]).get("m17.built") != "1":
        skip("not_built", "built without CONFIG_M17")
    snap = radio.snapshot()
    mode0 = radio.core().get("mode")
    h0 = radio.view(["tasks"])
    try:
        if mode0 != "fm":
            radio.link.dmr_fields(mode="fm")
            radio.wait_for(lambda x: x.get("mode") == "fm", 3.0, what="the channel in FM")
        v = radio.link.m17_bench(timeout=BENCH_S)
    finally:
        if mode0 != "fm":
            if snap["vfo"] == "1":      # the VFO's mode is its own: restore() only drops a channel's mode for now
                radio.link.dmr_fields(mode=mode0)
            radio.restore(snap)
    h1 = radio.view(["tasks"])
    print(f"\nm17 heap: free {h0.get('tasks.heap_free')} B before, {h1.get('tasks.heap_free')} B after; least since "
          f"start {h0.get('tasks.heap_min_free')} B before, {h1.get('tasks.heap_min_free')} B after the run")
    print(f"m17 bench: encode {v.get('m17.enc_us')} us avg ({v.get('m17.enc_pct')} %), "
          f"{v.get('m17.enc_max_us')} us max ({v.get('m17.enc_max_pct')} %); "
          f"decode {v.get('m17.dec_us')} us avg ({v.get('m17.dec_pct')} %), "
          f"{v.get('m17.dec_max_us')} us max ({v.get('m17.dec_max_pct')} %)")
    assert v.get("m17.bench") == "done", f"the benchmark did not finish: {v.get('m17.bench')}"
    assert (as_int(v.get("m17.enc_frames")) or 0) == 300 and (as_int(v.get("m17.dec_frames")) or 0) == 64, v
    assert v.get("m17.bench_ok") == "1", \
        f"the codec computed other values than on a host: enc {v.get('m17.enc_hash')} dec {v.get('m17.dec_hash')}"
    if mode0 != "fm":
        radio.wait_for(lambda x: x.get("mode") == mode0, 3.0, what=f"mode {mode0} back")
