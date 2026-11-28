// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 lyra XE1POC
// Copyright (c) 2026 David Behnke
// YSF VW voice channel (ysf_vw.h). The interleave table is from lyra XE1POC's GPL fork's ysf_voice.c, which gives it as
// MMDVMHost's AMBEFEC::regenerateIMBE and DSD-FME's fr_interleave permutation (CREDITS.md).
#include "ysf_vw.h"

#include <string.h>

#include "../air/air_syms.h"
#include "ysf.h"

// The air bit of each bit of the eight code words, taken in order: c0's first bit first
static const uint8_t vw_interleave[FEC_IMBE88_BITS] = {
	  0,   7,  12,  19,  24,  31,  36,  43,  48,  55,  60,  67,
	 72,  79,  84,  91,  96, 103, 108, 115, 120, 127, 132, 139,
	  1,   6,  13,  18,  25,  30,  37,  42,  49,  54,  61,  66,
	 73,  78,  85,  90,  97, 102, 109, 114, 121, 126, 133, 138,
	  2,   9,  14,  21,  26,  33,  38,  45,  50,  57,  62,  69,
	 74,  81,  86,  93,  98, 105, 110, 117, 122, 129, 134, 141,
	  3,   8,  15,  20,  27,  32,  39,  44,  51,  56,  63,  68,
	 75,  80,  87,  92,  99, 104, 111, 116, 123, 128, 135, 140,
	  4,  11,  16,  23,  28,  35,  40,  47,  52,  59,  64,  71,
	 76,  83,  88,  95, 100, 107, 112, 119, 124, 131, 136, 143,
	  5,  10,  17,  22,  29,  34,  41,  46,  53,  58,  65,  70,
	 77,  82,  89,  94, 101, 106, 113, 118, 125, 130, 137, 142,
};

// at: the field's first bit in buf
static uint8_t vw_get(const uint8_t *buf, uint16_t at, uint8_t imbe[YSF_VW_BYTES])
{
	uint32_t w[8] = { 0 };
	uint16_t k = 0;

	for (uint8_t i = 0; i < 8; i++)
		for (uint8_t b = 0; b < fec_imbe88_len[i]; b++)
			w[i] = (w[i] << 1) | air_bit_get(buf, (uint16_t)(at + vw_interleave[k++]));
	return fec_imbe88_decode(w, imbe);
}

static void vw_put(uint8_t *buf, uint16_t at, const uint8_t imbe[YSF_VW_BYTES])
{
	uint32_t w[8];
	uint16_t k = 0;

	fec_imbe88_encode(imbe, w);
	for (uint8_t i = 0; i < 8; i++)
		for (uint8_t b = fec_imbe88_len[i]; b > 0; b--)
			air_bit_put(buf, (uint16_t)(at + vw_interleave[k++]), (uint8_t)((w[i] >> (b - 1)) & 1));
}

void ysf_vw_encode(const uint8_t imbe[YSF_VW_BYTES], uint8_t field[YSF_VW_VCH_BYTES])
{
	vw_put(field, 0, imbe);
}

uint8_t ysf_vw_decode(const uint8_t field[YSF_VW_VCH_BYTES], uint8_t imbe[YSF_VW_BYTES])
{
	return vw_get(field, 0, imbe);
}

void ysf_vw_put(uint8_t *frame, uint8_t n, const uint8_t imbe[YSF_VW_BYTES])
{
	vw_put(frame, (uint16_t)(2 * YSF_BLOCK_SYM(n)), imbe);
}

uint8_t ysf_vw_get(const uint8_t *hard, uint8_t n, uint8_t imbe[YSF_VW_BYTES])
{
	return vw_get(hard, (uint16_t)(2 * YSF_BLOCK_SYM(n)), imbe);
}
