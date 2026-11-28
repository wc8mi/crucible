<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (c) 2026 David Behnke -->

# 007. Conventional NXDN: repeater access, digital monitor (NXDN and P25), dual watch

| | |
|---|---|
| Status | Proposed |
| Date | 2026-11-28 |
| Decides | Which of the DM32-nxdn-p25 fork's conventional NXDN changes Crucible takes, how they fit our stack, and in what order the work lands |

## Context

The owner's roadmap puts NXDN after YSF VW and V/D mode 1 ([ADR 005](005-ysf-vw-vd1.md)) and M17
([ADR 006](006-m17.md)). ADR 005 planned this record for NXDN trunking. The owner does not want trunking
(2026-11-28): Crucible keeps trunking off (CRUCIBLE.md, Features and defaults). This record covers conventional NXDN
only.

### What we have

RadioFoundry's NXDN plus [ADR 004](004-nxdn-radio-default.md): 4800 and 9600 bit/s, stored channels with RANs, group
squelch and talk target, the scrambler (`CONFIG_NXDN_SCRAMBLER`), dual watch across NXDN channels (`test_nxdn_dual`),
the stored radio default with its rate tile, Talk to in the VFO, NXDN in the CSV Mode column.

### What the fork has that we do not

DM32-nxdn-p25 (lyra XE1POC, GPL-3.0-or-later, already in [CREDITS.md](../../../CREDITS.md)), read at `4ebc641`
(2026-11-27). Its NXDN work is a patch stack on RadioFoundry, `integration/current/*.patch`.

| Fork patch | What it does | Here |
|---|---|---|
| `digital-repeater-interop` | A lead-in before the first frame; the unit ID falls back to the radio's own; voice starts after the transmitter goes | This record (1) |
| `base.patch`, nxdn.md 6.5 and p25.md 7.5 | Digital monitor on NXDN and P25: the hear all latch hears every RAN or NAC and every group | This record (2) |
| `nxdn-dual-monitor`, `issue66-nxdn-dual-monitor-tests` | Dual watch keeps each NXDN channel's monitor latch | This record (3) |
| `quick-digital-selectors` | Quick menu keys 2 and 8 set the RAN and talkgroup for now, on a stored channel too | Not wanted (owner, 2026-11-28). ADR 004's rule stands: Talk to is refused on a stored channel |
| `nxdn-scan-call-replay` | Call replay for NXDN | Not here: needs the fork's DMR call replay, which we lack (about 1,100 lines plus the DMR part) |
| `base.patch` AES, `voice-privacy-indicators` | AES-256 on NXDN channels, padlock icons | Not here: needs the fork's shared air encryption keyring |
| `nxdn-trunking`, `nxdn-trunked-dmon`, `nxdn-800-diagnostics` | Type-C trunking | Not wanted |

None of the fork's NXDN changes have on-air records in its repository. Its own notes name the DM-32UV only.

### Problems found in our tree

1. **The VFO sends unit ID 1.** A stored channel takes its unit ID from the channel, then the radio-wide NXDN record,
   then the DMR ID, then 1 (`unit_id()`, `core/nxdn/nxdn_codeplug.c`). The VFO and a channel without its own NXDN
   settings use the radio default instead (`set.unit_id`, `core/radio/radio_nxdn.c`). That value starts at 1, and
   `radio_nxdn_set_settings` turns 0 into 1. ADR 004 left the unit ID RAM-only, set only by the debug link. So every
   VFO transmission says unit 1, whatever the owner's DMR ID is.
2. **The preamble is short.** A transmission opens with the reference pattern (`5F` three times) and `5775FD`: 24
   symbols, 10 ms at 4800 bit/s, 5 ms at 9600 (nxdn.md 1). A repeater or hotspot that needs its receiver to settle
   may lose the header frame, and with it the call's VCALL until the first SACCH superframe. The fork adds 200 ms of
   the pattern for this reason (its patch name; the fork gives no measurement).
3. **No digital monitor on NXDN or P25.** The monitor key's short press toggles the hear all latch only on DMR
   (`radio_sm.c`, the `RREQ_MONITOR` handler). On NXDN, a call on another RAN or group stays silent; on P25, a call
   on another NAC or talk group. The owner must edit the channel or the radio default to hear it.
