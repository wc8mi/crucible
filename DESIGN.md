---
name: Crucible flasher
description: The Crucible firmware flasher page, cast iron and molten metal; every progress bar is a pour.
colors:
  hot-core: "#ff7738"
  hot-core-lift: "#ff8a52"
  hot-core-press: "#f0682b"
  flame: "#ffc45d"
  iron: "#1a120c"
  cooling: "#9c3410"
  cooling-mid: "#c84a13"
  mould-iron: "#2a231d"
  iron-black: "#14100d"
  iron-plate: "#1e1915"
  iron-plate-raised: "#2a231d"
  iron-seam: "#3d332b"
  ash-text: "#f2ece6"
  ash-muted: "#b9aca0"
  ash-faint: "#968a80"
  ember-link: "#ff9e6a"
  ember-soft: "#3a1f12"
  greensand: "#d8dbd4"
  greensand-plate: "#eff1ec"
  greensand-raised: "#c8ccc3"
  greensand-seam: "#aeb2a8"
  soot-text: "#1c1612"
  soot-muted: "#4c4f47"
  soot-faint: "#595c54"
  rust-link: "#a33d0b"
  ember-soft-light: "#f7d9c8"
  ok-dark: "#86d6a0"
  warn-dark: "#f2bd57"
  warn-soft-dark: "#33270f"
  bad-dark: "#ff8f80"
  ok-light: "#1d6b35"
  warn-light: "#7c5100"
  warn-soft-light: "#f5e7c8"
  bad-light: "#a3241a"
typography:
  display:
    fontFamily: "Barlow Condensed, Arial Narrow, sans-serif"
    fontSize: "clamp(2rem, 4.2vw, 2.9rem)"
    fontWeight: 700
    lineHeight: 1
    letterSpacing: "0.03em"
  headline:
    fontFamily: "Barlow Condensed, Arial Narrow, sans-serif"
    fontSize: "clamp(1.35rem, 2.2vw, 1.65rem)"
    fontWeight: 700
    lineHeight: 1.1
    letterSpacing: "0.03em"
  wordmark:
    fontFamily: "Barlow Condensed, Arial Narrow, sans-serif"
    fontSize: "1.7rem"
    fontWeight: 700
    lineHeight: 1
    letterSpacing: "0.06em"
  title:
    fontFamily: "system-ui, -apple-system, Segoe UI, Roboto, Helvetica Neue, Arial, sans-serif"
    fontWeight: 650
  body:
    fontFamily: "system-ui, -apple-system, Segoe UI, Roboto, Helvetica Neue, Arial, sans-serif"
    fontSize: "1rem"
    fontWeight: 400
    lineHeight: 1.5
  lead:
    fontFamily: "system-ui, -apple-system, Segoe UI, Roboto, Helvetica Neue, Arial, sans-serif"
    fontSize: "1.05rem"
    fontWeight: 400
    lineHeight: 1.5
  label:
    fontFamily: "system-ui, -apple-system, Segoe UI, Roboto, Helvetica Neue, Arial, sans-serif"
    fontSize: "0.875rem"
    fontWeight: 600
  mono:
    fontFamily: "ui-monospace, SF Mono, Menlo, Consolas, Liberation Mono, monospace"
    fontSize: "0.95em"
    fontFeature: "tnum"
rounded:
  edge: "2px"
  sm: "3px"
  md: "4px"
spacing:
  "1": "0.25rem"
  "2": "0.5rem"
  "3": "0.75rem"
  "4": "1rem"
  "5": "1.25rem"
  "6": "1.75rem"
  "7": "2.5rem"
  wrap: "980px"
