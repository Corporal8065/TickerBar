![TickerBar](docs/images/social-preview.png)

# TickerBar

**A professional crypto ticker bar for Windows** — live spot prices, 24h change and sparklines for BTC, ETH, HYPE and your own watchlist, in one slim strip that hugs the edge of your screen.

TickerBar is a borderless, always-on-top bar that lives at the edge of your monitor and keeps your coins in sight while you do anything else — trade, work, game, watch a chart on the other screen. No browser tab to babysit, no phone to pick up: the prices are just *there*, updating every 20 seconds.

## What it does

### Live prices, honestly sourced

- Prices are **real spot prices** from the **Coinbase Exchange public API** — no account, no API key, nothing to sign up for. HYPE comes from **CoinGecko**, which tracks it natively.
- Every price in the bar is labelled by its source, so you always know exactly what you're looking at.
- The default watchlist is **BTC · ETH · HYPE**. Add any other coin with a live Coinbase USD market — new symbols are checked against the real product list, so typos and delisted coins are rejected with a message instead of silently showing nothing. The list can never be emptied by accident.

### Every cell tells the story at a glance

Each coin gets a cell showing:

- **Price**, formatted to the precision the coin actually needs
- **24h change %**, green or red
- A **24h sparkline** of hourly closes — the shape of the day in one look

**Click any cell** and it opens a detail card: 24h high and low, 24h volume, a larger 24h chart, and that coin's alert settings (HYPE's card shows market cap in place of the rows spot markets don't have — nothing is ever fabricated to fill a gap).

### Market context, not just prices

Two extra cells sit alongside your coins:

- **Crypto Fear & Greed index** — value and label, so you know the mood of the market, not just its prices
- **BTC dominance %** — how much of the total crypto market is Bitcoin right now

They refresh on their own slower cadence (about every 5 minutes), completely separate from the price feed, so a hiccup in one never freezes the other.

### Price alerts

Set an **above** and a **below** level per coin. When a price genuinely crosses one, that cell flashes for ~12 seconds — a first reading never fires, and the alert re-arms only after the price comes back, so it can't spam you. An optional soft click sound exists but is **off by default**.

### Honest when the feed isn't

TickerBar never shows you a stale price pretending to be live. A status dot and readout on the bar say **Live / Stale / Down**; when a feed stalls, that coin's cell greys out, keeps its last real price, and is marked STALE until fresh data lands. Staleness is tracked **per coin** — one source having a bad minute never greys out the others.

### It docks — and reserves its space

Like its sibling MeterBar, TickerBar can dock to a screen edge, and **Reserve** makes Windows treat it as part of the desktop frame: maximised windows stop at the bar instead of sliding underneath it. **Arrange** slides floating windows clear of the bar; **Restore** puts them back. Background opacity is adjustable (15–100%), and the bar remembers its position, size and settings across restarts.

## Install

Installers are published on the [Releases](https://github.com/Corporal8065/TickerBar/releases) page — the first release is **v0.1.1**. Download `TickerBar-Setup.exe`, run it, set your watchlist, done.

**SmartScreen note:** TickerBar is an unsigned independent app, so Windows may show *"Windows protected your PC"* the first time. Click **More info → Run anyway**. Release installers are built from this source tree by the GitHub Actions workflow in this repository.

## Build from source

You need **Visual Studio 2022** with the *"Desktop development with C++"* workload:

- **`build.bat`** — builds the app for testing.
- **`build-release.bat`** — builds the shareable installer (needs [Inno Setup 6](https://jrsoftware.org/isdl.php)).

Built with [JUCE](https://juce.com); market-data parsing lives in a separate, unit-tested core so a malformed feed can never take the bar down with it.

## The family

TickerBar is one of a small family of edge-of-screen bars — one app, one repo each:

- **MeterBar** — a professional audio meter bar (spectrum, waveform, loudness, stereometer, oscilloscope).
- **SessionBar** — market-session clocks and countdowns (New York / London / Tokyo).
