#pragma once
// =====================================================================
//  UPDATER CONFIG — the ONE place that says where MeterBar updates
//  come from. v0.19 skeleton; wired to the real repo in v0.19.2.
//
//  kGitHubRepo is "owner/repository" on GitHub.
//
//  THE REPO IS PUBLIC (verified 2026-10-29), so the full flow —
//  check -> release notes -> download -> verify -> install ->
//  relaunch — works unauthenticated; this app carries NO token or
//  credential. The private-repo fallback stays in the code for
//  robustness: it is driven purely by the HTTP status GitHub
//  answers (404/401/403 -> say so plainly + "Open releases page"),
//  never by a flag here.
// =====================================================================
namespace tbupdate {
inline constexpr const char* kGitHubRepo = "Corporal8065/TickerBar";
} // namespace tbupdate
