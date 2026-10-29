# TickerBar — BUILD LOG

## v0.2.0 (2026-10-29) — field-test fixes + detail charts + family release flow

Built from Jackson's 2026-10-29 field test (his screenshot showed both
P1 bugs live) and his same-day feature requests. Per-item proof lives
in REQUESTS.md; summary here.

- **(a) % truncation fixed.** The old cell gave the symbol a fixed
  64 px and the % the leftovers — at narrow widths the % elided to
  "-1....". New pure-core layout rule (`Layout.hpp`): the % string is
  measured at its render font and reserved in full; the symbol takes
  the remainder and is the only text that may clip. The price row uses
  the same rule for the new 1h %. The window now enforces a minimum
  width computed from `minCoinCellWidth()` per watched cell with
  worst-case % strings, so the guarantee holds at every supported
  width — the floor grows with the watchlist.
- **(b) Reserve flicker fixed.** Root cause was the known MeterBar
  v0.18 pattern, still present here: a 4 Hz timer poll re-asserting
  the AppBar, comparing the requested rect against the rect Windows
  GRANTED (post-QUERYPOS, system-trimmed — never equal), tearing the
  reservation down and rebuilding it several times a second while
  maximised windows bounced. Ported MeterBar's fix wholesale: the
  pure-core `ReserveMachine` (transitions only, dock ≤2 px / release
  >8 px hysteresis, all four edges, full-strip requested rects),
  event-fed from MainWindow moved/resized; ScreenSpace idempotency
  compares against the REQUESTED rect. The **Dock** control moved
  onto the bar (button cycles Top/Bottom/Left/Right and snaps);
  `DockEdge` and the Settings combo now carry all four edges, and
  Arrange uses the machine's actual/nearest edge.
- **(c) Detail chart rebuilt.** Candlesticks (wick + body) over
  volume bars, 24h / 7d / 30d radio ranges, 1 s refresh against the
  client cache with throttled on-demand fetches. **Feed limits,
  plainly:** Coinbase returns at most 300 candles per request —
  24h = 24 hourly, 7d = 168 hourly, 30d = 30 DAILY candles (the
  finest honest 30d). CoinGecko (HYPE) carries a 7d hourly sparkline
  of closes only: 24h/7d draw as flat ticks with no volume bars (the
  chart says "no volume from this feed"), and **30d is reported
  unavailable in words — never backfilled or fabricated.**
