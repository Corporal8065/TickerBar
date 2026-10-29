# TickerBar — REQUESTS (per-item status ledger)

Verification rule (Jackson, 2026-10-29): every item is DONE only with
proof — the full core suite green after the change, new checks for new
behaviour, and errors recorded here with their actual text, never hidden.

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
