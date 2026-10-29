# TickerBar — v0.1.1 test kit

A borderless always-on-top crypto ticker bar for Windows — BTC / ETH / HYPE by
default, any Coinbase USD pair addable. Prices are **spot prices**: Coinbase
Exchange's public API for Coinbase-listed coins, CoinGecko for HYPE. No
account, no keys. Midnight Kids theme family (same as MeterBar), Midnight Kids
background by default.

**What changed in v0.1.1 (the "no connection" fix):** v0.1 pulled prices from
the Hyperliquid API, which **geo-blocks US IP addresses** — the bar verified
fine from the build machine (its network isn't US-geolocated) but showed a
permanent "no connection" on a US PC. v0.1.1 swaps the price sources to
Coinbase + CoinGecko, both US-reachable. Everything else — UI, theme, alerts,
Reserve, stale honesty — is unchanged. Two consequences: prices are now
**spot**, not perp marks; and HYPE refreshes about **once a minute**
(CoinGecko's free tier) instead of every 20 s, with its chart drawn from
CoinGecko's hourly sparkline. HYPE's detail card shows **market cap** instead
of a funding rate — spot has no funding, and the app never invents one.

At the right end of the bar sit two **market-context cells**: the **Crypto
Fear & Greed index** (value + label, e.g. "51 · Neutral", from
Alternative.me's free endpoint) and **BTC dominance** (BTC's share of total
crypto market cap, from CoinGecko's free global endpoint). They poll on their
own slow cadence — **every ~5 minutes**, completely separate from the price
poll — and follow the same honesty rules: if a context source fails or
rate-limits, its cell greys out with the last real value and a STALE tag.
Context data can never block or delay the prices.

No exe exists yet — the one-click build below makes it (the proven MeterBar
v0.15–v0.18 flow).

## 1. First: build it (about 5 minutes, one time)

You need Visual Studio 2022 or 2026 with the **Desktop development with C++**
workload (the same setup as MeterBar's test builds). Then:

1. Unzip this kit somewhere normal (Desktop is fine).
2. Open the **tickerbar-app** folder.
3. Double-click **build.bat**. Keep it open — it finds CMake by itself
   (PATH, standalone installs, vswhere, manual scan), configures (the first
   run downloads JUCE 8 — needs internet), builds Release, and runs the core
   unit tests at the end.
4. When it says DONE, double-click
   `build-app\app\TickerBar_artefacts\Release\TickerBar.exe`.

**SmartScreen / unsigned-app note:** the exe is unsigned free software, so
Windows may show a blue "Windows protected your PC" prompt. Click **More
info → Run anyway**. Same for the optional installer later.

Everything the build checked is in `build-log.txt`. If anything fails, send
Milo that file.

**Upgrading from the v0.1 kit:** your settings carry over — the settings
file format is unchanged, and a v0.1 watchlist (BTC/ETH/HYPE or anything you
added that's also a Coinbase USD pair) loads as-is.

## 2. Field tests — what to click and what you should see

1. **First launch.** A short "Made with JUCE" splash (~1.5 s), then the bar
   appears **borderless and always-on-top**, Midnight Kids dark with pink
   accents: BTC, ETH, HYPE cells, each with a price, a green/red 24h %,
   and a 24h sparkline once the first candle fetch lands (a few seconds).
   The status readout at the left says **live** with a green dot.
   If it opens somewhere odd (first run on a new PC), just drag it.
2. **Drag + it remembers.** Drag the bar anywhere by an empty part of its
   background (or by a cell — a click under 5 px of movement counts as a
   click, a drag moves the bar). Close it (the **X** at the right end),
   relaunch: it comes back in the same place, same size.
3. **Click a coin.** Click BTC: a detail card opens — spot price, 24h %,
   24h high/low, 24h volume, and a larger 24h chart. It says "spot price
   (Coinbase)" and shows an as-of time. Click HYPE instead: the card says
   "spot price (CoinGecko)" and shows a **Market cap** row (no funding row —
   spot markets don't have one). Esc or click elsewhere closes it.
4. **Detail alert editors.** In the detail card, put an **Above** level a
   little above the current price (e.g. +1%) and hit **Set alerts**. The
   level is saved; if price later crosses it, the BTC cell **flashes**
   (alternating pink/amber border) for about 12 seconds, once — it re-arms
   only after price returns below the level.
5. **Settings — watchlist.** Gear button → **Watchlist** panel. Add `SOL`:
   it appears on the bar and starts polling. Try adding `NOTACOIN`: it is
   rejected with "NOTACOIN is not a supported symbol (Coinbase USD pairs +
   HYPE)." Remove HYPE: it's gone from the bar. Restart the app: your
   watchlist persisted.
6. **Settings — alert sound.** Alerts panel: the "Soft click sound" toggle
   is **OFF by default**. Turn it on, set a level just across the current
   price, wait for a cross: the cell flashes **and** a soft click plays.
7. **Appearance.** Settings → Appearance: switch Background style between
   **Midnight Kids** / **Clean Dark** / **Flat**, and drag Background
   opacity down — the desktop shows through the bar's background (cell
   panels stay readable).
8. **Stale honesty (kill the network).** Turn off Wi-Fi / pull the cable
   for ~1–2 minutes. The dot turns **amber**, the readout says "stale Ns",
   cells grey out, get a **STALE** tag, and keep the **last real prices** —
   numbers never freeze-fresh or disappear. Reconnect: within a poll cycle
   or two the dot is green and prices update. (Backoff: it retries at
   5/10/20/40 s, capped 60 s.) Note HYPE ages on its own slower clock: it
   only goes STALE after ~3 minutes without a CoinGecko update, because it
   only polls once a minute — and a CoinGecko hiccup never greys out the
   Coinbase coins.
9. **Dock + Reserve screen space.** Drag the bar flush against the **top**
   edge of a monitor. Click **Reserve** (or Settings → Window). Maximise
   another window on that monitor: Windows now stops it at the bar — the
   bar owns that strip. Turn Reserve off: maximised windows go full-height
   again. Settings → Window → **Dock edge** switches Top/Bottom; the bar
   snaps to the chosen edge and Reserve follows it.
10. **Arrange / Restore.** Settings → Window → **Arrange windows clear of
    the bar**: every non-maximised window on the bar's monitor slides out
    of the bar's band. **Restore arranged windows** puts them back.
11. **Poll interval.** Settings → Window → Poll interval: 10/20/30/60 s.
    Default 20 s. (This is the Coinbase cadence; HYPE stays ~60 s.)
12. **Context cells.** The two right-hand cells show **FEAR & GREED**
    (number + word label, tinted red → amber → green by band) and **BTC
    DOMINANCE** (a percent). They fill in a few seconds after launch
    (they say "loading..." until the first fetch lands) and refresh about
    every 5 minutes — the Fear & Greed index itself only updates about
    daily. Hover either cell for its source. They are independent of the
    prices: killing the network greys them on their own slower clock
    (~15 minutes), while the price cells go stale in ~1 minute.

## No internet in the VM where this was built for you? No problem

The build ran on a Linux VM: the live APIs were verified there with curl
(real fixtures are in `tests/fixtures/` and the unit tests parse them), and
the Windows AppBar code was syntax-checked with MinGW — but the **live
Windows behaviour** (drag feel, Reserve, Arrange) is exactly what the field
tests above are for.

## Giving it to a friend

Run **build-release.bat** (same one-click style). It builds Release and
stages `dist\TickerBar.exe`. If **Inno Setup 6** is installed (free,
https://jrsoftware.org/isdl.php), it also compiles `dist\TickerBar-Setup.exe`
— the single file to send. Their first run shows the SmartScreen prompt:
**More info → Run anyway**.

## What's inside

- `app/Source/` — the JUCE app: bar UI, detail card, settings window,
  Coinbase/CoinGecko poller, ScreenSpace (AppBar) service ported from MeterBar.
- `core/include/tickerbar/` — dependency-free core (JSON reader, Coinbase +
  CoinGecko parsing, price formatting, alert-cross logic, stale/feed rules,
  window arrange math). Unit-tested without JUCE.
- `tests/` — `test_core.cpp` (186 checks: formatting/precision, 24h-change
  math, alert crossing, parsing against live + deliberately-broken fixtures
  for Coinbase, CoinGecko **and** both context sources, stale thresholds)
  and `fixtures/` (real payloads captured 2026-10-29 from Coinbase and
  CoinGecko, 2026-10-28 from Alternative.me, + malformed variants).

## Known v0.1.1 limits (honest list)

- Prices are **spot**, not perp marks (changed in v0.1.1 with the source
  swap) — labelled everywhere it matters.
- HYPE refreshes ~once a minute (CoinGecko free tier), and its chart is the
  trailing 24h of CoinGecko's hourly sparkline series — hourly closes only.
- HYPE has no funding rate (it's a spot price); its detail card shows
  market cap instead. Nothing is fabricated to fill the gap.
- Watchlist symbols must be Coinbase USD pairs or HYPE — a coin that only
  trades elsewhere can't be added.
- Alerts fire only while the app is running; no push/background service.
- Detail card is a snapshot at open (it doesn't live-update; the bar does).
- Reserve/Arrange need the bar docked flush at its edge; multi-monitor
  mixed-DPI is best-effort (per-monitor physical pixels, same as MeterBar).
- First candle (sparkline) fetch can lag the first price by one cycle.
