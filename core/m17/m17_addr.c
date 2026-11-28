// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 David Behnke
// M17 station addresses (m17.h; docs/spec/m17.md 3)
#include "m17.h"

static const char ALPHABET[41] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-/.";

static int digit(char c)
{
	if (c >= 'a' && c <= 'z')
		c = (char)(c - 'a' + 'A');
	if (c >= 'A' && c <= 'Z')
		return c - 'A' + 1;
	if (c >= '0' && c <= '9')
		return c - '0' + 27;
	switch (c) {
	case ' ': return 0;
	case '-': return 37;
	case '/': return 38;
	case '.': return 39;
	default: return -1;
	}
}

// The text without the spaces at its end (and without a NUL before len)
static size_t trimmed(const char *s, size_t len)
{
	size_t n = 0;

	while (n < len && s[n] != '\0')
		n++;
	while (n > 0 && s[n - 1] == ' ')
		n--;
	return n;
}

bool m17_addr_encode(const char *s, size_t len, uint64_t *addr)
{
	size_t n = trimmed(s, len), first = 0;
	uint64_t a = 0;

	if (n == 4 && s[0] == '@' && s[1] == 'A' && s[2] == 'L' && s[3] == 'L') {
		*addr = M17_ADDR_BROADCAST;
		return true;
	}
	if (n > 0 && s[0] == '#')
		first = 1;
	if (n <= first || n - first > (first ? M17_CALL_MAX - 1u : M17_CALL_MAX))
		return false;
	for (size_t i = n; i-- > first;) {
		int d = digit(s[i]);
		if (d < 0)
			return false;
		a = a * 40u + (uint64_t)d;
	}
	if (a == 0)
		return false;	// spaces only
	*addr = first ? a + M17_ADDR_HASH : a;
	return true;
}

size_t m17_addr_decode(uint64_t addr, char *out)
{
	size_t n = 0;

	if (addr == M17_ADDR_BROADCAST) {
		out[0] = '@', out[1] = 'A', out[2] = 'L', out[3] = 'L', out[4] = '\0';
		return 4;
	}
	if (addr >= M17_ADDR_HASH_END) {
		addr = 0;	// reserved
	} else if (addr >= M17_ADDR_HASH) {
		out[n++] = '#';
		addr -= M17_ADDR_HASH;
	}
	while (addr != 0) {
		out[n++] = ALPHABET[addr % 40u];
		addr /= 40u;
	}
	out[n] = '\0';
	return n;
}

bool m17_addr_dest(const char *s, size_t len, uint64_t *addr)
{
	size_t n = trimmed(s, len), lead = 0;

	while (lead < n && s[lead] == ' ')
		lead++;
	if (lead == n || (n - lead == 3 && (s[lead] | 0x20) == 'a' && (s[lead + 1] | 0x20) == 'l' &&
			   (s[lead + 2] | 0x20) == 'l')) {
		*addr = M17_ADDR_BROADCAST;
		return true;
	}
	return m17_addr_encode(s, n, addr);
}

void m17_addr_put(uint8_t p[M17_ADDR_BYTES], uint64_t addr)
{
	for (int i = M17_ADDR_BYTES - 1; i >= 0; i--) {
		p[i] = (uint8_t)addr;
		addr >>= 8;
	}
}

uint64_t m17_addr_get(const uint8_t p[M17_ADDR_BYTES])
{
	uint64_t a = 0;

	for (int i = 0; i < M17_ADDR_BYTES; i++)
		a = a << 8 | p[i];
	return a;
}
