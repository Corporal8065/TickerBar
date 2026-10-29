# TickerBar — REQUESTS (per-item status ledger)

Verification rule (Jackson, 2026-10-29): every item is DONE only with
proof — the full core suite green after the change, new checks for new
behaviour, and errors recorded here with their actual text, never hidden.

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
