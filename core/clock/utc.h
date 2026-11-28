// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// UTC calendar arithmetic: seconds since 1970-01-01 00:00:00 UTC to and from a civil date and time (proleptic
// Gregorian, no leap seconds, which is what TLE epochs and NMEA give). No state, no locks
#ifndef CLOCK_UTC_H
#define CLOCK_UTC_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
	uint16_t year;		// 1970..9999
	uint8_t month;		// 1..12
	uint8_t day;		// 1..31
	uint8_t hour, minute, second;
} utc_civil_t;

bool utc_leap_year(uint32_t year);
// Days in a month of a year (1..12); 0 for a bad month
uint8_t utc_month_days(uint32_t year, uint32_t month);
// A civil date and time to seconds; false for a field out of range (a date that doesn't exist, a year before 1970)
bool utc_from_civil(const utc_civil_t *c, int64_t *utc_s);
// Seconds (>= 0) to a civil date and time; false before 1970 or after 9999
bool utc_to_civil(int64_t utc_s, utc_civil_t *c);
// NMEA's RMC date (ddmmyy, years 80-99 are 19xx) and time (hhmmss) to seconds
bool utc_from_nmea(uint32_t date_ddmmyy, uint32_t utc_hms, int64_t *utc_s);

#endif
