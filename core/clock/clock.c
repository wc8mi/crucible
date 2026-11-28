// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// The UTC clock (clock.h). One state block under LOCK(); the RTC is only called from clock_tick and clock_init, both
// in the radio task, never under the lock
#include <stddef.h>
#include "config.h"
#include "clock/clock.h"

#if CONFIG_CLOCK

#define CLOCK_MIN_UTC_S		INT64_C(1577836800)	// 2020-01-01: older is a receiver's default, not the time
#define CLOCK_MAX_UTC_S		INT64_C(253402300799)	// 9999-12-31 23:59:59

static void nolock(void) {}
static void (*lock_fn)(void) = nolock;
static void (*unlock_fn)(void) = nolock;
#define LOCK()		lock_fn()
#define UNLOCK()	unlock_fn()

static clock_rtc_t rtc;

static struct {
	uint8_t src;		// clock_src_t; NONE: the rest means nothing
	int64_t utc_s;		// the anchor: this UTC second ...
	uint32_t anchor_ms;	// ... began at this tick
	int64_t set_utc_s;	// the UTC second the source last set (the age counts in UTC, so no tick wrap)
	bool rtc_due;		// write the RTC at the next tick
	bool rtc_written;	// written since start-up
	bool rtc_fix_written;	// ... with a GPS or computer time
	uint32_t rtc_ms;	// last written
} c;

void clock_set_lock(void (*lock)(void), void (*unlock)(void))
{
	lock_fn = lock != NULL ? lock : nolock;
	unlock_fn = unlock != NULL ? unlock : nolock;
}

void clock_set_rtc(const clock_rtc_t *r)
{
	rtc = r != NULL ? *r : (clock_rtc_t){ 0 };
}

// Moves the anchor up to now in whole seconds, so (now - anchor_ms) stays under a second and never meets the tick's
// wrap. A now_ms older than the anchor (a task that read the tick before another set the clock) counts as the anchor
static void advance(uint32_t now_ms)
{
	uint32_t elapsed = now_ms - c.anchor_ms;
	if ((int32_t)elapsed <= 0)
		return;
	uint32_t n = elapsed / 1000u;
	c.anchor_ms += n * 1000u;
	c.utc_s += n;
}

void clock_init(uint32_t now_ms)
{
	LOCK();
	c.src = CLOCK_SRC_NONE;
	c.rtc_due = false;
	c.rtc_written = false;
	c.rtc_fix_written = false;
	UNLOCK();
	int64_t utc_s;
	if (rtc.get != NULL && rtc.get(&utc_s))
		(void)clock_set(utc_s, CLOCK_SRC_RTC, now_ms);
}

bool clock_set(int64_t utc_s, uint8_t src, uint32_t now_ms)
{
	if (src == CLOCK_SRC_NONE || src > CLOCK_SRC_GPS || utc_s < CLOCK_MIN_UTC_S || utc_s > CLOCK_MAX_UTC_S)
		return false;
	LOCK();
	if (src == CLOCK_SRC_RTC && c.src != CLOCK_SRC_NONE) {
		UNLOCK();
		return false;
	}
	c.src = src;
	c.utc_s = utc_s;
	c.anchor_ms = now_ms;
	c.set_utc_s = utc_s;
	// Typed: written at once. A fix: the first of each start-up, then hourly, which keeps the RTC's drift small
	// without a flash-like write every second (the RTC isn't flash, but the write stops it for a moment)
	if (src == CLOCK_SRC_MANUAL || (src != CLOCK_SRC_RTC && (!c.rtc_fix_written || !c.rtc_written ||
								 now_ms - c.rtc_ms >= CLOCK_RTC_SYNC_MS)))
		c.rtc_due = true;
	UNLOCK();
	return true;
}

bool clock_now(int64_t *utc_s, uint8_t *src, uint32_t now_ms)
{
	LOCK();
	if (c.src == CLOCK_SRC_NONE) {
		UNLOCK();
		return false;
	}
	advance(now_ms);
	if (utc_s != NULL)
		*utc_s = c.utc_s;
	if (src != NULL)
		*src = c.src;
	UNLOCK();
	return true;
}

uint32_t clock_age_s(uint32_t now_ms)
{
	LOCK();
	uint32_t age = 0xFFFFFFFFu;
	if (c.src != CLOCK_SRC_NONE) {
		advance(now_ms);
		int64_t d = c.utc_s - c.set_utc_s;
		age = d > (int64_t)0xFFFFFFFEu ? 0xFFFFFFFEu : (uint32_t)d;
	}
	UNLOCK();
	return age;
}

void clock_tick(uint32_t now_ms)
{
	LOCK();
	bool write = false;
	int64_t utc_s = 0;
	uint8_t src = CLOCK_SRC_NONE;
	if (c.src != CLOCK_SRC_NONE) {
		advance(now_ms);
		if (c.rtc_due && rtc.set != NULL) {
			write = true;
			utc_s = c.utc_s;
			src = c.src;
			c.rtc_due = false;
		}
	}
	UNLOCK();
	if (!write)
		return;
	bool ok = rtc.set(utc_s);
	LOCK();
	if (ok) {
		c.rtc_written = true;
		c.rtc_ms = now_ms;
		if (src == CLOCK_SRC_GPS || src == CLOCK_SRC_HOST)
			c.rtc_fix_written = true;
	}
	UNLOCK();
}
#endif
