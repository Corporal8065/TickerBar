#pragma once
#include <cmath>
#include <vector>
namespace tickerbar {
// DETAIL-CHART VOLUME STATISTICS (v0.3.0, Jackson's T-VOL-flavoured
// ask): a 52-period moving average of VOLUME drawn as a line over the
// detail chart's volume pane, and a flag on bars whose volume runs at
// least 3 standard deviations above that average — the same shape as
// the T-VOL1 signal condition (extreme volume vs its own trailing
// history), drawn honestly instead of traded.
//
// Definitions (pinned by the tests):
//  - The window for bar i is the trailing min(period, i+1) bars
//    ENDING at i (the bar itself included) — the average a viewer
//    would compare that bar against.
//  - ma = mean of the window; sd = POPULATION standard deviation.
//  - hot requires a window of at least kMinWindow bars (a 3-sigma
//    claim on a handful of bars is noise wearing a lab coat) and
//    volume > ma + kSigma * sd (strictly above; with sd == 0 a bar
//    can only be hot by exceeding a flat average, which the strict
//    comparison on its own window can never do — a flat series has
//    no hot bars, by construction).
struct VolumePoint {
    double ma = 0.0;
    double sd = 0.0;
    int window = 0;   // bars in this point's trailing window
    bool hot = false; // volume >= ma + 3 sd over a >= kMinWindow window
};
inline constexpr int kVolumePeriod = 52;
inline constexpr int kVolumeMinWindow = 20;
inline constexpr double kVolumeSigma = 3.0;

inline std::vector<VolumePoint> volumeMaSd(const std::vector<double>& volumes,
                                           int period = kVolumePeriod,
                                           double kSigma = kVolumeSigma) {
    std::vector<VolumePoint> out;
    out.reserve(volumes.size());
    if (period < 1) period = 1;
    for (size_t i = 0; i < volumes.size(); ++i) {
        const size_t begin = (i + 1 > (size_t)period) ? (i + 1 - (size_t)period) : 0;
        const int n = (int)(i + 1 - begin);
        double sum = 0.0;
        for (size_t j = begin; j <= i; ++j) sum += volumes[j];
        const double ma = sum / n;
        double var = 0.0;
        for (size_t j = begin; j <= i; ++j) { const double d = volumes[j] - ma; var += d * d; }
        const double sd = std::sqrt(var / n);
        VolumePoint p;
        p.ma = ma; p.sd = sd; p.window = n;
        p.hot = n >= kVolumeMinWindow && volumes[i] > ma + kSigma * sd;
        out.push_back(p);
    }
    return out;
}
} // namespace tickerbar
