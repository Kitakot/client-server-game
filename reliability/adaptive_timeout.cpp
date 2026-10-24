#include "reliability/adaptive_timeout.hpp"

#include <algorithm>
#include <cmath>

void AdaptiveTimeout::OnSample(double rttMs) {
    if (!(rttMs >= 0.0)) {  // отсекает и отрицательные значения, и NaN
        return;
    }
    if (!initialized_) {
        srttMs_ = rttMs;
        rttVarMs_ = rttMs / 2.0;
        initialized_ = true;
        return;
    }
    // RTTVAR считается по старому SRTT, поэтому обновляется первым.
    rttVarMs_ = (1 - kBeta) * rttVarMs_ + kBeta * std::abs(srttMs_ - rttMs);
    srttMs_ = (1 - kAlpha) * srttMs_ + kAlpha * rttMs;
}

double AdaptiveTimeout::RtoMs() const {
    if (!initialized_) {
        return kInitialRtoMs;
    }
    const double raw = srttMs_ + 4.0 * rttVarMs_;
    return std::clamp(raw, kMinRtoMs, kMaxRtoMs);
}