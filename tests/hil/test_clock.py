# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 David Behnke
"""The UTC clock (docs/spec/clock.md) through the debug link core view: set from a computer's NMEA, kept by the RTC
across a restart and, asked of a person, across power off. Whether a radio's RTC keeps time with the power off is what
the last test finds out; the docs don't claim it."""
import time
from datetime import datetime, timezone

import pytest

from rfoundry_hil import as_int, skip

TOLERANCE_S = 5         # the link's round trips and a second's rounding at each end


def _clock(radio):
    v = radio.view(["core"])
    if "clock.utc" not in v:
        skip("not_built", "firmware without CONFIG_CLOCK")
    return v


def _rmc_now():
    """An RMC with a fix (status A) at this second, a fixed position, and its checksum."""
    t = datetime.now(timezone.utc)
    body = f"GPRMC,{t:%H%M%S},A,3344.940,N,08423.280,W,000.0,000.0,{t:%d%m%y},,"
    sum_ = 0
    for ch in body:
        sum_ ^= ord(ch)
    return f"${body}*{sum_:02X}"


def _near_now(v):
    return abs(as_int(v["clock.utc"]) - time.time()) <= TOLERANCE_S


def _set_from_host(radio):
    if "APRS" not in radio.features():
        skip("not_built", "built without CONFIG_APRS: no host NMEA command")
    radio.link.aprs_nmea(_rmc_now())
    return radio.wait_for(lambda x: x.get("clock.src") == "host" and _near_now(x), timeout=5.0,
                          what="the clock set from host NMEA")


@pytest.mark.feature("clock")
def test_clock_from_host_nmea(radio):
    """An RMC with a fix sets the clock, source "host", and it counts on."""
    _clock(radio)
    v = _set_from_host(radio)
    assert as_int(v["clock.age_s"]) <= TOLERANCE_S, v
    time.sleep(3.0)
    later = radio.view(["core"])
    assert _near_now(later) and as_int(later["clock.utc"]) >= as_int(v["clock.utc"]) + 2, (v, later)


@pytest.mark.feature("clock")
@pytest.mark.reboot
def test_clock_rtc_across_restart(radio):
    """A board with an RTC comes back from a restart with the time, source "rtc" (a reset keeps the backup domain)."""
    v = _clock(radio)
    if v.get("clock.rtc") != "1":
        skip("not_built", "the board has no RTC (hal_system_t.rtc_get)")
    _set_from_host(radio)
    time.sleep(1.0)     # the radio task writes the RTC at its next tick
    radio.link.reboot(0, radio.link.chip()["token"])
    radio.link.close()
    time.sleep(2.0)
    radio.reconnect(20.0)
    after = radio.wait_for(lambda c: c.get("clock.src") is not None, timeout=5.0, what="the core view")
    assert after["clock.src"] in ("rtc", "gps"), after     # a GPS with a fix may have taken over already
    assert _near_now(after), after


@pytest.mark.feature("clock", optional=True, owner="power the radio off for 10 s and on again when the test asks")
@pytest.mark.manual
def test_clock_rtc_across_power_off(radio):
    """Records whether the RTC keeps time with the power off (a backup supply for the RTC). Optional: a radio without
    one fails here, which is the answer, not a fault."""
    v = _clock(radio)
    if v.get("clock.rtc") != "1":
        skip("not_built", "the board has no RTC (hal_system_t.rtc_get)")
    _set_from_host(radio)
    time.sleep(1.0)
    print("\n>>> Turn the radio OFF, wait 10 s, turn it ON (waiting up to 120 s)", flush=True)
    radio.link.close()
    time.sleep(5.0)
    radio.reconnect(120.0, what="the power cycle")
    after = radio.wait_for(lambda c: c.get("clock.src") is not None, timeout=5.0, what="the core view")
    print(f"    clock.src={after.get('clock.src')} clock.utc={after.get('clock.utc')}")
    assert after["clock.src"] in ("rtc", "gps") and _near_now(after), after
