#pragma once

#include <cstdint>

// Адаптивный тайм-аут повторной передачи по Джекобсону (RFC 6298):
//   RTTVAR = (1 - beta) * RTTVAR + beta * |SRTT - RTT|
//   SRTT   = (1 - alpha) * SRTT + alpha * RTT
//   RTO    = clamp(SRTT + 4 * RTTVAR, 100 мс, 3000 мс)
// alpha = 0.125 — тот же коэффициент, что у SRTT в telemetry (ПР №2).
// Замеры приходят из PING/PONG и из времени до ACK надёжных пакетов.
class AdaptiveTimeout {
public:
    void OnSample(double rttMs);

    double RtoMs() const;
    std::uint64_t RtoUs() const { return static_cast<std::uint64_t>(RtoMs() * 1000.0); }

    bool HasSample() const { return initialized_; }
    double SrttMs() const { return srttMs_; }
    double RttVarMs() const { return rttVarMs_; }

    static constexpr double kAlpha = 0.125, kBeta = 0.25;
    static constexpr double kMinRtoMs = 100.0, kMaxRtoMs = 3000.0;
    // До первого замера RTT неизвестен: 1 с, как в RFC 6298.
    static constexpr double kInitialRtoMs = 1000.0;

private:
    double srttMs_ = 0.0, rttVarMs_ = 0.0;
    bool initialized_ = false;
};