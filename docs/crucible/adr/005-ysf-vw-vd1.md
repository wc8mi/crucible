<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (c) 2026 David Behnke -->

# 005. YSF VW and V/D mode 1: extend our engine, take the fork's self-contained parts

| | |
|---|---|
| Status | Accepted (KF8S, 2026-11-27) |
| Date | 2026-11-27 |
| Decides | How Crucible receives and sends YSF VW (Voice Wide) and V/D mode 1 calls, which code it takes from the DM32-nxdn-p25 fork, and in what order the work lands |

## Context

Crucible's YSF ([ysf.md](../../spec/ysf.md)) is RadioFoundry's engine (MIT, `core/ysf/`). It carries DN (V/D mode 2,
data type 2) only:

- **Receive.** The FICH data type (DT) is parsed (`ysf_frame.c`, `fich_unpack`). A DT 0 (V/D mode 1) or DT 3 (VW)
  call starts, but its VCH only count `voice_held` (`ysf_call.c`, `voice()`), the engine's `wanted()` takes DT 2 only
  (`ysf_engine.c`), and Home shows "Not DN voice: not played". Its DCH is not decoded, so the caller comes only from
  the header.
- **Transmit.** `fich_for` always sends DT 2 (or 1 for room data), FT 6, DEV 0.
- **Why it stopped there.** [ysf.md](../../spec/ysf.md) 8: VW is full-rate IMBE, which the radios' AMBE+2 vocoder
  does not run. The software IMBE codec P25 uses (`core/mbe`) was named as "a possible way later".

VW is common on Yaesu repeaters and radios, so a Crucible radio stays silent on many Fusion calls it can already see.
V/D mode 1 is rarer but costs little: its VCH is the DMR AMBE+2 frame as it is.

