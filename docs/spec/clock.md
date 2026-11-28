<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (c) 2026 David Behnke -->

# UTC clock

`core/clock` keeps UTC for satellite prediction when no GPS or computer fix is fresh. It is behind `CONFIG_CLOCK`
([build-config.md](../build-config.md)).

## Model

One anchor: a UTC second and the `hal_millis()` tick it began at. The time now is the anchor plus the whole seconds
elapsed since. Reads and `clock_tick()` move the anchor forward in whole seconds, so the difference stays under a
second and the 32-bit tick's 49.7-day wrap never matters. The radio task ticks the clock every loop; an anchor left
alone for more than 24 days would read as the future.

Times before 2020-01-01 are refused: a receiver without a fix reports its default date. No leap seconds; UTC as NMEA
and TLE epochs give it.

## Sources

| Source | Sets the clock | Notes |
|---|---|---|
| GPS | An RMC sentence with status `A` and a valid date and time, from the board GPS | Before a fix a receiver may send its own unsynced time or GPS time without leap seconds |
| HOST | The same, from a computer's NMEA | |
| MANUAL | A time typed in Settings | Overrides a GPS or computer time until the next fix |
| RTC | The board's real-time clock, once at start-up | Ignored once any other source has set the clock |

The last source to set the clock wins, except that the RTC only seeds an unset clock. `clock_age_s()` gives the
seconds since the source last set it.

## Real-time clock

A board with a battery-backed RTC gives `rtc_get`/`rtc_set` ([clock.h](../../core/clock/clock.h), `clock_rtc_t`).
Writes happen only in `clock_tick()`, in the radio task, outside the clock's lock:

| Event | RTC write |
|---|---|
| Time typed | At the next tick |
| First GPS or computer fix since start-up | At the next tick |
| Later fixes | At most once an hour (`CLOCK_RTC_SYNC_MS`) |
| Start-up from the RTC | None |

## Locking

The clock has its own lock (`clock_set_lock`; the radio task gives the scheduler lock). The position service feeds it
from `position_nmea()` after releasing its own lock, so the two are never held together.

## Consumers

Satellite prediction reads UTC through `satellite_utc_now()` ([satellite.md](satellite.md)). Without `CONFIG_CLOCK`
it falls back to a fresh NMEA time from the position service (60 s).
