#include "protocol/replication_lab.hpp"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Replication scenario contains 64 objects and eight units") {
    auto world = replication::InitialWorld();
    REQUIRE(world.size() == 64);
    int units = 0;
    for (const auto& entity : world) {
        if (entity.kind == 1) ++units;
        CHECK(entity.revision == 1);
    }
    CHECK(units == 8);
}

TEST_CASE("Replication modes independently control two optimizations") {
    auto baseline = replication::ParseMode("baseline");
    auto quantized = replication::ParseMode("quantized");
    auto dirty = replication::ParseMode("dirty");
    auto optimized = replication::ParseMode("optimized");

    CHECK_FALSE(baseline.dirtyOnly);
    CHECK_FALSE(baseline.compact);
    CHECK_FALSE(quantized.dirtyOnly);
    CHECK(quantized.compact);
    CHECK(dirty.dirtyOnly);
    CHECK_FALSE(dirty.compact);
    CHECK(optimized.dirtyOnly);
    CHECK(optimized.compact);
    CHECK_THROWS(replication::ParseMode("unknown"));
}

TEST_CASE("Replication full and compact packet sizes are fixed") {
    replication::Entity entity{63, 15, 7, 1, 0x01020304};
    auto full = replication::EncodeEntity(entity, false);
    auto compact = replication::EncodeEntity(entity, true);
    CHECK(full.size() == 28);
    CHECK(compact.size() == 17);

    const std::vector<uint8_t> expected{
        22, 0, 0, 0, 9, 0, 2, 0,
        0, 63,
        15, 7, 1,
        1, 2, 3, 4
    };
    CHECK(compact == expected);
}

TEST_CASE("Replication ACK round trip preserves object and version") {
    auto bytes = replication::EncodeAck(42, 123);
    auto packet = replication::Parse(bytes.data(), bytes.size());
    REQUIRE(packet.has_value());
    auto ack = replication::DecodeAck(*packet);
    REQUIRE(ack.has_value());
    CHECK(ack->id == 42);
    CHECK(ack->revision == 123);
}

TEST_CASE("Replication parser rejects damaged lengths and versions") {
    auto bytes = replication::EncodeAck(1, 2);
    SECTION("Truncated datagram") {
        bytes.pop_back();
        CHECK_FALSE(replication::Parse(bytes.data(), bytes.size()).has_value());
    }
    SECTION("Wrong version") {
        bytes[6] = 99;
        CHECK_FALSE(replication::Parse(bytes.data(), bytes.size()).has_value());
    }
    SECTION("Invalid object id") {
        bytes = replication::EncodeAck(64, 2);
        auto packet = replication::Parse(bytes.data(), bytes.size());
        REQUIRE(packet.has_value());
        CHECK_FALSE(replication::DecodeAck(*packet).has_value());
    }
}
