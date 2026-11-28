// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// Settings > Location > Date & time (docs/spec/clock.md): the UTC clock satellite prediction reads, where it came from
// and how long ago, and the time typed by hand when no GPS or computer fix gives it.
//
// Two rows, the date and the time, each three fields. OK opens a row's editor on its first field. Digits type the field
// from the left and move on when it is full; Up / Down (or the knob) step it, for radios without a keypad. OK goes to
// the next field and, on the last, saves; a long OK saves at once. BACK takes a digit back, then the field before,
// then leaves without a change. Saving the date keeps the clock's time of day, saving the time keeps its date.

#include "config.h"
#if CONFIG_CLOCK
#include "ui_nav.h"
#include "ui_svc.h"
#include "fmt.h"
#include "i18n.h"
#include "clock/clock.h"
#include "clock/utc.h"

enum { CK_DATE, CK_TIME, CK_SOURCE, CK_COUNT };
#define CK_YEAR_MIN	2020u		// older is refused by the clock
#define CK_YEAR_MAX	2099u		// a real-time clock holds two year digits
#define CK_YEAR_UNSET	2026u		// where the editor starts when the clock was never set

static bool ck_editing;
static uint8_t ck_field;		// 0..2 in the row
static uint8_t ck_typed_n;		// digits typed into the field (0: showing its value)
static uint16_t ck_typed;		// those digits
static utc_civil_t ck_c;		// what the editor holds

static uint8_t ck_width(uint16_t row, uint8_t f) { return row == CK_DATE && f == 0u ? 4u : 2u; }

static uint32_t ck_get(uint16_t row, uint8_t f)
{
	if (row == CK_DATE)
		return f == 0u ? ck_c.year : f == 1u ? ck_c.month : ck_c.day;
	return f == 0u ? ck_c.hour : f == 1u ? ck_c.minute : ck_c.second;
}

static uint32_t ck_min(uint16_t row, uint8_t f)
{
	return row == CK_DATE ? (f == 0u ? CK_YEAR_MIN : 1u) : 0u;
}

static uint32_t ck_max(uint16_t row, uint8_t f)
{
	if (row == CK_DATE)
		return f == 0u ? CK_YEAR_MAX : f == 1u ? 12u : utc_month_days(ck_c.year, ck_c.month);
	return f == 0u ? 23u : 59u;
}

static void ck_put(uint16_t row, uint8_t f, uint32_t v)
{
	if (row == CK_DATE) {
		if (f == 0u)
			ck_c.year = (uint16_t)v;
		else if (f == 1u)
			ck_c.month = (uint8_t)v;
		else
			ck_c.day = (uint8_t)v;
		uint8_t last = utc_month_days(ck_c.year, ck_c.month);
		if (ck_c.day > last)
			ck_c.day = last;	// 31 January to February: its last day
	} else if (f == 0u) {
		ck_c.hour = (uint8_t)v;
	} else if (f == 1u) {
		ck_c.minute = (uint8_t)v;
	} else {
		ck_c.second = (uint8_t)v;
	}
}

static void ck_num(sbuf_t *b, uint32_t v, uint8_t width)
{
	sb_u32(b, v, width);
}

// "2026-11-27" / "13:05:09"; in the editor the field being changed is in brackets, the digits still to type as
// underscores ("[20__]-11-27")
static void ck_text(sbuf_t *b, uint16_t row, bool editing)
{
	for (uint8_t f = 0; f < 3u; f++) {
		if (f != 0u)
			sb_chr(b, row == CK_DATE ? '-' : ':');
		bool here = editing && f == ck_field;
		if (here)
			sb_chr(b, '[');
		uint8_t w = ck_width(row, f);
		if (here && ck_typed_n != 0u) {
			char d[5];
			uint16_t v = ck_typed;
			for (int k = ck_typed_n - 1; k >= 0; k--, v /= 10u)
				d[k] = (char)('0' + v % 10u);
			for (uint8_t k = 0; k < w; k++)
				sb_chr(b, k < ck_typed_n ? d[k] : '_');
		} else {
			ck_num(b, ck_get(row, f), w);
		}
		if (here)
			sb_chr(b, ']');
	}
}