4. **Dual watch drops latches on each switch.** Any retune with the board in its DMR mode and an air mode selected
   calls `dmr_end()` (`radio_sm.c`, `tune()`), and `dmr_end()` clears the talk target and the hear all latch. With two
   NXDN channels in dual watch, the receiver switches every dwell and the latch goes each time. With (2) in place this
   breaks digital monitor in dual watch.

## Decision

### 1. Repeater access

**a. The unit ID.** The radio default's unit ID 0 means "from the codeplug": the radio-wide NXDN record, then the DMR
ID, then 1, the order a stored channel already uses after its own ID. 0 becomes its default. The debug link can still
set an ID for now. `nxdn_codeplug.c` gets one function, `nxdn_radio_unit_id()`, used by both paths, so the order lives
once.

**b. A lead-in.** `core/air/air_session` gets a generic lead-in: `air_session_tx_preamble(s, byte, ms)` queues a
repeated byte for the given time before the mode's first frame. `fill()` sends it ahead of the mode's own `tx_fill`.
NXDN asks for 200 ms of `5F`, its reference pattern, before its standard preamble. A constant in `radio_nxdn.c`;
step 3 tunes it on a repeater. P25 and YSF keep their transmissions as they are (owner, 2026-11-28).

**c. Voice start order.** The fork starts the microphone after `tx_go` and refuses PTT if the voice stream did not
start. Our order (voice before go) has no known fault. Step 1 adds a host test for the first voice frames on air; the
fork's order comes in only if that test, or the radio test in step 3, shows frames lost.

### 2. Digital monitor on NXDN and P25

The existing hear all latch works on NXDN and P25 channels and on the VFO in either mode. One mechanism, no new
request:

