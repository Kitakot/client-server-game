#include "reliability/reliable_channel.hpp"

#include <cstddef>
#include <iostream>
#include <utility>

void ReliableChannel::OnSent(
    std::uint16_t sequence, std::vector<std::uint8_t> rawBytes, std::uint64_t nowUs) {
    PendingPacket packet;
    packet.rawBytes = std::move(rawBytes);
    packet.firstSentAtUs = nowUs;
    packet.lastSentAtUs = nowUs;
    pending_[sequence] = std::move(packet);
}

bool ReliableChannel::OnAckReceived(std::uint16_t sequence) {
    return pending_.erase(sequence) > 0;
}

const PendingPacket* ReliableChannel::Find(std::uint16_t sequence) const {
    auto it = pending_.find(sequence);
    return it == pending_.end() ? nullptr : &it->second;
}

std::vector<PendingPacket*> ReliableChannel::CollectForRetransmission(
    std::uint64_t nowUs, std::uint64_t adaptiveTimeoutUs) {
    std::vector<PendingPacket*> due;
    for (auto it = pending_.begin(); it != pending_.end();) {
        PendingPacket& packet = it->second;
        if (nowUs < packet.lastSentAtUs || nowUs - packet.lastSentAtUs < adaptiveTimeoutUs) {
            ++it;
            continue;
        }
        if (packet.attempts >= maxAttempts_) {
            std::cerr << "[RELIABILITY] delivery failed [sequence=" << it->first
                      << ", attempts=" << packet.attempts << "]\n";
            failed_.push_back(it->first);
            // erase в unordered_map не трогает остальные элементы, поэтому
            // указатели, уже сложенные в due, остаются действительными.
            it = pending_.erase(it);
            continue;
        }
        ++packet.attempts;
        packet.lastSentAtUs = nowUs;
        due.push_back(&packet);
        ++it;
    }
    return due;
}

std::vector<std::uint16_t> ReliableChannel::TakeNewFailures() {
    std::vector<std::uint16_t> fresh(
        failed_.begin() + static_cast<std::ptrdiff_t>(reportedFailures_), failed_.end());
    reportedFailures_ = failed_.size();
    return fresh;
}

std::size_t ReliableChannel::PendingCount() const {
    return pending_.size();
}

std::size_t ReliableChannel::FailedCount() const {
    return failed_.size();
}