#pragma once
#include <cmath>
#include <cstdio>
#include <string>
namespace tickerbar {
// Display formatting — every rule pinned by unit tests so the bar can
// never quietly change how a number reads between builds.

// Decimals for a price: majors read like an exchange (2dp), small
// prices get the extra places they need to move visibly.
inline int decimalsForPrice(double px) {
    const double a = std::fabs(px);
    if (a >= 1.0) return 2;
    if (a >= 0.01) return 4;
    return 6;
}

inline std::string groupThousands(std::string digits) {
    std::string out;
    int run = 0;
    for (int i = (int)digits.size() - 1; i >= 0; --i) {
        out += digits[(size_t)i];
        if (++run == 3 && i > 0) { out += ','; run = 0; }
    }
    return std::string(out.rbegin(), out.rend());
}

inline std::string formatPrice(double px) {
    const int dec = decimalsForPrice(px);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", dec, std::fabs(px));
    std::string s = buf;
    auto dot = s.find('.');
    std::string grouped = groupThousands(dot == std::string::npos ? s : s.substr(0, dot));
    if (dot != std::string::npos) grouped += s.substr(dot);
    return std::string(px < 0 ? "-$" : "$") + grouped;
}

inline std::string formatPct(double pct) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%+.2f%%", pct);
    return buf;
}

// 24h notional volume: $1.23B / $456.70M / $12.35K / $999
inline std::string formatVolume(double usd) {
    char buf[32];
    const double a = std::fabs(usd);
    if (a >= 1e9) std::snprintf(buf, sizeof buf, "$%.2fB", usd / 1e9);
    else if (a >= 1e6) std::snprintf(buf, sizeof buf, "$%.2fM", usd / 1e6);
    else if (a >= 1e3) std::snprintf(buf, sizeof buf, "$%.2fK", usd / 1e3);
    else std::snprintf(buf, sizeof buf, "$%.0f", usd);
    return buf;
}

// BTC dominance percent, 1dp: "59.5%".
inline std::string formatDominance(double pct) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f%%", pct);
    return buf;
}

// Fear & Greed band for tinting: 0 Extreme Fear, 1 Fear, 2 Neutral,
// 3 Greed, 4 Extreme Greed (Alternative.me's own band edges).
inline int fngBandIndex(int value) {
    if (value <= 24) return 0;
    if (value <= 44) return 1;
    if (value <= 55) return 2;
    if (value <= 74) return 3;
    return 4;
}

} // namespace tickerbar
