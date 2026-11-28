// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// UTC calendar arithmetic (utc.h). Days are counted with the 400-year era method, so no loop over the years
#include <stddef.h>
#include "clock/utc.h"

bool utc_leap_year(uint32_t year)
{
	return year % 4u == 0u && (year % 100u != 0u || year % 400u == 0u);
}

uint8_t utc_month_days(uint32_t year, uint32_t month)
{
	static const uint8_t days[] = { 31u, 28u, 31u, 30u, 31u, 30u, 31u, 31u, 30u, 31u, 30u, 31u };
	if (month == 0u || month > 12u)
		return 0u;
	return (uint8_t)(days[month - 1u] + (month == 2u && utc_leap_year(year) ? 1u : 0u));
}

// Days since 1970-01-01 of a valid date (an era is 400 years, 146,097 days; March first so the leap day is last)
static int64_t days_from_civil(uint32_t y, uint32_t m, uint32_t d)
{
	y -= m <= 2u ? 1u : 0u;
	uint32_t era = y / 400u;
	uint32_t yoe = y - era * 400u;
	uint32_t doy = (153u * (m > 2u ? m - 3u : m + 9u) + 2u) / 5u + d - 1u;
	uint32_t doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
	return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

bool utc_from_civil(const utc_civil_t *c, int64_t *utc_s)
{
	if (c == NULL || utc_s == NULL || c->year < 1970u || c->year > 9999u || c->day == 0u ||
	    c->day > utc_month_days(c->year, c->month) || c->hour > 23u || c->minute > 59u || c->second > 59u)
		return false;
	*utc_s = days_from_civil(c->year, c->month, c->day) * 86400 + (int64_t)c->hour * 3600 +
		 (int64_t)c->minute * 60 + c->second;
	return true;
}

bool utc_to_civil(int64_t utc_s, utc_civil_t *c)
{
	if (c == NULL || utc_s < 0 || utc_s > INT64_C(253402300799))	// 9999-12-31 23:59:59
		return false;
	int64_t days = utc_s / 86400;
	uint32_t sod = (uint32_t)(utc_s % 86400);
	uint32_t z = (uint32_t)(days + 719468);
	uint32_t era = z / 146097u;
	uint32_t doe = z - era * 146097u;
	uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
	uint32_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
	uint32_t mp = (5u * doy + 2u) / 153u;
	uint32_t d = doy - (153u * mp + 2u) / 5u + 1u;
	uint32_t m = mp < 10u ? mp + 3u : mp - 9u;
	uint32_t y = yoe + era * 400u + (m <= 2u ? 1u : 0u);
	c->year = (uint16_t)y;
	c->month = (uint8_t)m;
	c->day = (uint8_t)d;
	c->hour = (uint8_t)(sod / 3600u);
	c->minute = (uint8_t)(sod / 60u % 60u);
	c->second = (uint8_t)(sod % 60u);
	return true;
}

bool utc_from_nmea(uint32_t date_ddmmyy, uint32_t utc_hms, int64_t *utc_s)
{
	if (date_ddmmyy > 999999u || utc_hms > 999999u)
		return false;
	uint32_t short_year = date_ddmmyy % 100u;
	utc_civil_t c = {
		.year = (uint16_t)(short_year >= 80u ? 1900u + short_year : 2000u + short_year),
		.month = (uint8_t)(date_ddmmyy / 100u % 100u),
		.day = (uint8_t)(date_ddmmyy / 10000u),
		.hour = (uint8_t)(utc_hms / 10000u),
		.minute = (uint8_t)(utc_hms / 100u % 100u),
		.second = (uint8_t)(utc_hms % 100u),
	};
	return utc_from_civil(&c, utc_s);
}
