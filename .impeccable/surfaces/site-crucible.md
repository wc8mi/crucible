---
version: 1
slug: "site-crucible"
primary_target: "site/crucible"
related_targets: ["tools/crucible_site.py"]
---

# Crucible flasher page

Scope: the one page at tinker.wc8mi.net/crucible/ (built by tools/crucible_site.py from the shared flasher). Mode: Operate.
Audience and job: Crucible radio owners updating to a release made for their radio. Constraints: the flasher's JS, flow and
markup stay upstream's; Crucible owns the page shell and a stylesheet loaded last. Both themes. Pinned by the maintainer:
the Crucible logo and the brand colors (hot-core #FF7738, flame #FFC45D). The maintainer delegated the rest; no decision
round was held.

## Direction contract

THESIS: Updating a radio is a single pour: one heat, one casting, watched until it sets. Refuses the grey installer
wizard with a blue accent that every flasher ships.

OWN-WORLD: Cast iron and molten metal. Dark: warm iron-black ground, iron-plate panels. Light: foundry-sand grey, never
cream. Hot-core orange is the committed shell color: a full-width header band carrying the single-cut mark and the
wordmark in iron. Flame marks only the live edge. Condensed uppercase casting-mark headings; system sans for reading;
mono only for versions and hashes. Plates have square shoulders (4px), full borders, no side bars.

STORY: The visitor sees Crucible, not RadioFoundry; connects the radio; sees the releases built for it; installs;
watches the pour; reads that it is done and where to get help (Crucible's issues).

FIRST VIEWPORT: Hot-core band: mark and CRUCIBLE left, theme toggle right. Heading "Update your radio" with a one-line
lead. The flasher plate below: stepper, then Connect as the one hot-core primary button. Credit footer below the fold.

FORM: own grounded list, candidate 7 of 7 (the foundry pour); seed:431f93115e50c3d9. Signature move: every progress bar
is a pour, a molten fill whose flame-bright leading edge cools to deep orange behind it; reduced motion holds it still.

FINISH: unreviewed and undocumented is unfinished; this build ends with the finish review, the verdict, DESIGN.md, and every shipping raster carrying its provenance
