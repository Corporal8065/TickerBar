# TickerBar — SPEC

## v0.1.1 (2026-10-29) — data-source swap (the "no connection" fix)

Identical app to v0.1 except the price sources. **Root cause of the
v0.1 field failure:** the Hyperliquid public API geo-blocks US IP
addresses; v0.1 verified fine from the build VM (egress not
US-geolocated) but showed a permanent "no connection" on Jackson's
US PC — and would on any US machine. The Hyperliquid client is
removed, not left selectable.

### Data (v0.1.1)
- **Coinbase Exchange public API**, no key, read-only, US-reachable
  (`https://api.exchange.coinbase.com`):
  - `GET /products/{SYM}-USD/stats` → last price (spot), open (the
    24h-change reference), 24h high/low, 24h volume (coin units,
    stored as USD × last). Polled every **20 s** (setting 10/20/30/60).
  - `GET /products/{SYM}-USD/candles?granularity=3600` (trailing 24h)
    → sparkline + chart, throttled to ~4 min per symbol.
  - `GET /products` → the watchlist validation universe (bases with a
    live SYM-USD product; delisted/non-USD excluded), refreshed ~30 min.
- **CoinGecko** (keyless) for HYPE, pinned by design (even though
  Coinbase also lists HYPE-USD): `GET /coins/markets?ids=hyperliquid
  &sparkline=true` → spot price, 24h % (open24h derived from the
  source's own %), 24h high/low, 24h USD volume, **market cap**,
  hourly sparkline (trailing 24h of the 7d series = the chart).
  Polled **~60 s**; 429s absorbed as staleness for HYPE only.
- Prices are **SPOT** (v0.1 was perp marks) — labelled in the bar
  tooltip, the Settings About panel, and the detail card header
  ("spot price (Coinbase)" / "spot price (CoinGecko)").
- **No funding figure exists on spot sources** — the detail card's
  funding row is gone; CoinGecko rows show Market cap instead.
  Nothing is fabricated to fill the gap.
- Watchlist: default **BTC, ETH, HYPE** (unchanged). Add validates
  against the live Coinbase product list + HYPE; unknown symbols
  rejected with a message; never empty; **the v0.1 settings file
  loads unchanged** (same JSON, same keys).
- Staleness is **per symbol** with the family's 3×-cadence rule:
  Coinbase symbols go stale at 60 s, CoinGecko symbols at 180 s, so
  a healthy HYPE never flickers STALE between its 60 s polls and a
  CoinGecko outage never greys the Coinbase cells. Feed Down/Stale/
  Live, backoff (5/10/20/40→60 s), and whole-payload rejection are
  unchanged from v0.1.
- Market-context cells (Fear & Greed + BTC dominance) **unchanged**.

### Architecture (v0.1.1 deltas)
- `core/include/tickerbar/MarketParse.hpp` replaces `HlParse.hpp`:
  Coinbase stats/candles/products parsers, CoinGecko markets parser,
  source routing (`sourceForSymbol`, `coinGeckoIdForSymbol`), per-symbol
  stale thresholds. Same strict, fixture-tested discipline.
- `app/Source/PriceClient` replaces `HlClient` (same thread shape,
  per-symbol success timestamps).

## v0.1 (2026-10-28) — crypto ticker bar, requested by Vishona via Jackson

A borderless, always-on-top Windows desktop bar showing live crypto prices,
in MeterBar's theme family (Midnight Kids default). More utility than the
bare ask (a BTC/ETH/HYPE ticker): detail view, watchlist editing, price
alerts, dock + reserve-screen-space, honest stale states.

### Data
- Source: **Hyperliquid public API**, no key, read-only:
  `POST https://api.hyperliquid.xyz/info`
  - `metaAndAssetCtxs` → per-asset mark price, prev-day price (24h change),
    24h notional volume, hourly funding.
  - `candleSnapshot` (interval 1h, trailing 24h) → sparkline + 24h high/low.
- Poll: prices every **20 s** (setting: 10/20/30/60); candles throttled to
  once per 60 s per symbol.
- **Prices are perpetual MARK prices, not spot** — labelled in the bar
  tooltip, the Settings About panel, and the detail card header.
- Failure honesty: feed status = Down (never had data) / Live (≤60 s old) /
  Stale (>60 s). Stale cells grey out, keep last real prices, show STALE.
  Retry with 5/10/20/40 s backoff (cap 60). Malformed payloads are rejected
  whole (never partial), never crash — fixture-tested.
- **Market context (Jackson's addition, same v0.1):** two extra cells —
  Crypto Fear & Greed index (value + label) from Alternative.me
  (`api.alternative.me/fng/?limit=1`) and BTC dominance % from CoinGecko
  (`api.coingecko.com/api/v3/global`). Own thread, ~5 min cadence (60 s
  retry after a failure), fully decoupled from the price poll. Stale rule
  is the same 3×-cadence shape: context goes stale at >900 s. Both
  parsers are strict + fixture-tested like the Hyperliquid one, including
  a captured CoinGecko rate-limit body.

### UI
- Borderless bar, drag to move (drag from background or cells; <5 px
  movement = click), remembers position/size/settings across restarts.
- Default watchlist **BTC, ETH, HYPE**. Each cell: symbol, price (2 dp ≥$1,
  4 dp ≥$0.01, else 6 dp, thousands-grouped), 24h % green/red, 24h
  sparkline of hourly closes.
- Status: dot (green live / amber stale / red down) + text readout.
- Click a cell → detail card: mark, 24h %, 24h high/low (from candles),
  24h volume, hourly funding, larger 24h chart, alert editors.
- Settings window (MeterBar idiom): Watchlist (add validated against the
  live perp universe — unknown symbols rejected with a message; remove;
  never empty), Alerts (per-symbol above/below + sound toggle), Appearance
  (background style + opacity), Window (pin, reserve, dock edge, poll,
  Arrange/Restore), About.
- Theme: **Midnight Kids** palette copied from MeterBar's Theme.h
  (bg `#17141F`, accent `#E04C89`, accent2 `#40AAC0`, good `#2FD68A`,
  bad `#FE5A3C`); background styles Clean Dark / Midnight Kids / Flat;
  **background opacity** 15–100 % over a per-pixel transparent window
  (MeterBar v0.13 pattern).

### Alerts
- Per symbol: one above-level and one below-level (NaN = unset), persisted.
- Crossing = pure state machine in core (`AlertTracker`): first tick primes
  (never fires), fires once on a true cross, re-arms after price returns.
  On a cross the cell flashes ~12 s; optional soft click (synthesised,
  **default OFF**).

### Screen space (port of MeterBar v0.18's ScreenSpace)
- **Reserve** (AppBar): while ON and the bar is docked flush at its
  monitor edge (top or bottom), Windows shrinks that monitor's work area.
  Default OFF. Toggle on the bar + in Settings.
- **Arrange/Restore**: slide other (non-shell, visible, non-minimised/
  maximised/cloaked, non-tool) windows on the bar's monitor clear of the
  bar's band, fits first then moves; Restore returns them.
- Same `_WIN32`-guard discipline (MeterBar v0.14 lesson: the guard must be
  `_WIN32`, and the TU's Windows content is proven by a MinGW syntax check
  + A/B preprocess grep, not by hope).

### Architecture
- `core/include/tickerbar/` — std-only headers: `Json` (hand-rolled parser),
  `Model`, `HlParse`, `Format`, `Alerts`, `Stale`, `Arrange`, `State`.
  Zero JUCE — unit-tested headless and reused by the app.
- `app/Source/` — JUCE 8 app: `MainComponent` (bar, cells, detail,
  settings), `HlClient` (polling thread), `SettingsStore` (JSON settings at
  `%APPDATA%/TickerBar/settings.json` — atomic via temp-file replace),
  `ScreenSpace` (Windows AppBar/arrange), `ClickPlayer`, `Theme`.
- Settings defaults pinned in core `State.hpp` and asserted by tests.

### Engineering rules (MeterBar v0.15–v0.18 lineage)
- Unit tests green (0 failures) — formatting/precision, 24h-change math,
  alert crossing, JSON parsing vs live + malformed fixtures, stale rules.
- Clean rebuild in tree **and** from the shipped zip in a scratch dir.
- Xvfb smoke run: starts and renders placeholder/stale state with no
  network dependency, no crash.
- `.bat` files ASCII + CRLF, goto-label flow; build-release keeps the
  plain `dist\TickerBar.exe` fallback when Inno Setup is absent.
- Deliverable: `~/workspace/your_files/tickerbar-test-kit-v0.1.zip`,
  README leading with field tests + SmartScreen note; BUILD_LOG entry
  with verification evidence.
