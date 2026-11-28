// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 David Behnke
// IMBE 7200x4400 voice code words (P25 voice, 5.3), shared by P25 and YSF VW: 88 bits of speech in eight words, u0-u3
// Golay(23,12), u4-u6 Hamming(15,11), u7 raw, u1-u6 scrambled by masks from u0. Each mode puts the 144 bits on air
// with its own interleave.
#ifndef RFOUNDRY_FEC_IMBE88_H
#define RFOUNDRY_FEC_IMBE88_H

#include <stdint.h>

#define FEC_IMBE88_BYTES	11	// u0..u3 (12 bits each), u4..u6 (11 each), u7 (7), first bit first
#define FEC_IMBE88_BITS		144	// on air: c0..c3 23 bits, c4..c6 15, c7 7
extern const uint8_t fec_imbe88_len[8];	// bits in each code word c0..c7

// words: c0..c7, each in its low bits, its first bit on air the highest. Decode never refuses; it returns what the codes
// corrected, packed: the total (bits 0-3, saturating at 15), in c0 (bits 4-5) and in c4 (bit 6)
void fec_imbe88_encode(const uint8_t imbe[FEC_IMBE88_BYTES], uint32_t words[8]);
uint8_t fec_imbe88_decode(const uint32_t words[8], uint8_t imbe[FEC_IMBE88_BYTES]);

#endif