components:
  band:
    backgroundColor: "{colors.hot-core}"
    textColor: "{colors.iron}"
    height: "60px"
  button-primary:
    backgroundColor: "{colors.hot-core}"
    textColor: "{colors.iron}"
    rounded: "{rounded.sm}"
    padding: "0.4rem 0.95rem"
    height: "36px"
  button-primary-hover:
    backgroundColor: "{colors.hot-core-lift}"
    textColor: "{colors.iron}"
  button-primary-active:
    backgroundColor: "{colors.hot-core-press}"
    textColor: "{colors.iron}"
  button-quiet:
    textColor: "{colors.ash-muted}"
    rounded: "{rounded.sm}"
    padding: "0.4rem 0.95rem"
  button-quiet-hover:
    backgroundColor: "{colors.iron-plate-raised}"
    textColor: "{colors.ash-text}"
  panel:
    backgroundColor: "{colors.iron-plate}"
    rounded: "{rounded.md}"
    padding: "1.25rem"
  panel-nested:
    rounded: "{rounded.sm}"
  notice-caution:
    backgroundColor: "{colors.warn-soft-dark}"
    textColor: "{colors.ash-text}"
    rounded: "{rounded.sm}"
    padding: "0.75rem 1rem"
  stepper-step:
    backgroundColor: "{colors.iron-plate}"
    textColor: "{colors.ash-muted}"
    typography: "{typography.label}"
    rounded: "{rounded.sm}"
  stepper-step-current:
    textColor: "{colors.ash-text}"
    rounded: "{rounded.sm}"
  release-row:
    backgroundColor: "{colors.iron-plate}"
    rounded: "{rounded.sm}"
    padding: "0.75rem"
  release-row-picked:
    backgroundColor: "{colors.ember-soft}"
    rounded: "{rounded.sm}"
  pour:
    backgroundColor: "{colors.mould-iron}"
    rounded: "{rounded.edge}"
    height: "14px"
  theme-toggle:
    textColor: "{colors.iron}"
    rounded: "{rounded.md}"
    size: "40px"
---
<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 David Behnke -->

# Design System: Crucible flasher

Scope: the one flasher page at tinker.wc8mi.net/crucible/. It does not cover the RadioFoundry site.

The flasher's markup, JavaScript and flow are upstream's (`site/static/js/flasher`, styled by `site/static/css/tokens.css`
and `site.css`). Crucible changes only two things: the page shell (`tools/crucible_site.py`: the hot-core band, the page
head, the footer) and `site/crucible/crucible.css`, loaded last. Every rule below is a token or override in that
stylesheet. A new Crucible surface restyles upstream classes; it does not fork upstream markup.

## Overview

**Creative North Star: "The Single Pour"**

Updating a radio is one heat, one casting, watched until it sets. The page is cast iron and molten metal. Hot-core
orange is the committed shell color: a full-width band at the top carrying the single-cut mark and the CRUCIBLE
wordmark in iron. Flame marks only the live edge. Everything else is iron plate on an iron-black ground (dark) or
greensand grey (light).

Density is operational: one column, one flasher plate, a stepper, one primary action. Headings are condensed uppercase,
like letters cast into iron. Reading text stays in the system sans. Mono appears only for versions, hashes and the build
stamp.

The page refuses the grey installer wizard with a blue accent. Light mode refuses cream: its ground is moulding sand,
grey with a green cast.

**Key Characteristics:**
- Hot-core band across the top; the mark and wordmark in iron.
- Condensed uppercase casting-mark headings (Barlow Condensed 700).
- Plates with square shoulders (3-4px), full borders, no side bars.
- Flame appears only at a live edge: the pour's leading edge, focus, the current step, the picked release.
- Every progress bar is a pour that cools behind its edge and sets when it stops.
- Dark is the default; light follows the system setting unless the visitor picked dark.

## Colors

Two brand colors over a warm iron neutral (dark) or a green-grey sand neutral (light). Dark values are the tokens on
`:root`; light values override them under `prefers-color-scheme: light` and `[data-theme="light"]`.

### Primary
- **Hot-Core Orange** (hot-core): the band, the primary button, the current step's numeral, selection, caret and
  native accent color, the body of the pour. Brand-pinned. Hover lifts to hot-core-lift; press sinks to
  hot-core-press.
- **Pour Iron** (iron): all type and marks on hot-core (7:1). Never a ground color.

### Secondary
- **Flame** (flame): the live edge only. The pour's leading 10px, the 2px bottom edge of the current step and the
  picked release row, and the dark-theme focus ring. Brand-pinned.

