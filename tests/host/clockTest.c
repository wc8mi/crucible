// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// The UTC clock (core/clock): calendar arithmetic, counting forward across the tick wrap, which source wins, and the
// RTC writes (when, how often, never under the lock)
#include <stdio.h>
#include <string.h>
#include "clock/clock.h"
#include "clock/utc.h"

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define T2026	INT64_C(1795737600)	// 2026-11-27 00:00:00

static bool locked;
static void lock(void) { CHECK(!locked); locked = true; }
static void unlock(void) { CHECK(locked); locked = false; }

static bool rtc_has;
static int64_t rtc_value;
static unsigned rtc_writes;
static bool rtc_get(int64_t *utc_s)
{
	CHECK(!locked);
	if (!rtc_has)
		return false;
	*utc_s = rtc_value;
	return true;
}
static bool rtc_set(int64_t utc_s)
{
	CHECK(!locked);
	rtc_has = true;
	rtc_value = utc_s;
	rtc_writes++;
	return true;
}
static const clock_rtc_t rtc = { rtc_get, rtc_set };

static void test_civil(void)
{
	utc_civil_t c = { 1970, 1, 1, 0, 0, 0 };
	int64_t s;
	CHECK(utc_from_civil(&c, &s) && s == 0);
	c = (utc_civil_t){ 2026, 11, 27, 0, 0, 0 };
	CHECK(utc_from_civil(&c, &s) && s == T2026);
	c = (utc_civil_t){ 2024, 2, 29, 23, 59, 59 };
	CHECK(utc_from_civil(&c, &s) && s == INT64_C(1709251199));
	c.year = 2023;
	CHECK(!utc_from_civil(&c, &s));			// no 29 February
	c = (utc_civil_t){ 2100, 2, 29, 0, 0, 0 };
	CHECK(!utc_from_civil(&c, &s));			// 2100 isn't a leap year
	c = (utc_civil_t){ 2000, 2, 29, 0, 0, 0 };
	CHECK(utc_from_civil(&c, &s));			// 2000 is
	c = (utc_civil_t){ 1969, 12, 31, 0, 0, 0 };
	CHECK(!utc_from_civil(&c, &s));
	c = (utc_civil_t){ 2026, 13, 1, 0, 0, 0 };
	CHECK(!utc_from_civil(&c, &s));
	c = (utc_civil_t){ 2026, 1, 1, 24, 0, 0 };
	CHECK(!utc_from_civil(&c, &s));
	// Round trips, every day from 1970 to 2400 at a varying time of day
	bool ok = true;
	for (int64_t day = 0; day < 157000 && ok; day++) {
		int64_t t = day * 86400 + (day * 7919) % 86400;
		utc_civil_t r;
		int64_t back;
		ok = utc_to_civil(t, &r) && utc_from_civil(&r, &back) && back == t;
	}
	CHECK(ok);
	utc_civil_t r;
	CHECK(utc_to_civil(T2026 + 3600 * 13 + 60 * 5 + 9, &r) && r.year == 2026 && r.month == 11 && r.day == 27 &&
	      r.hour == 13 && r.minute == 5 && r.second == 9);
	CHECK(!utc_to_civil(-1, &r));
	CHECK(utc_from_nmea(271126u, 130509u, &s) && s == T2026 + 3600 * 13 + 60 * 5 + 9);
	CHECK(utc_from_nmea(10180u, 0u, &s) && s == INT64_C(315532800));	// 1980-01-01: years 80-99 are 19xx
	CHECK(!utc_from_nmea(0u, 0u, &s));
	CHECK(!utc_from_nmea(271126u, 0xFFFFFFFFu, &s));
}

