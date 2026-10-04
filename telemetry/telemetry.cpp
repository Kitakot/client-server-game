#include "telemetry/telemetry.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

const char* toString(SampleStatus status) {
    switch (status) {
        case SampleStatus::Pending: return "pending";
        case SampleStatus::Received: return "received";
        case SampleStatus::Timeout: return "timeout";
    }
    return "unknown";
}

const char* toString(PongResult result) {
    switch (result) {
        case PongResult::Received: return "received";
        case PongResult::Late: return "late_response";
        case PongResult::Duplicate: return "duplicate_response";
        case PongResult::Unknown: return "unknown_response";
    }
    return "unknown";
}

double Mean(const std::vector<double>& values) {
    if (values.empty()) {
        return 0.0;
    }
    return std::accumulate(values.begin(), values.end(), 0.0) / values.size();
}

double Median(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

double MeanJitter(const std::vector<double>& values) {
    if (values.size() < 2) {
        return 0.0;
    }
    double sum = 0.0;
    for (std::size_t i = 1; i < values.size(); ++i) {
        sum += std::abs(values[i] - values[i - 1]);
    }
    return sum / (values.size() - 1);
}

Telemetry::Telemetry(std::string experimentId, std::uint64_t timeoutUs, std::size_t maxInFlight)
    : experimentId_(std::move(experimentId)),
      timeoutUs_(timeoutUs),
      maxInFlight_(maxInFlight) {}

void Telemetry::OnPingSent(std::uint16_t sequence, std::uint64_t nowUs) {
    // Места нет — вытесняем самый старый номер. Если ответа на него так и
    // не было, это потеря.
    while (inFlight_.size() >= maxInFlight_ && !order_.empty()) {
        const std::uint16_t oldest = order_.front();
        order_.pop_front();
        auto it = inFlight_.find(oldest);
        if (it == inFlight_.end()) {
            continue;
        }
        Sample& old = samples_[it->second];
        if (old.status == SampleStatus::Pending) {
            old.status = SampleStatus::Timeout;
        }
        inFlight_.erase(it);
    }

    Sample sample;
    sample.experimentId = experimentId_;
    sample.index = samples_.size() + 1;
    sample.sequence = sequence;
    sample.sentUs = nowUs;
    samples_.push_back(sample);

    inFlight_[sequence] = samples_.size() - 1;
    order_.push_back(sequence);
}

PongResult Telemetry::OnPong(std::uint16_t sequence, std::uint64_t nowUs) {
    auto it = inFlight_.find(sequence);
    if (it == inFlight_.end()) {
        ++unknown_;
        return PongResult::Unknown;
    }

    Sample& sample = samples_[it->second];
    if (sample.answered) {
        ++duplicate_;
        return PongResult::Duplicate;
    }
    sample.answered = true;

    // Ответ после тайм-аута не даёт RTT: измерение остаётся потерей.
    const std::uint64_t elapsedUs = nowUs - sample.sentUs;
    if (sample.status == SampleStatus::Timeout || elapsedUs > timeoutUs_) {
        sample.status = SampleStatus::Timeout;
        ++late_;
        return PongResult::Late;
    }

    const double rttMs = elapsedUs / 1000.0;
    // Первое измерение инициализирует SRTT, дальше — сглаживание.
    srttMs_ = hasSrtt_ ? 0.875 * srttMs_ + 0.125 * rttMs : rttMs;
    hasSrtt_ = true;

    sample.status = SampleStatus::Received;
    sample.rttMs = rttMs;
    sample.srttMs = srttMs_;
    return PongResult::Received;
}

std::size_t Telemetry::Expire(std::uint64_t nowUs) {
    std::size_t expired = 0;
    for (auto& entry : inFlight_) {
        Sample& sample = samples_[entry.second];
        if (sample.status == SampleStatus::Pending && nowUs - sample.sentUs > timeoutUs_) {
            sample.status = SampleStatus::Timeout;
            ++expired;
        }
    }
    return expired;
}

Summary Telemetry::Summarize() const {
    Summary summary;
    summary.sent = samples_.size();
    summary.late = late_;
    summary.duplicate = duplicate_;
    summary.unknown = unknown_;
    summary.srttMs = srttMs_;

    std::vector<double> rtts;
    for (const Sample& sample : samples_) {
        if (sample.status == SampleStatus::Received) {
            rtts.push_back(*sample.rttMs);
        } else if (sample.status == SampleStatus::Timeout) {
            ++summary.timeouts;
        }
    }
    summary.received = rtts.size();

    if (!rtts.empty()) {
        summary.minMs = *std::min_element(rtts.begin(), rtts.end());
        summary.maxMs = *std::max_element(rtts.begin(), rtts.end());
        summary.meanMs = Mean(rtts);
        summary.medianMs = Median(rtts);
        summary.jitterMs = MeanJitter(rtts);
    }
    if (summary.sent > 0) {
        summary.lossPercent = 100.0 * summary.timeouts / summary.sent;
    }
    return summary;
}