// "GPS, 3m": the source and the age in its largest unit
static void ck_source(sbuf_t *b, uint8_t src, uint32_t age_s)
{
	switch (src) {
	case CLOCK_SRC_GPS: sb_str(b, TR(CLOCK_SRC_GPS, "GPS")); break;
	case CLOCK_SRC_HOST: sb_str(b, TR(CLOCK_SRC_COMPUTER, "Computer")); break;
	case CLOCK_SRC_MANUAL: sb_str(b, TR(CLOCK_SRC_TYPED, "Typed")); break;
	default: sb_str(b, TR(CLOCK_SRC_RTC, "RTC")); break;
	}
	sb_str(b, ", ");
	if (age_s < 60u) {
		sb_u32(b, age_s, 1);
		sb_chr(b, 's');
	} else if (age_s < 3600u) {
		sb_u32(b, age_s / 60u, 1);
		sb_chr(b, 'm');
	} else if (age_s < 86400u) {
		sb_u32(b, age_s / 3600u, 1);
		sb_chr(b, 'h');
	} else {
		sb_u32(b, age_s / 86400u, 1);
		sb_chr(b, 'd');
	}
}

static uint16_t ck_count(void) { return CK_COUNT; }

static void ck_item(uint16_t i, ui_item_t *it)
{
	static const tr_id_t names[] = { TRN(CLOCK_LABEL_DATE, "UTC date"), TRN(CLOCK_LABEL_TIME, "UTC time"),
					 TRN(CLOCK_LABEL_SOURCE, "Source") };
	str_copy(it->label, sizeof it->label, tr(names[i]));
	sbuf_t b;
	sb_init(&b, it->value, sizeof it->value);
	int64_t utc_s;
	uint8_t src;
	uint32_t age_s;
	bool set = ui_svc_clock_now(&utc_s, &src, &age_s);
	if (i == CK_SOURCE) {
		it->flags |= UI_ITEM_INFO;
		if (set)
			ck_source(&b, src, age_s);
		else
			sb_str(&b, TR(VALUE_NOT_SET, "Not set"));
		return;
	}
	it->num = (char)('1' + i);
	if (ck_editing && i == ui_frame()->focus) {
		ck_text(&b, i, true);
		it->flags |= UI_ITEM_EDIT;
		return;
	}
	utc_civil_t now_c = ck_c;
	if (!set || !utc_to_civil(utc_s, &ck_c)) {
		ck_c = now_c;
		sb_str(&b, TR(VALUE_NOT_SET, "Not set"));
		return;
	}
	ck_text(&b, i, false);
	ck_c = now_c;
}

static void ck_open(void)
{
	int64_t utc_s;
	if (!ui_svc_clock_now(&utc_s, NULL, NULL) || !utc_to_civil(utc_s, &ck_c) || ck_c.year > CK_YEAR_MAX)
		ck_c = (utc_civil_t){ CK_YEAR_UNSET, 1u, 1u, 0u, 0u, 0u };
	ck_editing = true;
	ck_field = 0;
	ck_typed_n = 0;
}

// The edited row over the clock as it is now: the date keeps its time of day, the time its date (both counted on
// while the editor was open)
static void ck_save(uint16_t row)
{
	utc_civil_t now_c;
	int64_t utc_s;
	if (!ui_svc_clock_now(&utc_s, NULL, NULL) || !utc_to_civil(utc_s, &now_c))
		now_c = (utc_civil_t){ ck_c.year, ck_c.month, ck_c.day, 0u, 0u, 0u };
	if (row == CK_DATE) {
		now_c.year = ck_c.year;
		now_c.month = ck_c.month;
		now_c.day = ck_c.day;
	} else {
		now_c.hour = ck_c.hour;
		now_c.minute = ck_c.minute;
		now_c.second = ck_c.second;
	}
	ck_editing = false;
	if (!utc_from_civil(&now_c, &utc_s) || !ui_svc_clock_set(utc_s)) {
		ui_toast(TR(CLOCK_TOAST_REFUSED, "Time not set"));
		return;
	}
	ui_toast(TR(CLOCK_TOAST_SET, "Time set"));
}