- **(d)** Clicking the Fear & Greed cell opens
  https://alternative.me/crypto/fear-and-greed-index/ in the default
  browser (click vs drag distinguished by the family's 5 px rule).
- **(e) 1h %** on every cell's price row and in the detail card,
  derived in the core from the already-fetched hourly candles
  (`changePct1h`); nothing shown when the series doesn't reach back
  a full hour. No new API calls.
- **(f) Toast notifications** on alert crossings (Settings → Alerts,
  default ON): `ToastNotifier` uses a Shell_NotifyIcon balloon — the
  supported route for an unpackaged exe with no registered
  AppUserModelID; text built by the core's `alertNotificationText`
  and unit-tested. A TickerBar tray icon exists after the first toast.
- **Release workflow (Jackson's addition):**
  `.github/workflows/build-windows.yml`, adapted from MeterBar's —
  a `v*` tag builds the app, runs the core suite, compiles the Inno
  installer and attaches `TickerBar-Setup.exe` to the tag's GitHub
  Release; the job refuses a tag that doesn't match the CMakeLists
  version. `TickerBar.iss` gained the `#ifndef MyAppVersion` guard
  the CI `/D` override needs. **No tag was created by this work.**
- **README + theme parity (Jackson's additions).** README.md rebuilt
  in MeterBar's structure (centred header, CI badge, Features /
  Install / Build / Gallery / family). The Gallery is a
  screenshots-wanted list ONLY — no Windows captures of TickerBar
  exist yet, and per the standing gallery honesty rule nothing
  borrowed or faked stands in. Theme parity: the default theme is
  the family **Midnight Kids** palette, verified value-for-value
  identical to MeterBar's default (all twelve Theme colours and the
  styled-background gradient stops match exactly).
- **State:** new persisted fields `alertToasts` (default true) and
  `linkEnabled` (default false — the P4 linking state field; the
  linking component itself is OPEN, see REQUESTS.md). Old settings
  files load unchanged (defaults-first).

Kit file: `tickerbar-test-kit-v0.2.0.zip` — same layout as v0.1.1,
plus `REQUESTS.md`, `.github/workflows/build-windows.yml`, the new
core headers (`ReserveState.hpp`, `Layout.hpp`) and
`app/Source/ToastNotifier.{h,cpp}`.

Verified (Linux VM; Windows behaviour is the README field tests' job):
- **Core suite: 307 checks, 0 failures** (was 186) — in tree AND
  rebuilt from the shipped kit contents. New coverage: the ported
  reserve machine (register / dead-zone None / single Update /
  release / edge Switch / toggle-off / strip rects / tall-bar side
  docking), edge-aware arrange (left/right + legacy-wrapper
  agreement), the cell-layout invariant (sweep of widths; minimum-
  width checks), 1h-change math incl. insufficient-history, candle
  range/volume honesty, alert notification text, new state defaults.
- **App translation units syntax-checked clean** against JUCE 8.0.4
  headers (Main, MainComponent, PriceClient, ContextClient,
  ScreenSpace, ToastNotifier). The full CMake app build could not
  run here: the nested juceaide build fails on this VM with
  `X11/extensions/Xrandr.h: No such file or directory` (no
  libxrandr-dev installed) — an environment gap, recorded in
  REQUESTS.md; the new CI workflow builds the app on windows-latest.
  MinGW (previous releases' Windows-TU proof) is not installed on
  this VM this session either; the Windows branches keep v0.1.1's
  proven structure.

Honest limits: live Windows behaviour (reserve stability against
real windows, toast rendering, drag feel) is field-test territory —
README_TEST.md items 9, 12–15 cover the new surface click-by-click.
Linking (P4 item k) is NOT in this release; see REQUESTS.md.

## v0.1.1 (2026-10-29) — data-source swap: the "no connection" fix

**Root cause, plainly:** v0.1 read prices from the Hyperliquid public
API, which **geo-blocks US IP addresses**. Every verification ran from
this VM, whose egress is not US-geolocated, so the feed answered fine
here — and showed a permanent "no connection" on Jackson's PC in
California (and would for Vishona too, if she's in the US). The miss
was in source selection, not in the app: the API was never reachable
from the machines the app is for. The Hyperliquid client and parser
are **removed**, not left as a selectable source.

What replaced them (everything else in the app is byte-identical in
behaviour — UI, theme, alerts, ScreenSpace, settings format):
- **Coinbase Exchange public API** (keyless, US-reachable) for every
  Coinbase-listed coin: `/products/{SYM}-USD/stats` on the existing
  ~20 s poll (last = spot price, open = the 24h-change reference,
  high/low, coin-unit volume stored as USD × last); `/candles`
  (1h, trailing 24h, ~4 min throttle) for the sparkline/chart;
  `/products` for watchlist validation (live SYM-USD bases only —
  delisted and non-USD excluded; 405 bases at capture).
- **CoinGecko `/coins/markets`** for HYPE (pinned by design, though
  Coinbase also lists HYPE-USD): spot price, 24h % (open24h derived
  from the source's own percentage), 24h high/low, USD volume,
  **market cap**, hourly sparkline — polled ~60 s (free-tier limits),
  429s absorbed as staleness for HYPE only.
- Prices are now **SPOT**, and every label says so (bar tooltip, cell
  tooltips, detail header "spot price (Coinbase)"/"(CoinGecko)",
  About). The detail card's **funding row is gone** — spot has no
  funding and none is fabricated; CoinGecko rows show Market cap
  instead. HYPE's chart is the trailing 24h of CoinGecko's 7-day
  hourly sparkline (closes only — no invented candle ranges).
- Staleness went **per symbol** with the family's 3×-cadence rule
  (core `staleAfterSecFor`): Coinbase symbols stale at 60 s,
  CoinGecko symbols at 180 s — a healthy HYPE can't flicker STALE
  between its own 60 s polls, and a CoinGecko outage can't grey the
  Coinbase cells. A v0.1 `settings.json` loads unchanged.

Kit file: `tickerbar-test-kit-v0.1.1.zip` — same layout as v0.1;
`HlParse.hpp` → `MarketParse.hpp`, `HlClient` → `PriceClient`,
Hyperliquid fixtures replaced by Coinbase/CoinGecko captures.

Verified (Linux VM; Windows behaviour is the README field tests' job):
- **Core test suite: 186 checks, 0 failures** (was 145) — in tree
  AND rebuilt from the shipped kit contents. New coverage: Coinbase
  stats vs the live fixtures (BTC last 79,736.11 / open 80,873.62 at
  capture; coin→USD volume conversion), stats rejection (wrong types,
  missing last, zero last, truncated, garbage); Coinbase candles
  (newest-first input reversed to ascending; broken row rejected;
  empty valid); products universe (≥400 bases, BTC/ETH/SOL in,
  delisted ACX out, sorted); routing + per-source stale thresholds;
  CoinGecko markets vs the live HYPE fixture (price 78.54, 24h %
  −3.39382 round-tripping through the derived open24h, market cap
  $18.81B, 168-point sparkline → 25 hourly chart candles, closes
  only); CoinGecko rejection (wrong types, empty page valid, and a
  captured 429 body — the same `{"status":{"error_code":429,...}}`
  body CoinGecko served during v0.1's capture; a fresh 429 could not
  be triggered during this session despite repeated calls, so the
  v0.1 capture was reused for the markets-parser test).
- **Live API evidence (curl, this VM):** Coinbase BTC/ETH/SOL stats,
  BTC 1h candles (30 rows), full /products (845 products), and
  CoinGecko HYPE markets all answered HTTP 200 and are the fixtures.
- **App builds clean on Linux** in tree and from the kit contents
  (`[100%] Built target TickerBar`). Two infrastructure notes: a
  first configure raced a still-running earlier configure in the
  same build dir (orphan killed, dir rebuilt — no source impact);
  and `-j2` OOM-killed a JUCE translation unit on this shared 8 GB
  VM while sibling builds ran — `-j1` is clean, as in v0.1.
- **Xvfb smoke run:** starts headless with no network path (Linux
  JUCE_USE_CURL=0, every fetch fails by construction), renders its
  no-connection state, writes settings.json with exactly the pinned
  defaults (watchlist BTC/ETH/HYPE, winW 1040, reserve false, alert
  sound false, poll 20 s), still running at the 14 s timeout
  (exit 124 = survived, no crash).
- **Windows TU proof (ScreenSpace, unchanged from v0.1):** MinGW
  `-fsyntax-only` clean; A/B preprocess grep reproduces exactly —
  `SHAppBarMessage` ×7 and "appbar active" ×1 under MinGW, ×0 under
  Linux g++.
- `.bat` files byte-checked ASCII + CRLF **inside the final zip**;
  build.bat's version strings bumped to v0.1.1 (goto-flow untouched);
  version bumped everywhere v0.1 put it (CMake project + app,
  JUCE_APPLICATION_VERSION_STRING, Main.cpp, bar version label,
  Inno `MyAppVersion`).

Honest limits: the VM is Linux — live Windows behaviour (drag feel,
Reserve/Arrange against real windows, the alert click's audibility,
SmartScreen flow) is field-test territory, enumerated click-by-click
in README_TEST.md. Prices are spot, not perp marks; HYPE refreshes
~once a minute and its detail chart is hourly closes; watchlist
symbols must be Coinbase USD pairs or HYPE; CoinGecko's free tier
rate-limits shared IPs, which HYPE absorbs as staleness, never as a
price-path fault for the other coins.

## v0.1 (2026-10-28) — first build: ticker bar + market-context cells

New app (sibling of MeterBar; built for Vishona at Jackson's request).
Borderless always-on-top JUCE bar: BTC/ETH/HYPE by default from the
Hyperliquid public API (perp **mark** prices, labelled as such), any HL
perp addable, click-cell detail card, per-symbol price alerts (flash +
optional soft click, default OFF), Midnight Kids theme + Clean Dark /
Flat backgrounds + opacity, top/bottom docking, and MeterBar v0.18's
ScreenSpace service (Reserve AppBar + Arrange/Restore, default OFF)
ported across. Jackson's same-day addition is included in this kit:
**market-context cells** — Fear & Greed (Alternative.me) and BTC
dominance (CoinGecko) on their own thread at a ~5-minute cadence,
decoupled from the 20 s price poll, with the same stale-honesty rules
(core `contextStale`, stale at >900 s ≈ 3 missed cycles).

Kit file: `tickerbar-test-kit-v0.1.zip` — `tickerbar-app/` folder:
CMakeLists (root: core + tests; app behind -DTICKERBAR_BUILD_APP=ON),
SPEC.md, README_TEST.md, CLICK_ME_FIRST.txt, build.bat,
build-release.bat, TickerBar.iss, `core/include/tickerbar/` (std-only:
Json, Model, HlParse, ContextParse, Format, Alerts, Stale, Arrange,
State), `tests/` (test_core.cpp + fixtures: live Hyperliquid /
Alternative.me / CoinGecko payloads captured 2026-10-28 + hand-broken
variants incl. a CoinGecko rate-limit body), `app/Source/` (Main,
MainComponent = bar/cells/detail/settings, HlClient, ContextClient,
SettingsStore JSON, ScreenSpace, ClickPlayer, Theme), `app/icon/`.

Verified (Linux VM; Windows behaviour is the README field tests' job):
- **Core test suite: 145 checks, 0 failures** — JSON reader edge cases;
  Hyperliquid parsing vs the live fixture (235 assets; BTC szDecimals 5,
  ETH 4, HYPE 2; delisted MATIC flagged) and vs truncated / garbage /
  empty / wrong-types / broken-candle-row fixtures (all rejected whole);
  24h-change math; price precision + formatting; **alert crossing state
  machine** (prime-no-fire, cross fires once, re-arm, NaN-safe);
  stale thresholds + backoff table; arrange fit/clamp math (top and
  bottom); pinned v0.1 state defaults; **context parsing** (F&G live
  fixture: value in range, label, timestamp; dominance from the live
  CoinGecko fixture; empty / wrong-types / missing-label / truncated /
  rate-limit bodies all rejected), dominance formatting, F&G band edges,
  context stale rule.
- **Live API evidence (curl, this VM):** Hyperliquid metaAndAssetCtxs +
  BTC 1h candles captured as fixtures (BTC mark 79,868.0 at capture);
  Alternative.me F&G = 51 · Neutral; CoinGecko BTC dominance = 59.46%.
- **App builds clean on Linux** (`[100%] Built target TickerBar`) after
  four JUCE-API fixes (SettableTooltipClient bases, CallOutBox::dismiss,
  ComponentDragger instead of peer positions). One infrastructure note:
  a full-parallel (-j) build OOM-killed cc1plus on this 8 GB VM while
  sibling builds ran; -j1 is clean. Windows/MSVC is unaffected (the
  .bat flow builds at default parallelism on Jackson's machine).
- **Xvfb smoke run:** app starts headless with NO network path (Linux
  build has JUCE_USE_CURL=0, so every fetch fails by construction),
  renders its placeholder/no-connection state, writes settings.json with
  exactly the pinned defaults (watchlist BTC/ETH/HYPE, winW 1040,
  reserve false, alert sound false, poll 20 s), and is still running
  when the 14 s timeout kills it (exit 124 = survived, no crash; only
  ALSA no-sound-hardware warnings on stderr).
- **Windows TU proof (ScreenSpace):** the v0.14 lesson applied —
  MinGW `-fsyntax-only` on ScreenSpace.cpp is clean, and the A/B
  preprocess grep shows the AppBar code present exactly once under
  MinGW (`SHAppBarMessage` ×7, "appbar active" ×1) and zero times under
  Linux g++ — the `_WIN32` body provably compiles IN for Windows and
  OUT elsewhere, never silently the wrong way.
- `.bat` files are the MeterBar-proven scripts adapted (vswhere /
  PATH / standalone / folder-scan CMake discovery, goto-label flow):
  byte-checked ASCII + CRLF. build-release stages `dist\TickerBar.exe`
  unconditionally; the Inno installer (fresh AppId GUID) compiles only
  when ISCC exists.
- **Rebuilt from the shipped zip in a scratch dir:** core suite and the
  full JUCE app both rebuild from the exact zip contents and the suite
  passes there too (145 checks, 0 failures).

Honest limits: the VM is Linux — live Windows behaviour (drag feel,
Reserve/Arrange against real windows, the alert click's audibility,
SmartScreen flow) is field-test territory, enumerated click-by-click in
README_TEST.md. Prices are perp marks, not spot; the F&G index updates
~daily at the source; CoinGecko's free endpoint rate-limits shared IPs,
which the context cell absorbs as staleness, never as a price-path
fault.
