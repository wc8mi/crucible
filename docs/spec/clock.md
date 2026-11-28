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

Whether a radio's RTC keeps time with the power off depends on a backup supply, which no radio's page confirms yet.
`test_clock_rtc_across_power_off` (manual, [tests/hil](../../tests/hil/README.md)) records it. A restart keeps it.

### STM32F4

`platform/stm32f4/rtc.c`, UTC in the calendar, 24-hour, prescalers 127 and 255 for 1 Hz from the 32.768 kHz LSE.

| Rule | Why |
|---|---|
| Never reset the backup domain | It would clear the other backup registers and stop an RTC the stock firmware runs |
| Take the RTC only when its clock is the LSE, or none is chosen yet (then select the LSE) | Changing the clock needs a backup domain reset |
| Trust the calendar only with the marker `0x52544331` in `BKP17R` | Written with the time; a calendar another firmware set may be local time |
| Years 2000-2099 | The calendar holds two year digits |

| Backup register | Use |
|---|---|
| `BKP17R` | RTC calendar written by this firmware, in UTC (`rtc.c`) |
| `BKP18R` | A board's power flag, where a board needs one |
| `BKP19R` | ROM bootloader request (`sysboot.c`, `REBOOT` mode 1) |

## User interface

Settings > Location > **Date & time** (`ui_clock.c`, screen `clock`) shows the UTC date, the UTC time and the source
with its age. Each of the date and time rows edits three fields; saving one keeps the other half of the clock as it is
now, and sets the clock as MANUAL. An unset clock starts the editor at 2026-01-01 00:00:00. Years outside 2020-2099
are refused as typed. The user's keys are in [Settings](../user/17-settings.md#date--time).

The satellite pass list opens this screen when a scan ends `NO_TIME` ([satellite.md](satellite.md)).

## Locking

The clock has its own lock (`clock_set_lock`; the radio task gives the scheduler lock). The position service feeds it
from `position_nmea()` after releasing its own lock, so the two are never held together.

## Consumers

Satellite prediction reads UTC through `satellite_utc_now()` ([satellite.md](satellite.md)). Without `CONFIG_CLOCK`
it falls back to a fresh NMEA time from the position service (60 s).

## Debug link

The core view ([debuglink.md](debuglink.md)) has:

| Key | Value |
|---|---|
| `clock.utc` | UTC seconds since 1970; 0 while unset |
| `clock.src` | `none`, `rtc`, `manual`, `host` or `gps` |
| `clock.age_s` | Seconds since the source set it |
| `clock.rtc` | 1 when the board has an RTC |
