# Code signing policy — TickerBar

*DRAFT — to be published in the TickerBar repository (e.g. as
`CODE_SIGNING_POLICY.md`) and linked from the README's "Code signing policy"
section before the SignPath subscription is used. Status until SignPath
approves the project: **code signing is not active yet** — current releases
are unsigned, so Windows SmartScreen may warn about an unknown publisher.*

## What is signed

Only the Windows release files of TickerBar:

- the release installer, `TickerBar-Setup.exe`, and
- the application executable inside it, `TickerBar.exe`.

Both are built **only** by the GitHub Actions workflow in this repository
(`.github/workflows/build-windows.yml`, Windows runner, from a version tag),
from the source code in this public repository. Nothing built on a personal
machine is signed or released. Development builds are unsigned.

Third-party components inside the installer keep their own signatures or
remain unsigned, as applicable: the installer is produced with Inno Setup,
and TickerBar is built with the JUCE framework (see "Upstream components"
below).

## Who signs

Free code signing provided by SignPath.io, certificate by SignPath Foundation.

Releases are signed under a certificate issued to **SignPath Foundation**, so
Windows will show **"SignPath Foundation"** as the publisher of signed
TickerBar files. That is expected and correct. The signing key is held by
SignPath Foundation on its HSM and is never present on any project machine.

## Build and origin verification

Every signing request is submitted by this repository's GitHub Actions
workflow for a build of this repository. SignPath verifies that the signed
binary originates from this official repository and the exact commit that was
built. Every release's signing request is **approved manually** by the
project's approver (below) before SignPath signs it.

## Team roles

TickerBar is a one-person project.

- **Committers and reviewers:** Jackson (GitHub
  [Corporal8065](https://github.com/Corporal8065)) — sole committer; also
  reviews any external pull requests himself before merge.
- **Approvers:** Jackson (GitHub
  [Corporal8065](https://github.com/Corporal8065)) — the only person who can
  approve a signing request.

All team members (just Jackson) use multi-factor authentication on both
GitHub and SignPath.

## Upstream components

- **JUCE** (https://juce.com) — the application framework, fetched by CMake
  from the official JUCE GitHub release (8.0.4) during the public CI build.
  JUCE modules are dual-licensed (AGPLv3 / commercial JUCE licence); this
  project uses JUCE under its open-source (AGPLv3) licence option, and
  TickerBar's own code is licensed GPL-3.0. JUCE's binaries are upstream
  open-source components included in the signed installer; they are not
  signed with this project's SignPath subscription.
- **Inno Setup** — builds the installer from this repository's installer
  script.
- Windows system libraries are included as System Libraries.

## Privacy

This program will not transfer any information to other networked systems
unless specifically requested by the user or the person installing or
operating it.

TickerBar's purpose requires it to fetch public market data, so — beyond the
sentence above, which covers user data (TickerBar collects and transfers
**no** user data, has no accounts and no API keys, and sends no personal
data anywhere) — the complete picture is: to display prices it makes public,
read-only HTTPS requests, which necessarily expose the user's IP address to
those services as any web request does:

- the **Coinbase Exchange public API** (`api.exchange.coinbase.com`) —
  prices, candles and the product list for the coins on the user's watchlist
  (Coinbase privacy policy: https://www.coinbase.com/legal/privacy);
- **CoinGecko** (`api.coingecko.com`) — HYPE prices and global market data /
  BTC dominance (https://www.coingecko.com/en/privacy);
- **alternative.me** (`api.alternative.me`) — the Crypto Fear & Greed index
  (https://alternative.me/privacy/).

It also queries its self-updater endpoint,
`api.github.com/repos/Corporal8065/TickerBar/releases/latest`, on launch and
on menu request, and downloads a new installer from GitHub only when the user
chooses to update (verified by size and SHA-256 before installing). GitHub's
own privacy policy applies to those requests
(https://docs.github.com/en/site-policy/privacy-policies/github-general-privacy-statement).
