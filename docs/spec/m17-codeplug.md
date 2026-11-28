<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (c) 2026 David Behnke -->

# M17 in the codeplug

The stored records of M17 channels, on the generic layer of [channel-modes.md](channel-modes.md). The firmware's
codec is `core/m17/m17_codeplug.c` (`CONFIG_M17`), the Codeplug Editor's `site/static/js/codeplug/mode_m17.js`. The
radio core runs a channel of kind 7 where the build has `CONFIG_M17` and the board's air interface carries M17
([m17.md](m17.md) 6.3); elsewhere it is parked ([channel-modes.md](channel-modes.md) 6). Why the id is 7:
[ADR 006](../crucible/adr/006-m17.md).

| Id | Kind | System type |
|---|---|---|
| 7 (0x07) | M17 | none |

## 1. Channel (kind 7)

The tag ([channel-modes.md](channel-modes.md) 2): no system, flag bit 0 transmit allowed. The parameter block:

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | Bits 0-3: the CAN, 0-15. Bit 4: hear every CAN (Crucible's own). Bits 5-7: reserved |
| 1 | 9 | Destination, ASCII, padded with spaces or NUL. Blank or `ALL`: BROADCAST |

| Rule | Value |
|---|---|
| No block, or a block of 1 byte | CAN as byte 0 (0 without it), BROADCAST |
| A block shorter than 10 | The destination is the bytes it has |
| Destination characters | `0-9`, `A-Z`, `-`, `/`, `.` and space ([m17.md](m17.md) 3). Lower case reads as upper case |
| Another character in the destination | The destination reads as BROADCAST |
| Bytes past offset 10 | Not read; kept by the editor |
| Reserved bits of byte 0 | Not read. The radio writes 0; the editor keeps what it read |

With the tag's flag bit 0 clear the channel is receive only. Transmit is not written yet ([m17.md](m17.md) 7), so
PTT is refused on every M17 channel.

The base record carries:

| Record field | Use |
|---|---|
| Name, RX and TX frequency, power, TX time-out, scan skip | As any channel |
| Contact, RX group list, per-channel DMR ID, color code, timeslot | Kept, not read: M17 names its caller and us by callsign |
| Mode, receive only | DMR, receive only, always (the safe stand-in) |

## 2. Radio-wide record (kind 7)

Key 0x0A00 + 7 = 0x0A07 ([channel-modes.md](channel-modes.md) 5.5):

| Offset | Size | Field |
|---|---|---|
| 0 | 9 | Source callsign, ASCII: `0-9`, `A-Z`, `-` and `/`, padded with NUL or spaces. Lower case reads as upper case. Empty, absent or with another character: unset |

## 3. The callsign sent

The first that is set:

| Order | Source |
|---|---|
| 1 | The M17 record (2) |
| 2 | The YSF record ([ysf-codeplug.md](ysf-codeplug.md) 1), when it holds at most 9 characters |
| 3 | The radio name's first word (up to a space, `-` or `/`): 3 to 9 letters and digits, at least one of each |

None set: the radio has no callsign. Home says so, and transmit (when written) is refused.

## 4. Writing from the radio

Not written: the radio reads M17 channels but saves none yet. The VFO's M17 settings (CAN, hear every CAN,
destination) are held in RAM; the settings record, the channel editor and saving the VFO as a channel come in ADR 006
step 3.

## 5. Sharing

A zone share carries an M17 channel's tag as it is ([zone-share.md](zone-share.md) 4.2): the block is 10 octets,
inside the share's limit of 64. The radio-wide record is never shared.
