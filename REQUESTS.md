# TickerBar — REQUESTS (per-item status ledger)

Verification rule (Jackson, 2026-10-29): every item is DONE only with
proof — the full core suite green after the change, new checks for new
behaviour, and errors recorded here with their actual text, never hidden.

## v0.5.8 (2026-10-31) — Visualizer round: melted blur, calmed whites, Lines style (Jackson's asks)

Jackson (2026-10-31 14:17 PT, with reference frames at 14:32 PT):
(1) background blur increased so the colours smudge together into
a wash — the dots should melt into each other, not read as
distinct dotted circles; (2) the white bulbs calmed — clusters of
near-opaque grey-white discs are the loudest element in the bar;
give them the same movement strength/smoothing as the colourful
dots so they sit at the same energy as the rest of the field;
(3) a NEW "Lines" visualizer style alongside Bubbles (not
replacing it): smooth, hand-drawn-feeling flowing lines/ribbons,
one per frequency band (low/mid/high), spring-smoothed, thickness
swelling with the band's level, near-still when quiet.

- **Blur melt — DONE (all three bars, identical).** The composite
  kernel goes 7/2.8 -> 15/5.5 and the crisp pass backs off
  0.45 -> 0.30 of the background opacity (drawVisualizerBackground),
  so the blurred wash is what reads and the dots smudge into it.
- **Whites calmed — DONE (all three bars, identical, core
  Visualizer.hpp).** Three changes to the ambient convergers:
  velocity ease-out 0.995 -> 0.985 per frame (the burst dots' own
  value in the home model — they streamed hotter and piled up at
  the centre); brightness envelope gains the burst dots' fade-OUT
  over the last 30% of life (they used to sit at full brightness
  until the respawn pop); peak capped at kAmbientPeak = 0.55 of
  full white (they rendered at a full 255). The silence gate is
  untouched: no energy, no whites. New checks pin the cap and the
  fade-out; the v0.4.0 gate check's brightness threshold was
  retuned to the new scale (intent unchanged, recorded in the
  test).
- **Lines style — DONE (all three bars, identical).** New pure
  core model VisualizerLines (Visualizer.hpp): three ribbons at
  the burst anchors, each band's level a damped spring
  (k=42, d=9) toward the band energy, centreline = two detuned
  sines + a baked per-point wobble (hand-drawn, never a machine
  sine), half-thickness swelling with the level and breathing
  along the line; phase creep and amplitude collapse when quiet
  (near-still), and the particle field's activity gate fades the
  ribbons out in silence. Rendered as filled paths through the
  same group field map + blur composite as the dots. Selectable
  beside the existing visualizer controls ("Visualizer style"
  combo next to "Background effect" in Settings -> Appearance),
  persisted like the other appearance settings (SettingsStore
  "vizStyle").