static void test_counting(void)
{
	clock_set_rtc(NULL);
	clock_init(0);
	int64_t t;
	uint8_t src;
	CHECK(!clock_now(&t, &src, 0));
	CHECK(clock_age_s(0) == 0xFFFFFFFFu);
	CHECK(!clock_set(INT64_C(946684800), CLOCK_SRC_GPS, 0));	// 2000: a receiver's default, refused
	CHECK(!clock_set(T2026, CLOCK_SRC_NONE, 0));
	CHECK(clock_set(T2026, CLOCK_SRC_MANUAL, 1000));
	CHECK(clock_now(&t, &src, 1000) && t == T2026 && src == CLOCK_SRC_MANUAL);
	CHECK(clock_now(&t, NULL, 1999) && t == T2026);
	CHECK(clock_now(&t, NULL, 2000) && t == T2026 + 1);
	CHECK(clock_now(&t, NULL, 1000 + 3600000) && t == T2026 + 3600);
	CHECK(clock_age_s(1000 + 3600000) == 3600u);
	// A reader whose tick is older than the anchor gets the anchor's second
	CHECK(clock_now(&t, NULL, 500) && t == T2026 + 3600);
	// Across the tick's wrap, ticked once a minute as the radio task does (more often in fact)
	uint32_t now = 0xFFFFFFFFu - 30000u;
	CHECK(clock_set(T2026, CLOCK_SRC_GPS, now));
	for (int i = 0; i < 4; i++) {
		now += 60000u;
		clock_tick(now);
	}
	CHECK(clock_now(&t, NULL, now) && t == T2026 + 240);
	// A stale anchor (no call for 20 days) still counts right across the wrap
	CHECK(clock_set(T2026, CLOCK_SRC_GPS, 0xF0000000u));
	now = 0xF0000000u + 20u * 86400000u;
	CHECK(clock_now(&t, NULL, now) && t == T2026 + 20 * 86400);
	CHECK(clock_age_s(now) == 20u * 86400u);
}

static void test_sources(void)
{
	rtc_has = true;
	rtc_value = T2026;
	rtc_writes = 0;
	clock_set_rtc(&rtc);
	clock_init(0);
	int64_t t;
	uint8_t src;
	CHECK(clock_now(&t, &src, 0) && t == T2026 && src == CLOCK_SRC_RTC);
	clock_tick(10);
	CHECK(rtc_writes == 0);				// the RTC's own time isn't written back
	CHECK(!clock_set(T2026 + 5, CLOCK_SRC_RTC, 10));	// the RTC only seeds
	CHECK(clock_set(T2026 + 100, CLOCK_SRC_MANUAL, 20));
	CHECK(clock_now(&t, &src, 20) && t == T2026 + 100 && src == CLOCK_SRC_MANUAL);
	clock_tick(20);
	CHECK(rtc_writes == 1 && rtc_value == T2026 + 100);	// typed: written at once
	CHECK(clock_set(T2026 + 200, CLOCK_SRC_GPS, 30));
	clock_tick(30);
	CHECK(rtc_writes == 2 && rtc_value == T2026 + 200);	// the first fix: written
	// GPS every second for an hour: no more writes until an hour after the last
	uint32_t now = 30;
	for (int i = 0; i < 3599; i++) {
		now += 1000u;
		CHECK(clock_set(T2026 + 200 + i + 1, CLOCK_SRC_GPS, now));
		clock_tick(now);
	}
	CHECK(rtc_writes == 2);
	now += 1000u;
	CHECK(clock_set(T2026 + 3800, CLOCK_SRC_GPS, now));
	clock_tick(now);
	CHECK(rtc_writes == 3);
	// A typed time over GPS is taken (the user's choice); the next fix takes over again
	CHECK(clock_set(T2026 + 9000, CLOCK_SRC_MANUAL, now));
	CHECK(clock_now(NULL, &src, now) && src == CLOCK_SRC_MANUAL);
	CHECK(clock_set(T2026 + 3801, CLOCK_SRC_HOST, now + 1000));
	CHECK(clock_now(&t, &src, now + 1000) && t == T2026 + 3801 && src == CLOCK_SRC_HOST);
	// No RTC: nothing seeds, nothing is written
	clock_set_rtc(NULL);
	clock_init(0);
	CHECK(!clock_now(&t, NULL, 0));
	rtc_has = false;
	clock_set_rtc(&rtc);
	clock_init(0);
	CHECK(!clock_now(&t, NULL, 0));
}

int main(void)
{
	clock_set_lock(lock, unlock);
	test_civil();
	test_counting();
	test_sources();
	CHECK(!locked);
	printf("# clockTest: %d checks, %s\n", checks, failures ? "FAILED" : "PASSED");
	return failures != 0;
}