### Tertiary
- **Cooling Metal** (cooling, cooling-mid): the pour behind its edge, cooling-to-hot gradient left to right.
- **Mould Iron** (mould-iron): the pour's empty channel, the same iron in both themes so the flame edge always reads.

### Neutral
- **Iron-Black** (iron-black): dark ground. Warm, never neutral grey.
- **Iron Plate** (iron-plate) and **Raised Plate** (iron-plate-raised): dark panels; raised is code, quiet hover.
- **Iron Seam** (iron-seam): dark borders and dividers.
- **Ash** (ash-text, ash-muted, ash-faint): dark text, secondary text, the build stamp.
- **Ember Link** (ember-link) and **Ember Soft** (ember-soft): dark links; the picked-row tint.
- **Greensand** (greensand): light ground. Deliberately not cream.
- **Greensand Plate** (greensand-plate), **Raised Sand** (greensand-raised), **Sand Seam** (greensand-seam): light
  panels, raised fills, borders.
- **Soot** (soot-text, soot-muted, soot-faint): light text tiers.
- **Rust Link** (rust-link): light links and the light-theme focus ring (flame fails contrast on sand).

Status colors (ok, warn, bad, each with a soft tint) are re-tuned per theme for AA; see the frontmatter.

### Named Rules
**The Live Edge Rule.** Flame marks only something live or chosen right now: the pour's leading edge, focus, the
current step, the picked release. It never fills an area and never decorates a static element.

**The Greensand Rule.** The light ground is grey with a green cast (greensand). No cream, beige or warm off-white.

**The Brand Pair Rule.** Hot-core and flame are the only brand hues. Upstream's RadioFoundry accent never appears.

## Typography

