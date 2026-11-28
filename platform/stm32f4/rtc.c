// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// The STM32F4 RTC as the clock's real-time clock (hal_system_t.rtc_get/rtc_set, docs/spec/clock.md), in UTC on the
// LSE crystal. It never resets the backup domain: that would also clear the backup registers other code keeps there
// (BKP19, the ROM DFU request; a board's own) and stop an RTC another firmware set. So it takes the RTC only when its
// clock is the LSE, or not chosen yet, and trusts the calendar only after this firmware wrote it: BKP17 holds a marker
// written with the time (a calendar set by other firmware may be local time)
#include <stdbool.h>
#include <stdint.h>
#include "config.h"
#include "platform.h"
#include "stm32f4xx.h"

#if CONFIG_CLOCK
#include "clock/utc.h"

#define RTC_MARK	0x52544331u	// "RTC1": the calendar is UTC, written by this firmware
#define RTC_SPIN	200000u		// a bounded wait: INITF and RSF come within 2 RTC clocks (about 61 us)

static void backup_access(void)
{
	RCC->APB1ENR |= RCC_APB1ENR_PWREN;
	(void)RCC->APB1ENR;
	PWR->CR |= PWR_CR_DBP;
}

static bool spin(volatile uint32_t *reg, uint32_t mask)
{
	for (uint32_t i = 0; i < RTC_SPIN; i++) {
		if ((*reg & mask) != 0) {
			return true;
		}
	}
	return false;
}

// The RTC runs on the LSE: the crystal oscillates, it is the RTC's clock and the RTC is on
static bool on_lse(void)
{
	uint32_t bdcr = RCC->BDCR;
	return (bdcr & RCC_BDCR_LSERDY) != 0 && (bdcr & RCC_BDCR_RTCSEL) == RCC_BDCR_RTCSEL_0 &&
	       (bdcr & RCC_BDCR_RTCEN) != 0;
}

static uint32_t bcd(uint32_t v)
{
	return (v / 10u) << 4 | v % 10u;
}

static uint32_t unbcd(uint32_t v)
{
	return (v >> 4) * 10u + (v & 0xFu);
}

bool stm32_rtc_get(int64_t *utc_s)
{
	RCC->APB1ENR |= RCC_APB1ENR_PWREN;
	(void)RCC->APB1ENR;
	if (!on_lse() || RTC->BKP17R != RTC_MARK || (RTC->ISR & RTC_ISR_INITS) == 0) {
		return false;
	}
	// The shadow registers are fresh once RSF sets again. Reading TR locks DR until DR is read, so the pair is one
	// instant
	backup_access();
	RTC->WPR = 0xCAu;
	RTC->WPR = 0x53u;
	RTC->ISR &= ~RTC_ISR_RSF;
	RTC->WPR = 0xFFu;
	if (!spin(&RTC->ISR, RTC_ISR_RSF)) {
		return false;
	}
	uint32_t tr = RTC->TR;
	uint32_t dr = RTC->DR;
	utc_civil_t c = {
		.year = (uint16_t)(2000u + unbcd(dr >> 16 & 0xFFu)),
		.month = (uint8_t)unbcd(dr >> 8 & 0x1Fu),
		.day = (uint8_t)unbcd(dr & 0x3Fu),
		.hour = (uint8_t)unbcd(tr >> 16 & 0x3Fu),
		.minute = (uint8_t)unbcd(tr >> 8 & 0x7Fu),
		.second = (uint8_t)unbcd(tr & 0x7Fu),
	};
	return utc_from_civil(&c, utc_s);
}

bool stm32_rtc_set(int64_t utc_s)
{
	utc_civil_t c;
	if (!utc_to_civil(utc_s, &c) || c.year < 2000u || c.year > 2099u) {
		return false;	// the calendar holds two year digits
	}
	backup_access();
	uint32_t bdcr = RCC->BDCR;
	if ((bdcr & RCC_BDCR_LSERDY) == 0) {
		return false;	// no crystal, or it hasn't started (the platform turns it on at start-up)
	}
	if ((bdcr & RCC_BDCR_RTCSEL) == 0) {
		RCC->BDCR = bdcr | RCC_BDCR_RTCSEL_0;	// no clock chosen yet: the LSE (set once per backup domain)
	} else if ((bdcr & RCC_BDCR_RTCSEL) != RCC_BDCR_RTCSEL_0) {
		return false;	// another clock: changing it would need a backup domain reset
	}
	RCC->BDCR |= RCC_BDCR_RTCEN;

	uint32_t wday = (uint32_t)((utc_s / 86400 + 3) % 7) + 1u;	// 1970-01-01 was a Thursday; Monday is 1
	RTC->WPR = 0xCAu;
	RTC->WPR = 0x53u;
	RTC->ISR |= RTC_ISR_INIT;
	bool ok = spin(&RTC->ISR, RTC_ISR_INITF);
	if (ok) {
		// 32768 Hz / 128 / 256 = 1 Hz; the two prescalers are two writes, synchronous first
		RTC->PRER = 255u;
		RTC->PRER = 127u << 16 | 255u;
		RTC->TR = bcd(c.hour) << 16 | bcd(c.minute) << 8 | bcd(c.second);
		RTC->DR = bcd(c.year - 2000u) << 16 | wday << 13 | bcd(c.month) << 8 | bcd(c.day);
		RTC->CR &= ~RTC_CR_FMT;		// 24-hour
	}
	RTC->ISR &= ~RTC_ISR_INIT;
	RTC->WPR = 0xFFu;
	if (ok) {
		RTC->BKP17R = RTC_MARK;
	}
	return ok;
}

#endif