| Part | Change |
|---|---|
| Monitor key | The short-press toggle on release applies to NXDN and P25 as to DMR. FM keeps its open squelch while held |
| `RREQ_DMR_HEAR_ALL`, the quick tile, the debug link | Unchanged; they set the same latch |
| `radio_nxdn.c` | `radio_nxdn_monitor(bool)`. While on, the engine's receive RAN is any and the group squelch is off: every group, all call and unit call plays. What PTT sends stays the channel's |
| `radio_p25.c` | `radio_p25_monitor(bool)`. While on, the receive NAC is any (`P25_NAC_ANY`) and `rx_tgid` is 0: every talk group, all call and unit call plays. What PTT sends (NAC, talk group, unit ID) stays the channel's. Conventional only: Crucible builds P25 trunking off |
| `radio_sm.c` | Passes `hear_all_on(sm)` to both where it configures DMR, and restarts the running engine's settings when it changes |
| Ends | On a retune, as on DMR. Zone and VFO scans started with it on carry it, as on DMR (#595) |
| Encrypted calls | Stay silent without the key. NXDN scrambled calls with the key play; P25 DES or AES calls stay silent (Crucible has no P25 encryption) |
| Home, view | `view.dmr.hear_all_latch`, as today. The name says DMR; renaming it touches upstream's files, so it stays |

### 3. Dual watch keeps per-channel latches

| Part | Change |
|---|---|
| `tune()` | A retune between two channels of the same air mode, its engine running, does not call `dmr_end()`. The engine restarts with the new channel's settings as today |
| `dual_keep_load()` | A side with no kept state, reached from the other side, starts with the talk target and hear all latch cleared. Without this, keeping the latch in `tune()` would carry one side's latch to the other |

Both are the fork's `nxdn-dual-monitor` change. The `air_same` test names P25 and YSF too: they hit the same
`dmr_end()` call, and the fix belongs in the shared path (AGENTS.md rule 4, one stack). With (2), P25 channels in dual
watch keep their latches the same way. YSF has no latch to keep.

### 4. Size, switch and memory

Small: a few hundred bytes of flash, no RAM beyond two bytes of state. No new `CONFIG_` switch: it lives under
`CONFIG_NXDN` and `CONFIG_P25` (and `CONFIG_DUAL_WATCH` for 3). Step 1 measures each radio; a switch comes in only if it costs more
than 1 KB anywhere.

### 5. Code from the fork

The fork's patches are applied by hand, not as patch files: our tree has moved (ADR 004, ADR 005). Each commit that
brings fork code keeps its copyright lines where a whole function comes over, and adds what it took to the fork's
CREDITS.md row in the same commit.

## Plan

Each step is its own pull request. Each builds clean with `CONFIG_NXDN`, `CONFIG_P25`, `CONFIG_DUAL_WATCH` and `CONFIG_SCAN` off.

| Step | Lands | Specs and docs | User sees |
|---|---|---|---|
| 1 | Unit ID (1a), lead-in (1b), the voice start test (1c), debug link `nxdn.unit_id` source key; host tests in `nxdnEngineTest` and `radioCoreTest` | nxdn.md 1 and 6.2, ADR 004's settings table note | The VFO sends the owner's ID; a longer lead-in |
| 2 | Digital monitor (2) and the dual watch fix (3); `radioCoreTest` (latch on NXDN and P25, every RAN or NAC heard, PTT unchanged, retune ends it, two NXDN and two P25 channels in dual watch keep their own latches); `uiFlowTest` flows for the key on NXDN and P25 | nxdn.md 6.5 and p25.md (new sections), dmr-voice.md 1 link, user guide (digital monitor on NXDN and P25) | The monitor key hears every NXDN and P25 group |
| 3 | Radio tests, HIL `test_nxdn.py` additions (`nxdn.unit_id`, the latch keys), the lead-in tuned on a repeater | bring-up records, feature-matrix.md | Proven on air, or marked untested |

## Options considered

| Option | Why not |
|---|---|
| Take the fork's patch files as they are | They apply to the fork's base, not ours; they also carry P25, YSF and AES changes this record does not decide |
| The quick RAN/talkgroup selectors | Reverses ADR 004's rule for stored channels. The owner declined them |
| NXDN AES-256 and the padlocks | Needs the fork's air encryption keyring, a large shared subsystem, and flash ADR 005 and 006 already spend |
| Call replay for NXDN | Needs DMR call replay first, which we do not have |
| A unit ID setting in the NXDN menu | One more setting. The DMR ID is the owner's ID on every radio we know; the radio-wide NXDN record in the codeplug covers the exception |
| Fix dual watch only for NXDN | The same `dmr_end()` call hits P25 and YSF. One fix in the shared path |
| Lead-in for P25 and YSF | The owner declined. The generic call makes either a one-line change if a repeater ever needs it |

## Consequences

- **Upstream merges.** `air_session.{c,h}`, `radio_nxdn.c`, `radio_p25.c`, `nxdn_codeplug.{c,h}`, `radio_sm.c` change.
  `radio_sm.c` changes most often upstream; the hooks stay a few lines beside their DMR twins.
- **Air time.** Every NXDN transmission is 200 ms longer. On a busy repeater that is the cost of the header arriving.
- **Settings.** The radio default's unit ID moves from 1 to 0 ("from the codeplug"). It is RAM only, so no stored
  record changes.
- **ADR 004.** Its settings table says the unit ID stays RAM only; that holds. Its default changes from 1 to "from the
  codeplug"; step 1 notes it in nxdn.md, not in ADR 004.
- **ADR 005.** Its line "NXDN trunking get[s] its own record (007)" is out of date. ADR 005 is accepted and is not
  rewritten; this record replaces that plan.
- **Not covered.** Trunking, AES, call replay, quick selectors, P25 and YSF lead-ins.

## Owner's answers

| Question | Answer (2026-11-28) |
|---|---|
| Quick RAN/talkgroup selectors on a stored NXDN channel | No. ADR 004 stands |
| Digital monitor on P25 the same way | Yes, in this record (2) |
| A lead-in for P25 and YSF | No. NXDN's lead-in (1b) stays |

## Review log

| Round | Verdict | Changes |
|---|---|---|
| 2 | Owner's answers | No quick selectors; P25 digital monitor added to (2) and the dual watch note in (3); no P25 or YSF lead-in. Open questions replaced by the answers |
| 1 | Draft | First draft from a comparison with the fork at `4ebc641` |
