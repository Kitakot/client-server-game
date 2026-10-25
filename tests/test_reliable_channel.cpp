#include "reliability/reliable_channel.hpp"

#include <catch2/catch_test_macros.hpp>

// --- Тесты из задания ---

TEST_CASE("Packet is retransmitted after RTO expires") {
    ReliableChannel channel(/*maxAttempts=*/3);
    channel.OnSent(1, {0x04, 0x00, 0x01}, /*nowUs=*/0);

    auto none = channel.CollectForRetransmission(/*nowUs=*/50'000, /*rtoUs=*/200'000);
    REQUIRE(none.empty());

    auto due = channel.CollectForRetransmission(/*nowUs=*/250'000, /*rtoUs=*/200'000);
    REQUIRE(due.size() == 1);
    REQUIRE(due.front()->attempts == 2);
}

TEST_CASE("Packet exceeding max attempts moves to failed") {
    ReliableChannel channel(/*maxAttempts=*/2);
    channel.OnSent(7, {}, 0);
    channel.CollectForRetransmission(300'000, 200'000);  // попытка 2
    channel.CollectForRetransmission(600'000, 200'000);  // попытка 3 -> failed
    REQUIRE(channel.FailedCount() == 1);
    REQUIRE(channel.PendingCount() == 0);
}

TEST_CASE("Duplicate ACK is ignored") {
    ReliableChannel channel;
    channel.OnSent(3, {}, 0);
    REQUIRE(channel.OnAckReceived(3));
    REQUIRE_FALSE(channel.OnAckReceived(3));
}

// --- Дополнительные ---

TEST_CASE("Packet is not returned twice within one RTO") {
    ReliableChannel channel;
    channel.OnSent(1, {}, 0);
    REQUIRE(channel.CollectForRetransmission(200'000, 200'000).size() == 1);
    REQUIRE(channel.CollectForRetransmission(300'000, 200'000).empty());
    REQUIRE(channel.CollectForRetransmission(400'000, 200'000).size() == 1);
}

TEST_CASE("ACK stops retransmission") {
    ReliableChannel channel;
    channel.OnSent(1, {}, 0);
    REQUIRE(channel.OnAckReceived(1));
    REQUIRE(channel.CollectForRetransmission(10'000'000, 200'000).empty());
    REQUIRE(channel.PendingCount() == 0);
    REQUIRE(channel.FailedCount() == 0);
}

TEST_CASE("ACK for unknown sequence is ignored") {
    ReliableChannel channel;
    channel.OnSent(1, {}, 0);
    REQUIRE_FALSE(channel.OnAckReceived(42));
    REQUIRE(channel.PendingCount() == 1);
}

TEST_CASE("Late ACK after failure is ignored") {
    ReliableChannel channel(/*maxAttempts=*/1);
    channel.OnSent(5, {}, 0);
    channel.CollectForRetransmission(200'000, 200'000);
    REQUIRE(channel.FailedCount() == 1);
    REQUIRE_FALSE(channel.OnAckReceived(5));
}

TEST_CASE("Retransmission keeps original bytes and first send time") {
    ReliableChannel channel;
    channel.OnSent(1, {0xAA, 0xBB}, 1'000);
    auto due = channel.CollectForRetransmission(201'000, 200'000);
    REQUIRE(due.size() == 1);
    REQUIRE(due.front()->rawBytes == std::vector<std::uint8_t>{0xAA, 0xBB});
    REQUIRE(due.front()->firstSentAtUs == 1'000);
    REQUIRE(due.front()->lastSentAtUs == 201'000);
}

TEST_CASE("Packets have independent timers") {
    ReliableChannel channel;
    channel.OnSent(1, {}, 0);
    channel.OnSent(2, {}, 100'000);
    auto due = channel.CollectForRetransmission(200'000, 200'000);
    REQUIRE(due.size() == 1);
    REQUIRE(due.front()->firstSentAtUs == 0);
    REQUIRE(channel.Find(2)->attempts == 1);
}

TEST_CASE("Each failure is reported once") {
    ReliableChannel channel(/*maxAttempts=*/1);
    channel.OnSent(9, {}, 0);
    channel.CollectForRetransmission(200'000, 200'000);
    REQUIRE(channel.TakeNewFailures() == std::vector<std::uint16_t>{9});
    REQUIRE(channel.TakeNewFailures().empty());
}