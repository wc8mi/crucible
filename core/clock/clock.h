// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// The UTC clock (docs/spec/clock.md): the time of day for satellite prediction when no GPS fix is fresh. One anchor,
// a UTC second and the millisecond tick it was true at, counted forward from hal_millis. Set from a GPS or computer
// fix (RMC status A), by hand, or at start-up from the board's real-time clock. Any task may call it; the radio task
// ticks it (clock_tick), which also does every RTC write, so no other task touches the RTC
#ifndef CLOCK_CLOCK_H
#define CLOCK_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

// Where the time came from, in rising trust; MANUAL is above RTC (the user just set it), GPS and HOST above both
typedef enum {
	CLOCK_SRC_NONE,
	CLOCK_SRC_RTC,		// the real-time clock at start-up
	CLOCK_SRC_MANUAL,	// typed in Settings
	CLOCK_SRC_HOST,		// a computer: NMEA with a fix, or the debug link's clock command
	CLOCK_SRC_GPS,		// the board GPS with a fix
} clock_src_t;

#define CLOCK_RTC_SYNC_MS	3600000u	// a GPS or computer time is written to the RTC once an hour at most

typedef struct {
	bool (*get)(int64_t *utc_s);	// false: no RTC or never set
	bool (*set)(int64_t utc_s);
} clock_rtc_t;

void clock_set_lock(void (*lock)(void), void (*unlock)(void));
// The RTC's calls (NULL: none). Read once by clock_init
void clock_set_rtc(const clock_rtc_t *rtc);
// Start-up: forget the time, then take the RTC's if it has one
void clock_init(uint32_t now_ms);
// A new time from a source. False when refused: a time before 2020 or past 9999, or an RTC time once anything has
// set the clock
bool clock_set(int64_t utc_s, uint8_t src, uint32_t now_ms);
// The time now; false while nothing has set it. src may be NULL
bool clock_now(int64_t *utc_s, uint8_t *src, uint32_t now_ms);
// Seconds since the source last set the time (0xFFFFFFFF while unset); for the "GPS, 3 h ago" line
uint32_t clock_age_s(uint32_t now_ms);
// From the radio task's loop: keeps the anchor fresh across the 49.7-day tick wrap and writes the RTC when due.
// Called at least every 24 days (in practice every loop), since an anchor older than that reads as the future
void clock_tick(uint32_t now_ms);

#endif
