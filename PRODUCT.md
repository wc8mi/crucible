<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 David Behnke -->
# Product

<!-- impeccable:product-schema 1 -->

Recorded 2026-11-26 from the maintainer's decisions in conversation; the maintainer delegated the remaining choices
("go with your instincts"). Facts marked *(inferred)* were not asked.

## Platform

web

## Users

Amateur radio operators who own one of Crucible's five radios (crucible.toml `[radios]`) and want the maintainer's
build on it: mostly people the maintainer (KF8S) knows or who find the page through tinker.wc8mi.net. They sit at a
desktop with Chrome or Edge, the radio on a USB programming cable, and want to update and get back on the air.
*(inferred: the audience is small and already knows what DMR, P25 and NXDN are)*

## Product Purpose

Crucible is firmware for DMR radios, downstream of RadioFoundry (MIT): a subset of its radios with the maintainer's
own choices (DMR, P25, NXDN and an incomplete YSF; trunking off). Its flasher page updates a connected radio to a
Crucible release that matches it: connect, pick a release made for the radio and its band/power option, install.
Backup and restore stay available, secondary. Success: the radio runs the release the user picked, with its codec,
channels and settings kept.

## Positioning

Not RadioFoundry. Every user-facing surface says Crucible; help comes from wc8mi/crucible issues, never from
RadioFoundry. Contributions go upstream to RadioFoundry. Every release is a pre-release until a build is stable.

## Operating Context

- A static page served under tinker.wc8mi.net/crucible/; no backend. Releases come from wc8mi/crucible's GitHub
  releases, copied next to the page with a checksum-verified index (release-index/1).
- The page sends nothing to RadioFoundry's services: no flash log, no error reporting, no problem-report upload. A
  problem report is saved as a file the user can attach to an issue.
- WebUSB / Web Serial: Chrome or Edge on a desktop, or Chrome on Android.

## Capabilities and Constraints

- The flasher UI is RadioFoundry's (`site/static/js/flasher`), rendered by its JavaScript into one container, styled
  by `site/static/css/tokens.css` and `site.css`. Crucible changes the page shell and the tokens, not the flow, so
  upstream fixes keep merging.
- English only for now *(inferred)*.
- Light and dark themes both exist in the incumbent CSS and must keep working.

## Brand Commitments

- Name: Crucible. Maintainer: KF8S (David Behnke).
- Marks (docs/crucible/brand): hot-core (body `#FF7738`, flame `#FFC45D`) for color uses, single-cut (one color) for
  mono uses. The radio's boot screen draws the same mark.
- The color scheme is the brand's: hot-core orange and flame, not RadioFoundry's colors.
- Credit: "based on RadioFoundry (MIT), not affiliated"; LICENSE and notices kept.

## Evidence on Hand

- Brand files: docs/crucible/brand (SVG marks, PNG icons at 32/180/512, link cards 1200x630).
- No testimonials, user counts or test claims of Crucible's own. RadioFoundry's claims (installation counts, test
  bench) must not appear as Crucible's.

## Product Principles

1. Update first: the common case is a radio already on Crucible taking a newer release.
2. Say what will happen to the radio before it happens; never surprise a user mid-transfer.
3. Crucible, never RadioFoundry, in everything the user reads; credit upstream plainly.
4. Keep the fork thin: change the shell and tokens, not upstream's flow.

## Accessibility & Inclusion

WCAG AA contrast in both themes; keyboard operable; the flasher's existing live region and focus handling kept.
