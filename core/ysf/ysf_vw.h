// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 lyra XE1POC
// Copyright (c) 2026 David Behnke
// YSF VW (Voice Wide, data type 3, docs/spec/ysf.md 3.5): a VCH is a whole 72-symbol payload block, the IMBE code words
// of one 20 ms voice frame (fec/imbe88.h) in VW's own interleave. No RTOS or board call.
#ifndef RFOUNDRY_YSF_VW_H
#define RFOUNDRY_YSF_VW_H

#include <stdint.h>

#include "../fec/imbe88.h"

#define YSF_VW_BYTES		FEC_IMBE88_BYTES	// a voice frame as the IMBE codec takes it
#define YSF_VW_VCH_BYTES	18			// 144 bits on air
#define YSF_VW_SUB_VCH		3			// the sub-header's VCH are blocks 3 and 4

// One VCH: field is its 144 bits as on air, first bit in bit 7 of byte 0. decode returns what the codes corrected,
// packed as fec_imbe88_decode
void ysf_vw_encode(const uint8_t imbe[YSF_VW_BYTES], uint8_t field[YSF_VW_VCH_BYTES]);
uint8_t ysf_vw_decode(const uint8_t field[YSF_VW_VCH_BYTES], uint8_t imbe[YSF_VW_BYTES]);
// The same in payload block n = 0..4 of a frame (ysf.h)
void ysf_vw_put(uint8_t *frame, uint8_t n, const uint8_t imbe[YSF_VW_BYTES]);
uint8_t ysf_vw_get(const uint8_t *hard, uint8_t n, uint8_t imbe[YSF_VW_BYTES]);

#endif