- **Link carrying — the style is shared background state, carried
  like the effect.** BLK4 (magic bumped in all three cores; slot
  layout unchanged, the bgEffect ordinal's domain widened):
  1 = visualizer Bubbles, 3 = visualizer Lines. A style change in
  any linked bar adopts in the others with the effect, so the
  linked strip never mixes Bubbles on one bar with Lines on the
  next. Mixed BLK3/BLK4 binaries never link (the family's magic
  rule).
- Installer fallback define bumped with the version (0.5.8) —
  the version_guard ctest holds it to the single source.
- Honest limit: the LOOK is verified only on Jackson's PC. The
  evidence here is the code, the checks, and his frames.

## v0.5.7 (2026-10-31) — Arrange takes maximized windows (Jackson's ask)

Jackson (2026-10-31 15:00 PT, verbatim): "the arrange does not
work when the application is already meximized".

- **DONE (all three bars, identical).** Root cause, in the code:
  the EnumWindows callback in ScreenSpace.cpp skipped IsZoomed
  windows outright, so a maximized app was never arranged. Now a
  zoomed window participates through its NORMAL rect
  (GetWindowPlacement rcNormalPosition): the core plan
  (planArrangeWindow in Arrange.hpp) fits that rect, the window is
  snapshotted BEFORE being touched (normal rect + wasMaximized),
  un-maximized (SW_RESTORE), then fitted. Restore honours the flag
  (planRestoreWindow): snapshot rect applied, then SW_MAXIMIZE —
  never stranded restored-down.
- **Port drift repaired in the same change:** this repo's sweep
  had lost two MeterBar v0.18.1 invariants the siblings kept —
  snapshots were CLEARED every sweep (a second sweep remembered
  post-first-sweep positions as home) and the GUI_INMOVESIZE
  guard was missing. Both are restored: write-once snapshots
  (only Restore clears) and the in-move/size skip, so the three
  sweeps are identical again. Windows TU verified by the MinGW
  syntax+type check; real arrange behaviour is field-verified on
  Jackson's PC.

## v0.5.6 (2026-10-31) — stationary Sizes editor + visible Link background toggle (Jackson's ask)

Jackson (2026-10-31 14:57 PT, verbatim): "still need a visual UI
way of changing the size of each thing thats going by for the
ticker bar and to link th ebackground".

- **Sizes panel — DONE.** Settings gains a "Sizes" panel: one row
  per watchlist symbol (name, slider, px readout; "auto" until a
  width is set), plus "Reset all coin widths to auto". It writes
  the SAME store the on-bar dividers write — TickerState::
  cellWidths, via MainComponent::setSymbolCellWidth — so panel and
  dividers always reflect each other, and it applies live in both
  the fitted layout and the scrolling marquee (same saveSettings +
  resized tail as a divider drag). No parallel store. Band: core
  clampSymbolCellWidth, floor = the fitted solver's own 90 px
  (vertical floors at 56 and clamps at layout), ceiling = the
  family's existing dragged-extent cap (kContextCellMax, 600).
  Reset clears only the widths (the full "Reset layout to
  defaults" still exists separately and also clears them).
- **Link background — DONE (with MeterBar v0.21.3 / SessionBar
  v0.4.5).** Diagnosis of the Oct 29 "did not visibly propagate"
  report: the style/opacity publish-adopt path was complete
  (traced end-to-end in current code), but (1) there was no
  visible control or mention anywhere — nothing told the user
  backgrounds were linked or let him turn it off; and (2) the
  background EFFECT selection (styled / visualizer / glow) was
  not in the packet at all, so with the visualizer on — Jackson's
  look — a peer's style change drew nothing different and effect
  changes never crossed. Fix, following the family's BLK2
  precedent: LinkSlot gains bgEffect (appended), BgPacket/
  BgSyncState carry it, magic BLK2 -> BLK3 (mixed versions never
  link, as before); applyBg now also runs syncFxPump so an
  adopted effect actually starts. Plus the visible control: a
  persisted "Link background" toggle (state.linkBackground,
  default ON) beside the Background effect control in Settings;
  the client gates both publish and adoption on it, and turning
  it on re-stamps the current background for peers.

## v0.5.5 (2026-10-31) — GitHub link, the Reposition dropdown, and Reserve moves into it (Jackson's asks)

Jackson's asks (2026-10-31 14:13 PT): "can you add a link to the
git hub page for each on inside oeach? also i like the drop down
box you did for meter bar can you add that to session and ticker?
also what does the reserve button do? also for the button thatr
resizes the program so that your other browsers when hit mazimize
will not bock the meter bar. can we make the link button do that
instead? ideally this would be in the reposition drop down instead
of the other button we have" — his decision on the follow-up
question: Reserve goes into the reposition dropdown (NOT onto the
Link button).

- **(a) GitHub link in Settings — DONE.** The About panel's row is
  now "Check for updates" | "GitHub"; GitHub opens
  https://github.com/Corporal8065/TickerBar in the default browser
  (juce::URL::launchInDefaultBrowser). It sits next to the version
  line, which now reads the real single-source version (the About
  note carried a stale hard-coded "v0.4.0" in prose — same stale-
  literal family as the v0.5.3 fix; it now composes from
  kVersion).
- **(b) Reposition dropdown — DONE.** New Reposition button beside
  Preset (MeterBar's placement, 96 px) opening showRepositionMenu,
  a port of MeterBar's menu mapped item by item: "At the Top /
  Bottom / Left / Right" (MeterBar has the first two; this bar
  docks to four edges — each item ticked when current; picking one
  sets the edge, refreshes the Dock button, saves, snaps);
  "Pin Above Other Windows" (identical to MeterBar's);
  "Move to Display" submenu (identical shape; the move centres on
  the target display, then snapToDockEdge settles the bar onto its
  current edge there). All actions deferred until the popup closes
  (MeterBar's 2026-10-13 field lesson). NOT ported, by feature
  absence: MeterBar's "Stick" (no stick state here) and its
  "Show Reposition Hint" toggle (no hint label here — the strip's
  middle is the feed status label).
- **(c) Reserve into the dropdown — DONE.** The standalone Reserve
  button is removed from the button row (its 76 px closes up in
  both orientations' cluster layout; chrome colour list and the
  two toggle-mirror syncs dropped with it). The menu gains a
  ticked "Reserve Screen Space" item toggling the SAME
  state.reserveScreenSpace through the same syncScreenSpace()
  path, with saveSettings() — the same key as before, so a bar
  with Reserve on keeps it on. What Reserve does (the explanation
  Jackson asked for): it registers the bar as a Windows AppBar on
  its docked edge, shrinking that monitor's work area so
  maximised windows stop at the bar instead of sliding under it.

## v0.5.4 (2026-10-31) — per-symbol sizing back: the overflow marquee no longer hides the dividers (Jackson's report)

Jackson's report (2026-10-31 14:09 PT, after updating): "ok ticker
bar update good but now i cannot custom change size of each symbol
directly by clicking on the asset bubble thing." The feature: the
per-symbol sizing dividers between the coin cells (the divider
system, v0.3.0; untouched by v0.4.0's feel pass).

- **Root cause — FOUND in the diff, and it is not a mouse-code
  regression.** The v0.4.0 → v0.5.0 diff was audited hunk by hunk:
  mouse handlers, the CellSplitter components, the layout solver,
  the marquee math, state/settings, and window sizing are ALL
  unchanged; v0.5.0's only app delta is the paint-time group-field
  remap in drawVisualizerBackground plus the pure fieldMap core
  math. The visualizer field is NOT a component — it is drawn in
  MainComponent::paint underneath the child components and cannot
  intercept clicks (the family's only overlay, MascotView, already
  has setInterceptsMouseClicks(false, false) and is untouched).
  v0.5.1 is installer-only; v0.5.2 floors link-applied bounds by
  position only (sizes preserved: planStackAll / groupDockRect /
  snapToPeers / ensureRectVisible all verified size-preserving);
  v0.5.3 touched version strings only. The ONLY mechanism in the
  app that can remove per-symbol sizing is the v0.4.0 overflow
  marquee: the moment the strip's natural extent exceeds the
  window, resized() hid EVERY splitter and the strip scrolled —
  the user's only sizing handles vanished for the entire marquee
  phase, with no way back short of widening the window. An
  overflowing strip could therefore never be sized back down:
  that is the defect.
- **Fix — DONE in code.** The coin dividers stay live during the
  marquee: positionStrip() places each divider in the scroll gap
  after its pair's first cell (new core helper marqueeSplitterRel:
  item edge + (gap − grip)/2 = the fitted layout's own +3 px air
  rule, tracking the item's wrapped scrolled position), and a
  divider press now holds the scroll for the drag
  (splitterDragHold_ joins dragMode_ in stepFx's advance gate —
  the v0.4.0 "paused during drags" promise, made true for divider
  drags). Dragging adjusts the pair's widths through the same
  onDragDelta machinery as the fitted layout; if the strip then
  fits, the next resized() returns to the fitted layout on its
  own. The context/trailing dividers still hide during the
  marquee (their cells are scroll items; their sizing returns
  with the fitted layout). The scroll itself is unchanged.
- **Proof:** 4 new core checks pin marqueeSplitterRel (centred in
  the gap; tracks a wrapped, half-out item; narrow-item air rule);
  full core suite 513 checks, 0 failures + version guard green.
  The full app build also completed on this VM (exit 0, TickerBar
  executable linked) — the Windows CI build remains the installer
  proof, as usual.

## v0.5.3 (2026-10-31) — app reports its real version: stale 0.4.0 literals killed (Fiona's report, primary root cause)

Fiona's report (2026-10-31): after updating TickerBar — in-app,
and even after a manual install of the 0.5.0 Setup.exe — the app
still reported 0.4.0 and kept offering the 0.5.0 update. The
v0.5.1 fix (below) addressed a real defect, but not this symptom's
primary cause: her manual install had actually worked — the app
was misreporting its own version.

- **Single-source version — DONE.** The current-version strings
  were hard-coded literals last bumped at v0.4.0: Main.cpp's
  getApplicationVersion() returned "0.4.0", and MainComponent.cpp's
  kVersionNum ("0.4.0") was passed to the updater as the current
  version while kVersion ("v0.4.0") fed the Settings label and the
  updater dialogs. Ported MeterBar's v0.19 pattern: root project()
  VERSION is the single source of truth; configure_file generates
  TickerBarVersion.h (app/TickerBarVersion.h.in) with
  tickerbar::kVersionString, which getApplicationVersion(),
  kVersion, and the updater call now all read. app/CMakeLists.txt
  takes its gui-app VERSION and JUCE_APPLICATION_VERSION_STRING
  from ${PROJECT_VERSION}. Grep proof: no semver string literal
  remains in Main.cpp / MainComponent.cpp; the only one left in
  app/Source is a format example in an Updater.h comment.
- **Guards — DONE.** 3 new compiled checks in test_core assert
  the generated header equals the project version (string and
  major/minor/patch), and a new ctest entry, version_guard
  (tests/version_guard.cmake), fails on any bare semver literal
  assigned/returned in the two TUs or any carrier drift (gui-app
  VERSION, JUCE string, .iss fallback, generated header).
  Negative-tested: bites on a reintroduced literal and on a
  drifted .iss; passes clean.
- **Version 0.5.2 → 0.5.3** — root CMakeLists.txt project
  VERSION + TickerBar.iss fallback define; the rest follows.
- **Proof:** core suite 506 → 509 checks, 0 failures; ctest 2/2
  (core + version_guard). Full app build on this VM (JUCE 8.0.4):
  the TickerBar target compiled and linked. Windows CI compiles
  the real installer on the v0.5.3 tag, as usual.

## v0.5.2 (2026-10-31) — link applies get a visibility floor (Jackson's "stuck somewhere i cannot see" report)

Jackson's report (2026-10-31, #crypto-book, about SessionBar —
same defect here by construction): "Session bar is also for me
still stuck somewhere i cannot see upon load up and have no idea
how to fix plz help".

Root cause: the v0.3.0 clamp guards the two places a SAVED rect is
applied — the startup restore and preset apply
(`applyWindowRectFromState`). It never covered the family LINK:
with Link on, stack plans, drag follows, snap settles and group
docks move the bar AFTER startup through the link client's
setBounds callback, unguarded. A stale stack request in the shared
block, or a peer docked against space no monitor owns any more,
could move the just-restored bar off-screen within a tick of
launch — and the position-save feed then saved the off-screen
rect, so every later launch restored (clamped) and re-lost it the
same way.

- **Visibility floor in core WindowGeom — DONE.** New pure rule,
  identical in all three repos: `rectMeaningfullyVisible` (at
  least 100x40 px of the rect inside ONE display work area — the
  rect itself when it is smaller than that) and
  `ensureRectVisible`. A meaningfully-visible rect — a bar on a
  second monitor, a straddle — is returned untouched; below the
  floor the rect keeps its size and lands on the primary work
  area, centred horizontally, near the top. 12 new checks: fully
  off-screen rescued, second display untouched, above-floor
  partial untouched, sliver and sunk rects rescued, straddle
  untouched, no-areas no-op. Suite 494 → 506 checks, 0 failures.
- **Link setBounds funnel guarded — DONE.** This bar's
  `linkClient_.setBounds` callback — the single funnel every
  remote apply passes through — now runs its target through
  `ensureRectVisible` with the live displays' user areas before
  touching the window. The fix landed first in SessionBar v0.4.2
  (the reported bar) and is ported identically here and in
  MeterBar v0.21.1.
- **Version 0.5.1 → 0.5.2** in the single-source places: root
  CMakeLists.txt, app/CMakeLists.txt (incl.
  JUCE_APPLICATION_VERSION_STRING), TickerBar.iss fallback.
  Proof: full core suite green at 506 checks, 0 failures; the
  edited app TU passes a full g++ type-check vs JUCE 8.0.4.
  Windows CI + Jackson's eyes remain the visual proof.

## v0.5.1 (2026-10-31) — installer relaunches after silent in-app updates (Fiona's report)

Fiona's report (2026-10-31): after updating TickerBar — in-app, and
even after a manual install of the 0.5.0 Setup.exe — the app still
reported 0.4.0 and kept offering the 0.5.0 update.

- **Silent-relaunch [Run] entry — DONE.** TickerBar.iss carried only
  the interactive `nowait postinstall skipifsilent` launch entry, so
  a silent in-app update (installer run with /SILENT
  /CLOSEAPPLICATIONS /RESTARTAPPLICATIONS /NORESTART, after which the
  app quits) never relaunched the new version. Ported MeterBar's cure
  verbatim — a second [Run] entry with NO skipifsilent, gated to
  silent installs via `Check: WizardSilent` (it landed there with
  MeterBar's v0.19 updater work).
- **Version 0.5.0 → 0.5.1** in the single-source places: root
  CMakeLists.txt, app/CMakeLists.txt (incl.
  JUCE_APPLICATION_VERSION_STRING), TickerBar.iss fallback define.
- **Proof:** full core suite green after the change — 494 checks,
  0 failures (count unchanged from v0.5.0; the change is
  installer-only, no core behaviour touched). The .iss was re-read
  after editing: all sections intact, the gated entry inside [Run].
  Windows CI compiles the real installer on the tag, as usual.
- **Correction (2026-10-31, v0.5.3):** this entry's fix is real
  but SECONDARY for Fiona's symptom. A missing relaunch entry
  cannot explain the app still reporting 0.4.0 after a manual
  install — that was the stale hard-coded version literals in
  Main.cpp / MainComponent.cpp (frozen at 0.4.0), the primary root
  cause, fixed in v0.5.3 above.

## v0.5.0 (2026-10-31) — one linked background across the bars (Jackson's ask)

Jackson's words (dictated, 2026-10-31): "the addition to MeterBar
where we change the background graphics needs to be applied to
not only MeterBar but when you connect the other 2 softwares to
all link and instead use the combination of the 3 resolutions as
the sizing for it, and then when sound is played with specific
frequencies and or transients that we have set up will do its
thing."

- **Group field map — DONE in code, core-proven.** New pure core
  in `BarLink.hpp` (identical in all three repos): `fieldMap` /
  `fieldToLocal` / `fieldDotVisible`. When the bars are linked,
  the visualizer's unit field spans the GROUP rect — the union of
  the linked bars' screen rects, i.e. the combination of their
  resolutions — and each bar renders only its own slice of it.
  Burst anchors and the ambient convergence centre sit at the
  centre of the COMBINED strip, so the audio reaction plays
  across the whole linked strip instead of restarting per bar.
  17 new checks pin the seam arithmetic (the field centre maps
  to the left bar's right cache edge AND the right bar's left
  cache edge — one screen point), the three-bar middle third,
  vertical stacks, degenerate-rect fallback and dot culling.
  Suite 477 → 494 checks, 0 failures.
- **Render — DONE in code.** `drawVisualizerBackground` maps each
  particle through the field map. Unlinked (or linked but alone)
  the map is the bar-local identity and the v0.4.0 render runs
  unchanged. Dot radius is deliberately NOT remapped — a dot
  keeps its physical size; only positions come from the group.
  The edited translation unit passes a full g++ syntax/type
  check against the JUCE 8.0.4 headers (0 errors). The Windows
  CI build after push, and Jackson's eyes on the linked strip,
  are the remaining proof — nothing visual was verified here.
- **Siblings.** MeterBar v0.21.0 renders the same field from its
  own audio (it is the BarLink broadcast source) and SessionBar
  v0.4.0 renders it from the broadcast; SessionBar also received
  the feel-pass render treatment it missed in the v0.4.0 round
  (its own REQUESTS record).

## v0.4.0 (2026-10-31) — Jackson's feel pass (his direct ask, web chat)

His words, lightly trimmed: "(1) resizable however you want,
including each ticker; (2) if it's more than the current width
it scrolls to the left to add each one like the news; (3) the
visualizer background with the colorful sound-reactive circles
— more curved; (4) don't show the white circles that come with
the null sections of the response; (5) increase the ability to
put a larger input in the trim; (6) there isn't really a blur
yet — more vibey, a blur in the background, darker tone;
(7) each symbol's background transparent too."

- **(1) Free resize — DONE in code.** Window resize limits
  160..7680 × 48..2160 (was the 412..692 px "supported" floor
  × 92..1200); the constructor's restored-width clamp lowered
  to 160; the trailing divider's drag range follows the same
  limits. Per-ticker resizing is the existing divider system,
  untouched. Proof: core suite green (below); the app-layer
  numbers are plain limit constants.
- **(2) Overflow marquee — DONE in code.** Core gains the pure
  marquee math (`stripNaturalExtent`, `marqueeNeeded`,
  `marqueeCycle`, `marqueePosition`) with 15 new checks:
  fits → no scroll; overflow → cycle = content + 56 px gap;
  positions wrap so every item crosses a window narrower than
  the content over one cycle. App: when the natural strip
  (dragged-or-default coin widths + visible context cells)
  exceeds the room, dividers hide, the strip scrolls at
  ~45 px/s via the fx pump, paused during drags; both
  orientations. When it fits, the previous fitted layout runs
  byte-for-byte as before.
- **(3) Rounder circles — DONE in code.** Rendered dots 1.35×
  plumper and the blur/crisp balance shifted (see 6) so the
  shapes read as soft rounds. The core geometry (arcs and
  strands) is unchanged — the roundness Jackson sees is the
  render, and the render is what changed.
- **(4) No white circles in silence — DONE, core-proven.**
  Root cause: the visualizer's ambient particles are WHITE by
  design and keep converging whether or not the bands carry
  energy, so null sections of the linked MeterBar feed (and
  plain silence) showed white circles. Fix: an activity gate
  in the engine — smoothed max band energy (attack 6/s,
  release 0.8/s); ambient white brightness scales by
  clamp(activity / 0.30), so silence fades the whites to
  invisible while music keeps them. New checks: after 4 s of
  zero bands the brightest ambient particle is < 12/255;
  after 0.5 s at 0.5 energy it is > 150/255. Colored burst
  strands are not gated (they only fire on real hits anyway).
- **(5) Bigger trim input — DONE by interpretation (MeterBar).**
  TickerBar has NO trim control anywhere (searched: the only
  "trim" in this repo is whitespace trimming). The family's
  trim is MeterBar's "Trim dB" input trim, so the wider range
  landed there: **−24..+24 dB → −48..+48 dB** in MeterBar
  v0.20.3 (slider + state comment; default +10.18 dB and the
  gain law unchanged).
- **(6) Real blur + darker tone — DONE in code, needs field
  eyes.** Two layers: (a) the visualizer render itself now
  blurs harder (Gaussian kernel 5/1.8 → 7/2.8), the crisp pass
  drops 0.9 → 0.45, and a 0.18 black scrim darkens the whole
  effect; (b) the window now requests a REAL Windows backdrop
  blur — acrylic (`SetWindowCompositionAttribute`,
  ACCENT_ENABLE_ACRYLICBLURBEHIND, dark plum tint) behind the
  bar's translucent pixels, plain blur-behind as fallback,
  dynamically loaded. Neither can be seen from the build VM;
  Jackson's Windows check is the proof for (b) especially.
- **(7) Transparent symbol backgrounds — DONE in code.** The
  panel fill is removed from both cell paints (coin cells and
  context cells); borders, flash states and stale dimming are
  unchanged, so the bar background shows through every symbol.

Verification: core suite 455 → **477 checks, 0 failures**
(includes the 22 new gate/marquee checks); MeterBar's suite
985 → **989 checks, 0 failures** for its half. App layer,
recorded honestly: the first full JUCE app build FAILED on a
real error in this change — `MainComponent.cpp:957/1044:
error: unable to deduce 'auto*' from
'__gnu_cxx::__normal_iterator<std::unique_ptr<CellSplitter>*,
...>::operator*()'` — the marquee blocks ranged over
`splitters_` (a vector of unique_ptr) with `auto*`. Fixed to
`auto&`; both edited translation units (MainComponent.cpp,
Main.cpp) then pass a full syntax/type check against the
JUCE headers with zero errors. A complete app link on the
build VM was afterwards blocked by missing X11 extension
dev headers (the VM was replaced mid-verification), so the
Windows Actions build on push remains the full-build proof.
Delivery state: committed locally — the 2026-10-31 push to
main was blocked by the VM's egress proxy resetting SSH to
GitHub; no release was cut. The visual items (2's feel, 3,
6) are Jackson's to confirm on his machine.

## v0.3.2 (2026-10-29) — Vishona's follow-ups, via #crypto-book

- **Coins fill the space when the context cells are hidden.**
  Her words: "after removing the fear/greed and dominance
  sections the hype, eth, and btc tickers cannot move to the
  right to fill space and leaves too much, there also itsnt a
  draggable section to the right of the hype ticker." Root
  cause: `solveCellWidths` distributed leftover width only
  among cells WITHOUT an explicit width. Every divider drag
  persists explicit widths for both sides of the pair (and the
  v0.3.1 context divider A persists the last coin's width too),
  so her bar was all-explicit: the solver returned the widths
  verbatim and the remainder — the ~256 px the hidden context
  cells freed — belonged to no cell. Fix in the core solver:
  when every cell is explicit and the sum undershoots the
  strip, the last cell absorbs the surplus (the fill-exactly
  rule the auto path always had). One solver serves both
  orientations, so vertical stacks fill downward the same way.
  Explicit overshoot still stands (the window floor keeps it
  out of the app; the solver never shrinks a user's drag).
- **A draggable divider right of the last coin.** Divider A
  only exists next to a visible context cell, so hiding both
  removed the only handle right of HYPE. New trailing divider
  (`trailSplit_`, the same CellSplitter): shown exactly when no
  context cell follows the coins, placed at the coin block's
  end with the same 16 px boundary (reserved from the solver,
  counted by `minimumBarWidth()`). Its pair is the last coin
  against the bar end, so dragging it resizes the window along
  the bar axis — clamped by the core `trailingWindowExtent`
  to the window's own limits — and the fill rule hands the
  change to the last coin cell. The window's resize
  notification persists the new bounds. Vertical mode mirrors
  it under the last coin row (the stored extent doubles as
  height, as everywhere else).
- **"Reset to defaults" button.** Her words: "there also
  should be a "reset to defaults" button somewhere". Added to
  Settings > Context cells as "Reset layout to defaults",
  backed by the pure-core `resetLayoutToDefaults`: context
  toggles back on, `cellWidths` cleared, `fngCellWidth` /
  `domCellWidth` back to 0, and the coin order restored to
  the factory BTC / ETH / HYPE sequence. Scoped to layout
  only — watchlist membership (extra symbols keep their
  relative order after the trio), alerts, poll interval,
  theme and window settings are NOT touched. Applies through
  the normal path: `settingsChanged(true)` (apply + persist +
  rebuild cells in the restored order + deferred sheet
  rebuild, so the sheet shows the reset state).
- Proof: core suite 437 → 455 checks, 0 failures (18 new
  checks: the all-explicit fill incl. exact-fit / overshoot /
  single-cell / mixed cases, the trailing-extent clamp, and
  the reset restoring layout fields while leaving watchlist
  extras, alerts and other settings untouched). The JUCE app
  layer cannot be compiled on this VM — its proof is the
  push's GitHub Actions Windows build, as with prior rounds.
  Released as v0.3.2: version bumped in the same spots the
  v0.3.1 bump touched and tagged; the tag's Actions run
  builds and publishes the release. (Milo ships Vishona's
  bar changes per Jackson's 2026-10-29 standing instruction.)

## v0.3.1 (2026-10-29) — Vishona's first request, via #crypto-book

- **Context cells: hide/show + adjustable width.** Fear & Greed and
  BTC dominance were fixed 112 px chrome, always shown. Now:
  Settings gains a "Context cells" panel with a show/hide toggle
  per cell (`showFearGreed` / `showBtcDominance` in state,
  persisted; both default ON, so an upgraded bar looks unchanged),
  and each visible cell gets a draggable divider on its inner
  side — the same CellSplitter + `adjustPairWidths` machinery as
  the coin cells — storing a per-cell extent (`fngCellWidth` /
  `domCellWidth`, 0 = the default 112 px / 66 px row) that
  persists in settings.json and rides presets (shared
  toVar/fromVar). One divider pairs the last coin cell with the
  nearest context cell, the other pairs the two context cells;
  the coin side keeps its solver floor. Hidden cells take no
  space in either orientation, and `minimumBarWidth()` counts
  only visible context cells at their effective widths. In
  vertical mode the stored width doubles as the row height,
  exactly like the coin cells' widths.
- Proof: core suite 426 → 437 checks, 0 failures (11 new checks
  pin the new state defaults, the context extent
  defaults/clamps in `contextCellExtent`, and the pair-drag math
  at the context floor). The JUCE app layer cannot be compiled on
  this VM — its proof is the push's GitHub Actions Windows build,
  as with prior rounds. Released as v0.3.1: version bumped in the
  same spots the v0.3.0 bump touched and tagged; the tag's
  Actions run builds and publishes the release. (Milo ships
  Vishona's bar changes per Jackson's 2026-10-29 standing
  instruction.)

## v0.2.1 (2026-10-29, later the same night — Jackson's follow-up: TickerBar gets a working release)

- **Missing-release root cause found and fixed.** The v0.2.0
  Actions run (40843816422) failed at step 5 "Build Release" on a
  compile error in the toast feature — full text in the Errors
  section below. `ToastNotifier.cpp` called
  `LoadIconW(nullptr, IDI_INFORMATION)`; this TU defines no
  UNICODE, so `IDI_INFORMATION` resolves to the ANSI macro (LPSTR)
  and MSVC rejects it for the wide API. The file's discipline is
  explicit-W everywhere else; the fix follows it:
  `LoadIconW(nullptr, MAKEINTRESOURCEW(32513))` (32513 is
  IDI_INFORMATION's resource id), with the reason recorded in a
  comment at the call. The rest of TickerBar's Windows sources were
  scanned for the same pattern — no other occurrence.
- This release also carries the shared-core fixes that landed on
  main earlier tonight (section below): group-dock cross-axis
  alignment and the background-sync hardening.
- Version stamped 0.2.1 in root/app CMake, `Main.cpp`, the Inno
  fallback, the window title and the About note; annotated tag
  `v0.2.1` triggers the release workflow. Proof available here:
  core suite 343 checks, 0 failures; `MainComponent.cpp`
  syntax-checked clean. The Windows TU itself cannot be compiled
  on this VM (no MinGW) — the tag's CI run is its proof, as it was
  (in the negative) for v0.2.0.

## Landed on main 2026-10-29 (SessionBar v0.2.1 fix round — first landed main-only, then released as v0.2.1 above per Jackson's follow-up)

Shared-core corrections from Jackson's SessionBar field test, applied
identically in all three repos:

- **Group-dock cross-axis alignment:** `groupDockRect` moved a
  stacked follower along the dock axis only, so a bar stacked under
  a sideways-moved peer kept its own x and no longer sat under it.
  Stacking now aligns left edges (top/bottom stacks) / top edges
  (left/right stacks). New core checks pin it.
- **Background sync hardening:** the publish/adopt decisions moved
  into the pure core (`BgSyncState`, `bgIsLocalChange`,
  `bgAdoption`) with tests, and the app client was rewired onto
  them. Defects fixed from the v0.2.0 client: a follower echoing an
  adopted background back as its own change (bgSeq bump), the
  settings feed force-publishing (bgSeq bump) on every interaction
  whether or not the background changed, and adoption being
  postponed indefinitely behind a live peer drag. Core suite:
  320 → 343 checks, 0 failures. The live two-bar field test is
  still the end-to-end proof (Link must be ON in both bars).

## v0.2.0 (2026-10-29)

- (a) **% change truncation at small widths — DONE.** Root cause: the
  symbol took a fixed 64 px and the % got the remainder. Fix: pure-core
  layout rule (`core/include/tickerbar/Layout.hpp`) — the % text is
  measured and reserved in full first; the symbol clips instead. The
  window enforces a minimum width built from `minCoinCellWidth()` per
  watched cell (worst-case % strings at the render fonts). Proof: new
  core checks sweep content widths 40–200 px asserting the % is fully
  visible whenever the row can hold it, plus minimum-width checks.
- (b) **Reserve/dock instability — DONE.** The 4 Hz timer-polled sync
  is gone. MeterBar's event-driven `ReserveMachine` is ported into the
  pure core (all four edges, dock ≤2 px / release >8 px hysteresis,
  transitions only), fed by MainWindow moved/resized events;
  ScreenSpace idempotency now compares against the REQUESTED rect, not
  the system-trimmed granted rect. The **Dock** control moved onto the
  bar (cycles Top/Bottom/Left/Right + snaps); Settings keeps the same
  control. Proof: core checks for Register/None-in-dead-zone/Update/
  Release/Switch/strip-rects; app TUs syntax-checked. Live Windows
  behaviour is field-test item 9 in README_TEST.md.
- (c) **Detail candlesticks + volume + 24h/7d/30d — DONE.** Detail card
  draws real candlesticks (wick + body) over volume bars, with radio
  range buttons and a 1 s refresh against the client cache. Feed
  limits (stated in the card, the build log and README_TEST): Coinbase
  caps one candles request at 300 — 24h/7d hourly, 30d daily.
  CoinGecko (HYPE) is hourly closes only: flat ticks, no volume bars,
  30d reported unavailable in words — never fabricated.
- (d) **Fear & Greed click-through — DONE.** Clicking the F&G cell
  (not dragging) opens https://alternative.me/crypto/fear-and-greed-index/
  in the default browser.
- (e) **1h % next to 24h % — DONE.** Derived in the pure core from the
  hourly candles already fetched (`changePct1h`: last close vs the
  candle one hour earlier; no value shown when the series doesn't
  reach back a full hour). Shown on the price row of each cell and as
  a row in the detail card. Zero new API calls. Proof: core checks
  (exact 1h math, insufficient-history cases).
- (f) **Windows toast notifications on alert crossings — DONE.**
  `ToastNotifier` (new app TU): Shell_NotifyIcon balloon — the
  supported route for an unpackaged exe without a registered
  AppUserModelID; Windows renders it as a toast-style banner. Toggle
  in Settings → Alerts, default ON (Jackson's request). Text is built
  by the core (`alertNotificationText`) and unit-tested. Non-Windows
  builds compile the honest stub.
- **Release workflow — DONE.** `.github/workflows/build-windows.yml`
  (adapted from MeterBar's): on a `v*` tag it builds the app, runs the
  core suite, compiles the Inno installer and attaches
  `TickerBar-Setup.exe` to that tag's GitHub Release; it refuses to
  release when the tag doesn't match the CMakeLists version. No tag
  has been created by this work — tagging stays with Jackson/Milo.
  `TickerBar.iss` gained MeterBar's `#ifndef MyAppVersion` guard so
  the CI `/DMyAppVersion` override works.
- **README parity + theme parity — DONE.** README.md restructured to
  MeterBar's format (centred header, build badge, Features / Install /
  Build from source / Gallery / The family). Gallery ships as a
  screenshots-wanted list only — no Windows captures of TickerBar
  exist yet, and no other product's images are used (standing gallery
  honesty rule). Theme: TickerBar's default is the family's Midnight
  Kids palette — verified value-for-value identical to MeterBar's
  default theme (bg/panel/accent/text/good/warn/bad/border and the
  styled-background gradient stops).
- (j) **UI parity (family movement/docking/sizing/settings) — PARTIAL.**
  Landed in this release: event-driven reserve + four-edge docking +
  on-bar Dock control + minimum-width sizing floor (the MeterBar
  conventions). Remaining differences (settings window layout details)
  are OPEN.
- (k) **Inter-app linking — OPEN.** Not landed in v0.2.0. The state
  field (`linkEnabled`, default OFF) and its persistence are in place;
  the shared-memory link component, snap/drag-follow and shared
  background are still to be built across all three apps together.

## Errors recorded during this work

- Linux full-app CMake build could not run in this environment: the
  nested juceaide build fails with
  `fatal error: X11/extensions/Xrandr.h: No such file or directory`
  (the VM lacks libxrandr-dev). App translation units were verified
  with `g++ -fsyntax-only` against the JUCE 8.0.4 headers instead —
  all clean. The Windows CI workflow compiles the real thing.
- MinGW is not installed on this VM this session, so the previous
  releases' MinGW `-fsyntax-only` proof for the Windows-only TUs
  (ScreenSpace/ToastNotifier) could not be re-run; the `_WIN32`
  branches are unchanged in structure from v0.1.1's proven ScreenSpace
  code, and CI builds them on windows-latest.
- **Missing Releases section investigated (2026-10-29, Jackson's
  field report).** `.github/workflows/build-windows.yml` was diffed
  line-by-line against SessionBar's (which produced a release) and
  MeterBar's: the files are equivalent — every difference is a
  correct per-app substitution (project name in the version regex,
  `TICKERBAR_BUILD_APP`, the `TickerBar_artefacts` exe path,
  `TickerBar.iss`, `TickerBar-Setup.exe`). Supporting files check
  out too: the CMake option and target exist, the .iss
  `vc_redist` references are `#if FileExists`-guarded in all three
  repos, and tag v0.2.0 points at the same commit as main did.
  **Verdict: no defect found in the workflow or build files.** If
  TickerBar's v0.2.0 release is absent, the failure is inside the
  Actions run itself (a Windows compile error in app code is the
  standing suspect — the VM can only syntax-check app units, not
  build them), which is visible only in the repo's Actions tab,
  unreadable from here. No re-tag was made on speculation.
- **ROOT CAUSE (confirmed later the same night from the actual
  Actions log, run 40843816422, read via a read-only token):** the
  standing suspect above was exactly right. The workflow ran fine;
  step 5 "Build Release" failed with:

      app/Source/ToastNotifier.cpp(69,21): error C2664:
      'HICON LoadIconW(HINSTANCE,LPCWSTR)': cannot convert
      argument 2 from 'LPSTR' to 'LPCWSTR'

  The toast feature (added in v0.2.0) passed `IDI_INFORMATION` —
  an ANSI LPSTR in that TU because it defines no UNICODE — to the
  explicitly-wide `LoadIconW`. The VM's g++ syntax pass could not
  model the MSVC macro resolution, and MinGW is not installed, so
  the Windows-only TU shipped unchecked; MSVC caught it in CI and
  no release was produced. Fixed in v0.2.1 (see above):
  `MAKEINTRESOURCEW(32513)`.