The DM32-nxdn-p25 fork (GPL-3.0-or-later, lyra XE1POC) has both, in a **different YSF engine** of its own, plus the
glue to run VW through RadioFoundry's P25 voice path. Its own records say none of VW or V/D mode 1 has been checked
on air: VW is checked only by its own loopback, and its Pi-Star check is still open (fork issue #64). Since
Crucible relicensed to GPL-3.0-or-later ([AGENTS.md](../../../AGENTS.md) rule 1), its code may come in with credit.

### Memory measured

Sizes from the review (clang, thumbv7em, Crucible's headers; to be confirmed from the linker map in step 1). KB here is 1000 bytes:

| Block | Bytes |
|---|---|
| `p25_engine_t` | 872 |
| `ysf_engine_t`, `CONFIG_AIR_RX_ADAPT` off / on (on by default with `SUBTONE` and `YSF`) | 2632 / 4208 |
| `p25_voice_mem_t` | 4700 |
| `p25_mem_t`, without / with `CONFIG_P25_TRUNK` (on by default) | 5576 / 6600 |
| HR_C7000 vocoder lend (`HAL_VOCODER_LEND_BYTES`) | 7168 |

The DM-1701's default build (linker output, this branch): main RAM 116.6 KB of `.data` and `.bss` plus a 6 KB stack
in 128 KB, about 8 KB free. CCM: 34.7 KB placed, the rest (30.5 KB) is the FreeRTOS heap.

On the STM32F4 radios the YSF engine now lives in P25's idle block. The engine and VW's voice memory together
(7.3 to 8.9 KB) do not fit in that block (6.6 KB), so VW needs a new block there (4).

## Decision

### 1. Extend Crucible's engine. Do not replace it.

We keep `core/ysf/` and add VW and V/D mode 1 to it. From the fork we take only self-contained pieces, each with its
notices kept and credited in [CREDITS.md](../../../CREDITS.md) (9).

### 2. Receive follows the received data type

Whatever the channel transmits, a received DN, V/D mode 1 or VW call plays. The provider task only asks for a voice
path (AMBE or IMBE) from the FICH's DT. The radio task switches paths, as the fork's cross-DT receive does: end the
current sink, start the other, drop VCH until the two agree. After the call it restores the channel's transmit path.

PTT is refused while the active path is not the channel's transmit path ("YSF: busy"). It is accepted once the transmit
path is restored, not merely at the call's end. Room data (DT 1) keeps today's handling.

### 3. V/D mode 1 (DT 0)

| Part | What |
|---|---|
| Frame | Five blocks of DCH1 (9 bytes) and VCH (9 bytes). The VCH is the DMR 72-bit AMBE+2 frame, not whitened |
| Voice | Through `ambeFecDecode` and the DMR voice path, as DN. The vocoder sees no difference |
| DCH1 receive | Decoded: FN 0 destination and source, FN 1 downlink and uplink, FN 2 remarks. Data units (FN 3-7) are dropped |
| Transmit cycle | Voice only: 3 CC frames, FN 0-2, FT 2 (Yaesu Tables 4-3 and 4-4, as the fork's final rule). No data frames are sent |
| Switch | Inside `CONFIG_YSF`: small, and it needs no new codec |

### 4. VW (DT 3)

| Part | What |
|---|---|
| Frame | 5 VCH of 18 bytes, no DCH. 144 bits: 4 Golay(23,12) words, 3 Hamming(15,11) words, 7 raw bits, with the PN masks from u0 as P25's IMBE codeword. The 144-entry bit interleave is VW's own table, not P25's `VOICE_MAP` |
| FEC | `core/fec/imbe88.{c,h}` from the fork: P25's voice codeword FEC moved out of `p25_unit.c`, kept MIT (The RadioFoundry Authors). P25 and YSF share one copy |
| Transmit cycle | The first CC frame is the sub-header: FN 0, FT 1, DCH1 CSD3 (remarks 1-4) and 2 voice blocks. Each later frame is FN 0, FT 0 (fork, after Yaesu Tables 4-3 and 4-4). CSD3's content is the fork's inference, not sourced. FT on transmit and CSD3 are unverified until a Yaesu capture confirms them; receive accepts any FT and any CSD3 |
| Voice frames | `AIR_VOICE_FRAME_MAX` grows from 9 to 11 (`P25_IMBE_BYTES`), so VW transmit goes through `air_voice` as DN does. It sizes one stack buffer (`air_voice.c`) and two asserts: 2 bytes of stack, no NXDN memory. Receive gets a VW hook of its own (`rx_vw`: 11 bytes and the FEC errors) beside the AMBE hook |
| Codec | P25's software IMBE path (`p25Voice`, `core/mbe`). One codec for P25 and YSF |
| Shared P25 change | `p25VoiceBegin` gets a frames-per-burst argument and a receive AGC mode. P25 primes on one LDU (9 frames, 180 ms) and passes `RX_AGC_P25`, unchanged; YSF primes on one frame's 5 VCH (100 ms; 2 in the sub-header) and passes `RX_AGC_YSF`. The fork made the burst change |
| Levels | Receive uses YSF's setting: `rx_agc_ysf` through the AGC mode above, set when the IMBE path starts. Transmit uses P25's level (`tx_level_p25`, the target `p25VoiceSetTxTarget` already holds): VW and P25 share the IMBE encoder, so they start from one value. A VW-only adjustment comes only if listening on a Yaesu radio shows VW needs one (revision 1) |
| Encoder level | The fork found its IMBE encoder input about 14 dB low and adds +12 dB, saturating. We do not take that blind: in step 4 we measure P25 and VW transmit level against [ADR 003](003-digital-agc.md)'s on-air target and fix the shared encoder once if it is low |
| Switch | `CONFIG_YSF_VW`, added in step 2. A normal switch, in `FEATURES` and Crucible's CI matrix: `CONFIG_YSF_VW ?= $(if $(filter y,$(CONFIG_YSF)),$(CONFIG_P25),n)`, and an `$(error)` when it is on without `CONFIG_YSF` and `CONFIG_P25`. A radio short of flash or heap sets it `n` in its `config.mk`. Off: VW calls show as today, "Not DN voice" |

**Memory.** VW needs the YSF engine and `p25_voice_mem_t` at once.

| Platform | YSF engine | VW voice memory | RAM added |
|---|---|---|---|
| HR_C7000 | Its own static block, as today | The vocoder's lend (7168 B), as P25 borrows it. While VW plays, the AMBE+2 state is not kept; a switch back rebuilds it, it does not reload the codec | None |
| STM32F4 | With VW on, its own static block in CCM (`RFOUNDRY_CPU_RAM`, below), shared with NXDN's engine as `radio_air_mem.c` already does where P25 is off | P25's idle block (5576 or 6600 B, holds the 4700 B voice memory) | About 4.2 KB of CCM, taken from the FreeRTOS heap |

The CCM block applies only where `CONFIG_YSF_VW` is on and the vocoder does not lend (`!defined(HAL_VOCODER_LEND_BYTES)`): there `radio_air_mem.c` tags its engine union `RFOUNDRY_CPU_RAM`. With VW off it keeps using P25's idle block, as today; on the lending radios nothing moves. The test keys on the lend define, so `core/` names no platform.

Step 1 confirms both platforms from the linker map, and the FreeRTOS heap high-water mark on each STM32F4 radio with
VW on. If a radio's heap margin is too small, `CONFIG_YSF_VW` is off on that radio and the feature matrix says so.

### 5. Deviation

| Profile | Outer deviation | FICH DEV |
|---|---|---|
| Wide (default) | ±2700 Hz, as today | 0 |
| Narrow | ±1350 Hz | 1 |

`ysf_air_mode` becomes two profiles, and `ysf_engine_fit` and the engine's begin take the choice. Receive uses the
channel's profile; a change starts a new session, as a mode change does.

| Provider | Fit at 1350 Hz |
|---|---|
| HR_C7000 (`hrc7000_air.c`) | Inside its 100-6000 Hz transmit window. Receive through the soft modem, as today |
| HR_C6000 on the STM32F4 radios (`hrc6000_air.c`) | Transmit inside its 1050-2400 Hz range (target about 1427 Hz). Its correlator never syncs at 1350 Hz, but YSF already receives through the soft modem (no recurring sync, so the chip's receive fit fails). Narrow receive therefore depends on the soft modem at half deviation |

**Wide stays the default for every data type.** The fork's rule of Narrow except VW cites Yaesu 4.2.1. The handheld
Crucible recorded on air sends DEV 0 on DN ([ysf.md](../../spec/ysf.md) 4.2), and the recording outranks the reading.
Narrow is a per-channel choice for systems that want it.

### 6. Codeplug and site

The YSF channel block grows by one byte, byte 2.

| Byte 2 bits | Meaning |
|---|---|
| 0-1 | Transmit data type: 2 DN (also when the byte is absent), 3 VW, 0 V/D mode 1. 1 (data) reads as DN |
| 2 | Narrow (1) or Wide (0, also when absent) |
| 3-7 | Reserved, kept, written 0 |

- **New firmware, old channel.** No byte 2: DN Wide, as today.
- **Old firmware, new channel.** Its decoder reads bytes 0-1 only. A rewrite that passes the name check keeps the
  rest of the block (`codeplug_tag.c`). A rename fails that check, and `ysf_chan_save` then writes a fresh 2-byte
  block: the channel falls back to DN Wide. Recorded in [ysf-codeplug.md](../../spec/ysf-codeplug.md).
- **Not the fork's layout.** Its 13-byte block moves the DG-IDs and would break every saved Crucible codeplug.

The web Codeplug Editor gets "Voice mode" (DN, VW, V/D mode 1) and "Deviation" (Wide, Narrow); its `grow` keeps
unknown bytes as today. CSV import reads the fork's column names ("YSF DT", "Data Type", "Deviation") as aliases, so
a codeplug moved from the fork keeps its choices.

### 7. On the radio

| Where | What |
|---|---|
| Channel editor, YSF | Two rows after DG-ID: Voice mode (DN, VW, V/D 1) and Deviation (Wide, Narrow). VW is offered only when `CONFIG_YSF_VW` is on |
| VFO in YSF | The same two values in Settings > Talk & listen > YSF, stored with the radio's other YSF settings, DN Wide by default. No new quick tile: tile 3 (Rooms) and tile 8 (DG-ID) stay |
| Home | The call line shows the received mode (DN, VW, V/D 1) beside the caller. "Not DN voice" stays only for what is still not played (VW with the switch off, data) |
| Debug link | `ysf.dt` (received), `ysf.tx_dt`, `ysf.dev`, `ysf.path` (none, ambe, imbe), `ysf.path_switches`, `ysf.vw_frames`, `ysf.vw_fec_bits`, `ysf.vd1_frames` |

Every new string gets a key and is translated into each catalog before the merge.

### 8. Tests and checks

| Level | What |
|---|---|
| Host, codec | V/D mode 1 and VW frames against vectors made once on the host with DSD-FME or MMDVMHost (neither linked nor shipped), committed as data. These tools are where the VW interleave table comes from, so they check our code, not the table. A demodulated bit capture of a real Yaesu VW transmission is the independent check; it is bits, not an audio recording (rule 2), and is committed when an owner records one |
| Host, P25 | `p25VoiceTest`: priming on a 5-frame burst, and 9-frame priming unchanged; the AGC mode passed through. P25 tests still pass after the `imbe88` move |
| Host, engine | DT 0, 2 and 3 calls through the framer; a call changing DT mid-session; late entry on V/D mode 1; VW sub-header and CSD3; both transmit cycles |
| Host, air | The soft modem receiving at 1350 Hz; a Wide channel receiving a Narrow transmission and the reverse; fit at 1350 Hz on both providers |
| Host, radio core | Path switching (`radioCoreTest`): VW received on a DN channel, PTT refused until the transmit path is restored; levels restored per mode; the memory layout on both platforms |
| Host, UI | A `uiFlowTest` flow through the two new channel editor rows and the Settings entry; the gallery regenerated |
| HIL | `tests/hil/test_ysf.py`: VW and V/D mode 1 transmissions decoded off air by `tools/bench/ysftool.py` (opt-in, `--allow-rf`); a `ysf_vw` entry in `tests/hil/features.toml` |
| On air | Owner checks, recorded in the bring-up records: a Yaesu radio in VW and in DN to and from Crucible, and a Pi-Star/MMDVM node in each mode. Until they pass, release notes say "untested on air" |

### 9. Taking code from the fork

| Piece | How |
|---|---|
| `core/fec/imbe88.{c,h}` | Taken as is: code moved out of `p25_unit.c`, header MIT (The RadioFoundry Authors) |
| VW VCH layout and interleave table | Taken from the fork's `ysf_voice.c` into a new file, `core/ysf/ysf_vw.c`, headed `GPL-3.0-or-later` with lyra XE1POC's and David Behnke's copyright lines |
| Cross-DT path switching | Rewritten against our `radio_ysf.c`, following the fork's design. If any of its code is carried, the file becomes `MIT AND GPL-3.0-or-later` with lyra XE1POC's line |
| `p25VoiceBegin` frames per burst | Rewritten (a small change to a RadioFoundry file) |
| CSV aliases, DCH1 cycle | Rewritten (the fork's versions are tied to its engine) |
| Engine, codeplug, UI | Not taken |

[CREDITS.md](../../../CREDITS.md) gets a row with the first commit that brings in fork code: DM32-nxdn-p25 (lyra
XE1POC, GPL-3.0-or-later, what was taken). The VW interleave table is credited there to its origin as the fork gives
it, MMDVMHost and DSD-FME (GPL-2.0-or-later, compatible).

## Plan

Each step is its own pull request. It builds clean with every switch it touches off, and updates the specs for what it
lands.

| Step | Lands | Specs and docs | User sees |
|---|---|---|---|
| 1 | V/D mode 1 and VW frame codecs; `imbe88` shared with P25; the vectors; memory confirmed from the linker map and heap high-water marks | ysf.md 3.4 | Nothing yet |
| 2 | Receive of V/D mode 1 and VW; `AIR_VOICE_FRAME_MAX` 11; `p25VoiceBegin` frames per burst; path switching; `CONFIG_YSF_VW`; Home and debug link | ysf.md 4 and 8, debuglink.md, build-config.md, feature-matrix.md | VW and V/D mode 1 calls play |
| 3 | Codeplug byte 2, site, CSV, channel editor and Settings rows; Narrow profile and fit | ysf-codeplug.md 2, ysf.md 1 and 5, user guide | Choice of voice mode and deviation |
| 4 | Transmit in V/D mode 1 and VW; transmit level measured and fixed | ysf.md 4, user guide | Can call in VW and V/D mode 1 |
| 5 | HIL tests, on-air records, release notes | bring-up records, feature-matrix.md | Proven on air, or marked untested |

## Options considered

| Option | Why not |
|---|---|
| Replace Crucible's engine with the fork's | Loses rooms, the radio ID, the codeplug format and our tests. Every RadioFoundry merge into `core/ysf` would conflict. The fork's engine is no better proven on air |
| Take the fork's whole VW and V/D mode 1 stack as patches | Its patches sit on RadioFoundry `e89121e9` and its own engine. They do not apply to ours; the rework is the same as writing them against ours |
| Separate VW transmit hooks, `AIR_VOICE_FRAME_MAX` kept at 9 | Every transmit frame still passes through `air_voice`'s 9-byte buffer. Widening it costs 2 bytes of stack |
| VW engine and voice memory both in P25's block on STM32F4 | 7.3 to 8.9 KB needed, 6.6 KB there |
| VW off on the STM32F4 radios | Three of Crucible's five radios. 4.2 KB of CCM heap is the smaller cost |
| Narrow by default (the fork's rule) | Our on-air recording of a handheld says DEV 0 (Wide) |
| Wait for RadioFoundry to add VW | Crucible no longer sends work upstream; RadioFoundry's spec names VW only as "possible later" |

## Consequences

- **Flash.** V/D mode 1 is small and always in with YSF. VW adds the VW codec and glue; the IMBE codec is already in
  with P25. Measured per radio in step 1; a radio short of flash gets `CONFIG_YSF_VW` off.
- **RAM.** About 4.2 KB of CCM heap on each STM32F4 radio with VW on; none on the HR_C7000 radios. The link fails below `_Min_Task_Heap` (24 KiB); the DM-1701 keeps about 1.7 KB above it. The MD-9600 and MD-UV380 are measured in step 1. Tasks are created at run time, so the heap high-water mark is the real check.
- **P25.** Shares `imbe88`, the voice path and the priming change with YSF. A fix to either now reaches both; P25 tests
  guard both.
- **Upstream merges.** `p25_unit.c`, `p25Voice.c`, `air_voice.h`, `radio_air_mem.c`, `radio_ysf.c`, `ysf_call.c` and
  `ysf_engine.c` change. Each RadioFoundry merge touching them needs care. RadioFoundry adding its own VW later would
  conflict; we would then pick one.
- **Switching cost.** On the HR_C7000 radios a switch between VW and AMBE voice rebuilds the AMBE state from the lent
  block. The first frames after a switch may be lost. Measured in step 2; if it costs more than one frame (100 ms),
  receive keeps the current path until the call ends.
- **Narrow receive on the STM32F4 radios** rests on the soft modem at half deviation. If the step 3 tests show it
  failing, Narrow is transmit only there, and the feature matrix says so.
- **Codeplug.** One byte per YSF channel, read as DN Wide when absent. Old firmware keeps it except on a rename.
- **Not covered.** Data units in V/D mode 1 (positions, pictures), Group Monitor, news and VW data stay unsupported.
  M17 and NXDN trunking get their own records (006, 007).

## Review log

| Round | Verdict | Changes |
|---|---|---|
| 4 | Revision 1 (KF8S, 2026-11-27) | VW transmit uses P25's TX level, not `tx_level_ysf`: one encoder, one starting value, adjusted after listening on a Yaesu radio. The YSF engine's block placed in CCM as planned once no DMA was found to touch it |
| 3 | Accept | Wording: the STM32F4 memory row names the VW-on condition |
| 2 | Revise | `CONFIG_YSF_VW` a normal switch, off per radio; CCM block only with VW on and no lend; receive AGC mode in `p25VoiceBegin`; heap floor and margin; units |
| 1 | Revise | STM32F4 memory re-planned from measured sizes (engine to CCM); `AIR_VOICE_FRAME_MAX` widened instead of separate transmit hooks; one transmit cycle per data type, FT marked unverified; `p25VoiceBegin` priming change and YSF level settings named; Narrow profile and per-provider fit; spec updates per step; codeplug fallback on rename; interleave credit; `CONFIG_YSF_VW` derived; PTT tied to the restored path; title |
