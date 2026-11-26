<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->
<!-- Copyright (c) 2026 David Behnke -->

# Security

## Reporting a vulnerability

Report vulnerabilities privately. In GitHub,
under [Crucible's Security Advisories](https://github.com/wc8mi/crucible/security/advisories),
click [**Report a vulnerability**](https://github.com/wc8mi/crucible/security/advisories/new).
Don't open a public issue or pull request about it.

Most of Crucible's code is RadioFoundry's. If the problem is in RadioFoundry's own release too, report it to
[RadioFoundry](https://github.com/radiofoundry/radiofoundry/security/advisories/new) as well, so every radio gets the
fix. We pick it up at the next merge.

Include what is affected (the firmware, and on which radio; the Crucible flasher; the tools), the release or commit,
and how to reproduce the issue. Crucible is maintained by one person: expect an answer, not a deadline. The advisory
credits you unless you'd rather not be named.

## In scope

- The firmware: input that makes the radio do something it shouldn't, such as crash, run attacker-supplied code or
  transmit without the user asking it to. That covers a received transmission, anything on the network side (a
  hotspot, IP traffic over DMR, a browser on the web control page), the computer on its USB port, and a codeplug.
- `tools/rfoundry` and the Crucible flasher: a crafted firmware image or other file that runs code on the computer, or
  "bricks" a radio so it can't be recovered.
- The Crucible site and the CI workflows: anything that lets someone, by opening a pull request or by visiting the
  site, change what is built, published or served.

## Not a vulnerability

- Eavesdropping, spoofing a call sign or DMR ID, or breaking DMR basic privacy. These modes don’t authenticate
  senders, and basic privacy is a weak scrambler, not encryption.
- Attacks that need physical access to the radio. Anyone holding it can start the bootloader (keys held at power-on)
  and flash whatever they like; that's by design.
