// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 David Behnke
// Digital AGC (docs/crucible/adr/003-digital-agc.md): which level each digital mode uses. With a switch On, the
// radio's defaults (board_t.rx_agc_default_db, tx_level_default_db; P25 transmit -20 on every radio); with it Off,
// the user's row for the mode. The results are choices of the existing lists: rx_agc's targets (audio/rx_agc.h) and
// tx_level_choice (dsp/tx_level.h), 0 = Off. Pure functions: the callers read the settings and apply the choice
#ifndef RFOUNDRY_DIGITAL_AGC_H
#define RFOUNDRY_DIGITAL_AGC_H
#include <stdbool.h>
#include <stdint.h>
#include "config.h"
#include "audio/rx_agc.h"
#include "dsp/tx_level.h"

#define DAGC_DEFAULT_DB		(-30)	// a board that gives no default
#define DAGC_P25_TX_DB		(-20)	// P25 transmit with the switch On, every radio

// The transmit modes, in the order of the TX level rows
typedef enum { DAGC_TX_DMR, DAGC_TX_P25, DAGC_TX_NXDN, DAGC_TX_YSF, DAGC_TX_MODES } dagc_tx_mode_t;

// The RX choice whose target is db, or the core default's when the list has no such target (0 without RX AGC)
static inline uint8_t dagc_rx_choice_of_db(int db)
{
#if CONFIG_RX_AGC
	for (int pass = 0; pass < 2; pass++, db = DAGC_DEFAULT_DB)
		for (uint8_t c = 1; c < RX_AGC_TARGETS; c++)
			if (rx_agc_target(c) == db)
				return c;
#else
	(void)db;
#endif
	return 0;
}

// The TX choice whose target is db (TX_LEVEL_CHOICE_DBFS), or the core default's when the list has none
static inline uint8_t dagc_tx_choice_of_db(int db)
{
	for (int pass = 0; pass < 2; pass++, db = DAGC_DEFAULT_DB)
		for (uint8_t c = 1; c < TX_LEVEL_CHOICES; c++)
			if (TX_LEVEL_CHOICE_DBFS(c) == db)
				return c;
	return 0;
}

// The RX choice in force: manual (the switch Off) takes the user's stored choice
static inline uint8_t dagc_rx(bool manual, uint8_t stored, int8_t board_db)
{
	return manual ? stored : dagc_rx_choice_of_db(board_db != 0 ? board_db : DAGC_DEFAULT_DB);
}

// The TX choice in force for a mode
static inline uint8_t dagc_tx(dagc_tx_mode_t mode, bool manual, uint8_t stored, int8_t board_db)
{
	if (manual)
		return stored;
	return dagc_tx_choice_of_db(mode == DAGC_TX_P25 ? DAGC_P25_TX_DB : board_db != 0 ? board_db : DAGC_DEFAULT_DB);
}

// The stored P25 and NXDN TX bytes (settings.h: a choice + 1, 0 = unset) as a choice
static inline uint8_t dagc_tx_p25_stored(uint8_t raw)
{
	return raw == 0 || raw > TX_LEVEL_CHOICES ? dagc_tx_choice_of_db(DAGC_P25_TX_DB) : (uint8_t)(raw - 1u);
}
static inline uint8_t dagc_tx_nxdn_stored(uint8_t raw, uint8_t dmr_choice)
{
	if (raw == 0 || raw > TX_LEVEL_CHOICES)
		return dmr_choice < TX_LEVEL_CHOICES ? dmr_choice : 0;
	return (uint8_t)(raw - 1u);
}

#endif