**Display Font:** Barlow Condensed 600/700, self-hosted woff2 (latin, latin-ext), OFL-1.1 (with Arial Narrow)
**Body Font:** system-ui stack (upstream's `--font-sans`)
**Label/Mono Font:** ui-monospace stack (upstream's `--font-mono`), tabular numerals

**Character:** a narrow industrial casting face for marks against a neutral reading sans; mono is kept for machine text.

### Hierarchy
- **Display** (700, clamp(2rem, 4.2vw, 2.9rem), 1.0, uppercase, 0.03em): the page h1, "Update your radio". Balanced wrap.
- **Headline** (700, clamp(1.35rem, 2.2vw, 1.65rem), 1.1, uppercase, 0.03em): panel h2s.
- **Wordmark** (700, 1.7rem, 1.0, uppercase, 0.06em): CRUCIBLE in the band only.
- **Title** (650, system sans): h3 inside panels, sentence case.
- **Lead** (400, 1.05rem, muted, 72ch max): the one line under the h1.
- **Body** (400, 1rem, 1.5): all reading text.
- **Label** (600, 0.875rem): stepper steps, buttons.
- **Mono** (0.95em, tabular): release names, versions, hashes, the build stamp (0.72rem, faint).

### Named Rules
**The Casting Mark Rule.** Only h1, h2 and the wordmark use the condensed face, always uppercase. Body, labels and
h3 stay in the system sans.

**The Machine Text Rule.** Mono is for strings a machine wrote (release names, hashes, the build stamp). Numerals in
mono, code and the stamp are tabular.

## Layout

One centered column, max width 980px (upstream's 1200px narrowed), with upstream's gutter. Order: band (min 60px; mark
and wordmark left, theme toggle right), page head (h1 and one-line lead), the flasher plate (stepper, notices, panels,
sticky action bar), then the credit footer on a plate-colored strip below the fold. Spacing uses upstream's scale
(0.25 to 2.5rem). Panels pad 1.25rem; the page head opens with 1.75rem; the footer sits 2.5rem below content. Below
760px upstream's responsive rules apply unchanged.

## Elevation & Depth

Mostly flat plates separated by full borders and tonal steps (ground, plate, raised). Upstream's panel shadow is kept,
re-tinted per theme. The pour's mould is the one inset.

### Shadow Vocabulary
- **Plate (dark)** (`box-shadow: 0 1px 1px rgb(0 0 0 / 35%), 0 6px 18px rgb(0 0 0 / 28%)`): panels and the action bar.
- **Plate (light)** (`box-shadow: 0 1px 1px rgb(40 24 12 / 8%), 0 6px 16px rgb(40 24 12 / 9%)`): the same in light.
- **Mould** (`box-shadow: inset 0 1px 2px rgb(0 0 0 / 45%)`): the pour channel.
- **Live edge** (`box-shadow: inset 0 -2px 0 flame`): the current step and the picked release row.

### Named Rules
**The Bottom Edge Rule.** A chosen or current item takes a 2px flame bottom edge, drawn inset. Never a side stripe.

## Shapes

Square shoulders: 4px on plates and the theme toggle, 3px on nested plates, buttons, steps, release rows, tags and
notices, 2px on the pour, step numerals and the focus outline. Upstream's pills (999px) and 10px corners are all
replaced. Every bordered element carries a full 1px border; upstream's 4px left notice bar is removed. The single-cut
mark is drawn inline in `currentColor`; done steps use a masked check SVG (`site/crucible/check.svg`) in ok, not a
glyph.

## Components

### Buttons
- **Shape:** square shoulders (3px).
- **Primary:** hot-core fill and border, iron text, 600 weight. One per step: Connect, then Update.
- **Hover / Active:** hot-core-lift, then hot-core-press. No brightness filter.
- **Quiet:** transparent, muted text; hover fills raised plate with full-strength text.
- **Focus:** 2px outline in focus color (flame dark, rust light), 3px offset. Inside the band the outline is iron.

### Cards / Containers (plates)
- **Corner Style:** 4px; nested 3px.
- **Background:** plate token on the ground.
- **Border:** 1px seam, full.
- **Internal Padding:** 1.25rem; h2 then 1rem gap.

### Notices
- **Style:** soft status tint, full 1px border at 55-60% of the status color, 3px corners. Info uses the seam color.
- **The standing caution:** the flasher's "do not switch apps" line is toned warn, not bad, so a real error stands out.

### Navigation (band)
- Hot-core strip with a 1px dark bottom rule. Brand link: 26x30px single-cut mark plus wordmark, in iron. Theme
  toggle: 40px square, transparent, 1px iron border at 35%, 10% iron tint on hover.

### Stepper
- Plates in a row (3px), label type. Current: hot-core border, hot-core numeral with iron text, flame bottom edge.
  Done: a masked check in ok.

### Release list
- Plate rows (3px) with radio, mono release name, tags. Picked: hot-core border, ember-soft tint, flame bottom edge.

### The Pour (signature)
- 14px iron channel (mould-iron in both themes), 2px corners, inset mould shadow.
- Fill: gradient cooling, cooling-mid at 40%, hot-core at 82%, flame at 100%. Width eases 0.4s
  cubic-bezier(0.16, 1, 0.3, 1).
- While active, a 10px flame edge pulses opacity 1 to 0.35 over 1.1s, alternating.
- Once stopped (done or error) the casting sets: cooling to hot-core, no flame.
- Reduced motion: no width transition, the edge holds at full opacity.

## Do's and Don'ts

### Do:
- **Do** put hot-core only where the brand speaks or the user acts: band, primary button, current step, pour.
- **Do** keep flame on a live edge only (The Live Edge Rule).
- **Do** mark the current or chosen item with a 2px inset flame bottom edge.
- **Do** keep every plate square-shouldered (2-4px) with a full 1px border.
- **Do** set h1, h2 and the wordmark in Barlow Condensed 700, uppercase.
- **Do** keep the pour's mould iron (#2a231d) in both themes and drop the flame when the pour stops.
- **Do** keep all text WCAG AA in both themes; on sand, use rust instead of flame for focus.
- **Do** restyle upstream classes from crucible.css; leave the flasher's markup and flow upstream's.

### Don't:
- **Don't** use side-stripe borders (border-left accents) on notices, rows or panels.
- **Don't** use cream, beige or warm off-white for the light ground.
- **Don't** use pills (999px) or soft 7-10px corners.
- **Don't** use the condensed face for body text, labels or h3.
- **Don't** tone a standing caution as an error.
- **Don't** use RadioFoundry's accent colors or name RadioFoundry as the product.
- **Don't** add eyebrows or kickers above headings.