static void ck_select(uint16_t i, bool hold)
{
	if (i == CK_SOURCE)
		return;
	if (!ck_editing) {
		ck_open();
	} else if (hold || ck_field == 2u) {
		ck_save(i);
	} else {
		ck_field++;
		ck_typed_n = 0;
	}
	ui_dirty();
}

static void ck_step(uint16_t row, int dir)
{
	uint32_t lo = ck_min(row, ck_field), hi = ck_max(row, ck_field), v = ck_get(row, ck_field);
	if (dir > 0)
		v = v >= hi ? lo : v + 1u;
	else
		v = v <= lo ? hi : v - 1u;
	ck_put(row, ck_field, v);
	ck_typed_n = 0;
}

static bool ck_key(const input_event_t *ev)
{
	uint16_t row = ui_frame()->focus;
	if (!ck_editing || row == CK_SOURCE)
		return false;
	bool press = ev->gesture == GESTURE_PRESS || ev->gesture == GESTURE_LONG;
	if ((ev->action == ACT_NAV_UP || ev->action == ACT_NAV_DOWN) && press) {
		ck_step(row, ev->action == ACT_NAV_UP ? 1 : -1);
		ui_dirty();
		return true;
	}
	if (ev->action == ACT_ENC_STEP && ev->gesture != GESTURE_UP) {
		if (ev->value != 0)
			ck_step(row, ev->value > 0 ? 1 : -1);
		ui_dirty();
		return true;
	}
	if (ev->action >= ACT_DIGIT_0 && ev->action <= ACT_DIGIT_9 && press) {
		ck_typed = (uint16_t)(ck_typed_n == 0u ? 0u : ck_typed * 10u) + (uint16_t)(ev->action - ACT_DIGIT_0);
		ck_typed_n++;
		if (ck_typed_n == ck_width(row, ck_field)) {
			ck_typed_n = 0;
			if (ck_typed < ck_min(row, ck_field) || ck_typed > ck_max(row, ck_field)) {
				ui_toast(row == CK_DATE && ck_field == 0u ? TR(CLOCK_TOAST_YEAR, "Year 2020 to 2099") :
					 TR(CLOCK_TOAST_RANGE, "Out of range"));
			} else {
				ck_put(row, ck_field, ck_typed);
				if (ck_field < 2u)
					ck_field++;
			}
		}
		ui_dirty();
		return true;
	}
	if (ev->action == ACT_BACK && ev->gesture == GESTURE_PRESS) {
		if (ck_typed_n != 0u) {
			ck_typed_n--;
			ck_typed /= 10u;
		} else if (ck_field != 0u) {
			ck_field--;
		} else {
			ck_editing = false;	// leave without a change
		}
		ui_dirty();
		return true;
	}
	return false;	// OK: ck_select
}

static void ck_enter(void) { ck_editing = false; }

// From the UI loop: the rows count on while the screen is open, so it is drawn again each second
void ui_clock_tick(uint32_t now_ms)
{
	static uint32_t drawn_ms;
	if (ui_frame()->scr != SCR_CLOCK || ck_editing || now_ms - drawn_ms < 1000u)
		return;
	drawn_ms = now_ms;
	ui_dirty();
}

const ui_scr_def_t ui_clock_screen = {
	.name = "clock", .flags = SF_LIST | SF_NUMBERED,
	.prefix = TRN(SET_LABEL_DATE_TIME, "Date & time"),
	.hint = TRN(HINT_OK_CHANGE_BACK, "OK change  Back"),
	.count = ck_count, .item = ck_item, .select = ck_select, .key = ck_key, .enter = ck_enter,
};
#endif
