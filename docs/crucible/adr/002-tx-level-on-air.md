<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 David Behnke -->

# 002. TX level: one on-air target, a trim per radio

| | |
|---|---|
| Status | Accepted (KF8S, 2026-11-26) |
| Date | 2026-11-26 |
| Decides | What the DMR TX level and YSF TX level settings mean, and where the difference between radios is kept |

## Context

The DMR TX level and YSF TX level settings (Settings > Talk & listen) set the RMS level the leveler holds the
microphone at **before the encoder**: Off, or -30 to -12 dBFS in 2 dB steps (`core/dsp/tx_level.h`,
`tx_level_choice_rms`). The level a listener hears is what the encoder makes of that input, decoded at the other end.

On the air, with a Yaesu receiver as the meter, body mic and one voice, the maintainer
found these settings give about -34 dBFS, the level Yaesu receivers handle well:

| Radio | Encoder target for -34 on air | Difference to the setting |
|---|---|---|
| DM-32UV | -26 dBFS | +8 dB |
| DM-1701 | -30 dBFS | +4 dB |
| MD-UV380 | not measured | |
| MD-9600 | not measured | |

So the same setting gives a different on-air level on each radio: the DM-32UV is about 4 dB quieter than the DM-1701
at the same setting.

What the code says about the cause:

- Both radios pass the leveled PCM to the codec unscaled (`platform/hrc7000/vocoder.c`,
  `platform/stm32f4/hrc/vocoder.c`). The leveler output is the same.
- Each radio runs **its own manufacturer's AMBE+2 build** in place. The builds are not the same code. Their input
  scaling and noise reduction (`VOCODER_OPT_NOISE_REDUCTION`, on by default) can differ.
- The leveler measures RMS of everything the microphone gives, noise included. A noisier microphone path reaches the
  target with less speech, and the codec's noise reduction then takes the noise out. That makes a radio quieter on
  the air.

Either cause is a property of the radio (its codec build and its microphone path), not of the operator. The
operator wants one thing: a level that suits the receiver.

Two more facts:

- The DM-1701's -30 is the **lowest choice** in the menu. A radio that needs less cannot get it today.
- The decoder also runs the receiving radio's codec build. A meter reading depends on which radio receives.

## Decision

1. **The setting is the on-air level.** DMR TX level and YSF TX level name the level a reference receiver hears,
   not the encoder input. Labels stay. The value reads "-34 dBFS on air" in the help text.
2. **A trim per radio, per profile.** `board_t` gets `int8_t tx_level_trim_db[AMBE_TX_PROFILES]` (DMR, YSF), in whole
   dB, 0 when not given. The encoder target is the on-air setting plus the trim. `core/` reads the field and never
   names a radio (AGENTS.md rule 4).
3. **Starting trims** from the measurements above (body mic, one voice, Yaesu receiver): DM-32UV DMR +8, DM-1701
   DMR +4. They are provisional until the method below adds a second voice and the hand mic. YSF, the MD-UV380 and
   the MD-9600 stay 0 until measured. A radio's bring-up record says "untested" for a trim not yet measured on the air.
4. **The range moves.** On-air choices: Off, -40 to -22 dBFS in 2 dB steps (10 choices, as now). The encoder target
   (setting plus trim) is clamped to -42 to -10 dBFS, so the leveler table covers 1 dB steps across that span.
5. **The reference receiver is a Yaesu radio.** Trims are measured against one Yaesu receiver, named in
   `docs/spec/dmr-voice.md`, the same one for every trim, so they agree with each other and match the listeners that
   matter.
6. **Default stays Off** in this change. A follow-up sets -34 dBFS as the default once all four radios have a
   measured DMR trim.
7. **Saved settings.** The stored value stays a choice index. Its meaning changes, so a saved level shifts by the
   radio's trim after the update. Crucible is in pre-release; the release notes say so and give the new value to
   pick. No migration code.

## Options considered

| Option | For | Against | Verdict |
|---|---|---|---|
| A. Leave it. Publish the right setting for each radio. | No code. | Every operator must find and remember a number for their radio. A codeplug moved between radios sounds different. The DM-1701 is already at the floor. | Rejected |
| **B. On-air setting plus a board trim** | One number for everyone. The radio's difference sits with the radio, where `board_t` keeps other calibration. Small code. | Needs a measurement for each radio and each profile. Saved levels shift once. | **Chosen** |
| C. Trim by scaling PCM in each vocoder driver | No new `board_t` field. | Hides calibration inside a driver. The leveler's ceiling and limiter would see the wrong level. Two places to change, not one. | Rejected |
| D. Use the codec's own AGC (`VOCODER_OPT_AGC`) for leveling | The codec knows its own input. | Opaque, and different per codec build: the cause of the problem, not a fix. Two AGCs in a row fight (`ambeVoice.h`). | Rejected |
| E. Measure the level on the air automatically | No manual trims. | A radio cannot hear its own transmission decoded by another radio. Needs a second radio and a link. | Rejected |

## Consequences

- One level suits Yaesu receivers on every Crucible radio: the operator picks -34 and moves on.
- A new radio needs its trims measured before its TX level is right. Until then it behaves as today.
- The trims are only as good as the measurement. Voice, distance from the microphone and the hand mic or body mic
  change the reading by a few dB. The method below fixes those.
- The change is generic. It suits RadioFoundry as well; offer it upstream after it works here.
- `docs/user/17-settings.md` still sends the reader to Mic check, which Crucible removed. This change rewrites those
  rows and the steps.

## Measurement method

For each radio and each profile (DMR, then YSF):

| Step | Do this |
|---|---|
| 1 | Set the radio under test to an on-air level of -34 dBFS and its trim to 0. |
| 2 | Use the reference Yaesu receiver (decision 5), 3 m away, same channel. |
| 3 | Read the text below at a normal voice, 5 cm from the microphone, 30 seconds. |
| 4 | Note the receiver's mean level reading during speech. |
| 5 | Repeat with a second voice, and with the hand mic if the radio has one. |
| 6 | Trim = -34 minus the mean of the readings, rounded to whole dB. |
| 7 | Set the trim, transmit once more, and check the reading is within 1 dB of -34. |

Record each radio's readings and trim on its bring-up page. Nothing transmits in a HIL test without `--allow-rf`.

## Plan

| Phase | Work | Needs |
|---|---|---|
| 1 | `board_t.tx_level_trim_db`; leveler table in 1 dB steps, -42 to -10; target = setting + trim, clamped; host tests for the sum and the clamp; spec and user docs | Code only |
| 2 | DM-32UV and DM-1701 DMR trims (+8, +4), confirmed by the method above | The maintainer at the bench |
| 3 | YSF trims; MD-UV380 and MD-9600 trims | The maintainer at the bench |
| 4 | Default -34 dBFS on air | Phases 2 and 3 |

## Maintainer answers

| Question | Answer |
|---|---|
| 1. What was the meter for -34? | A Yaesu receiver. It is the reference receiver (decision 5). |
| 2. Hand mic or body mic, how many voices? | Body mic, the maintainer's voice. The starting trims are provisional (decision 3). |
| 3. Saved levels shift once at the update: acceptable? | Yes. No conversion; the release notes say to pick the level again (decision 7). |
