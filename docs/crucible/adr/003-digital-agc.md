<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 David Behnke -->

# 003. Digital AGC: two switches, defaults per radio

| | |
|---|---|
| Status | Proposed (2026-11-26). KF8S accepts. |
| Date | 2026-11-26 |
| Decides | How the digital receive and transmit audio levels are set on each radio |
| Supersedes | [002](002-tx-level-on-air.md) |

## Context

Each digital mode has its own audio level settings (Settings > Talk & listen):

- **RX AGC** for DMR, P25, NXDN and YSF: Off, or a target from -36 to -12 dBFS.
- **TX level** for DMR and YSF: Off, or a target from -30 to -12 dBFS before the encoder. NXDN uses the DMR value.
- **P25 TX**: a fixed -20 dBFS target. The Mic AGC toggle turns it on or off.

A good level differs by radio: the DM-32UV needs a louder TX target than the others, and the MD-9600 a louder RX
target. ADR 002 kept one on-air setting and added a trim per board, but the trims were never merged. Most users want
good levels without setting eight values. Tinkerers still want every value.

## Decision

1. **Two switches.** **Digital AGC RX** and **Digital AGC TX**, each On or Off, in Settings > Talk & listen above the
   per-mode rows.
2. **On: the radio's defaults.** Each mode uses the radio's default target:

   | Radio | RX: DMR, P25, NXDN, YSF | TX: DMR, NXDN, YSF | TX: P25 |
   |---|---|---|---|
   | DM-32UV | -30 | -26 | -20 |
   | DM-1701 | -30 | -30 | -20 |
   | MD-UV380 | -30 | -30 | -20 |
   | MD-9600 | -25 | -30 | -20 |

   Values are dBFS targets at the encoder input (TX) or the speaker path (RX), as the settings meant before ADR 002.
   P25 TX is -20 on every radio.
3. **Off: the user's values.** Each mode uses its own row: RX AGC for DMR, P25, NXDN, YSF; TX level for DMR, P25
   (new), NXDN (new, no longer shared with DMR), YSF. Each list keeps **Off** (no leveling for that mode).
4. **Rows stay visible.** With a switch On, its rows are dimmed and show the value in use. OK on a dimmed row says
   "Digital AGC sets the level". The user's manual values are kept and come back when the switch goes Off.
5. **Both switches start On**, on a new radio, after a factory reset and after an update.
6. **The defaults live with the board.** `board_t` holds `rx_agc_default_db` and `tx_level_default_db` (0 = the core
   default, -30). Shared code never tests a radio. P25's -20 is in shared code.
7. **Mic AGC no longer controls P25 leveling.** The P25 TX level does. Mic AGC stays for a vocoder with its own AGC.
   In P25, Mic gain dims as "AGC" when P25 leveling is on.
8. **ADR 002 is superseded.** The difference between radios is in the defaults, not in a trim. PR #2 is closed.

## Options considered

| Option | Why not |
|---|---|
| A trim per board, one on-air setting (ADR 002) | Two numbers to explain per mode. The defaults table gives the same result with one. |
| Hide the rows when a switch is On | The user cannot see the level in use. |
| One switch for RX and TX | RX and TX are tuned for different reasons; owners asked for each. |

## Consequences

- **Settings record.** Four bytes are appended (settings_t 424 to 428 bytes): a marker (0xA6), the two switches
  (bit set = manual), the P25 TX level and the NXDN TX level (each a choice + 1; 0 = unset: P25 reads -20, NXDN reads
  the DMR value). A record shorter than 428 bytes, or with another marker, reads as both switches On.
- **Upstream collision.** This block is Crucible's own. If RadioFoundry later appends its own field at byte 424, a
  Crucible radio reading such a record sees a wrong marker and uses the defaults. There is a 1 in 256 chance that a
  foreign byte equals 0xA6, so its other bytes are read as manual values. A user who installs RadioFoundry after
  Crucible should reset the settings.
- **Existing radios.** After the update, both switches are On. Saved DMR and YSF values stay as manual values. NXDN
  starts from the DMR value and P25 from -20.
- **Debug link.** View keys show both switches and the level in use per mode, so HIL tests can check them.

## Plan

| Step | Work | Needs a radio |
|---|---|---|
| 1 | Settings, policy, menu, P25 and NXDN targets, tests, docs | No |
| 2 | A test release with all four radios | No |
| 3 | An on-air check of each radio's defaults | Yes |

## Review log

| Round | Reviewer | Result |
|---|---|---|
| 1 | Independent (code) | Revise: settings size assert, upstream collision, runtime dim precedent, P25 tied to Mic AGC, NXDN profile, setting id plumbing, default units, debug link and HIL, docs, build matrix |
| 2 | Independent (code) | Revise: marker set in RAM at defaults and load; remove the Mic AGC to P25 coupling |
| 3 | Independent (code) | Revise: host tests that assume today's behavior; append setting ids at the end |
| 4 | Independent (code) | Accept. Note: the stub settings must also start with both switches On |
