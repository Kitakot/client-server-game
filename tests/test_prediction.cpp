#include "protocol/prediction.hpp"
#include "server/prediction_match.hpp"

#include <catch2/catch_test_macros.hpp>

namespace pred = prediction;

TEST_CASE("Unity HELLO packet is accepted by the protocol parser") {
    const std::vector<uint8_t> bytes{
        0x06,
        0x00, 0x00,
        0x00, 0x00,
        0x00, 0x02,
        0x00
    };

    auto parsed = parsePacket(bytes.data(), bytes.size());

    REQUIRE(std::holds_alternative<Packet>(parsed));

    const Packet& packet = std::get<Packet>(parsed);

    CHECK(packet.header.type == CommandType::HELLO);
    CHECK(packet.header.payloadSize == 0);
    CHECK_FALSE(packet.header.requiresAck);
}

TEST_CASE("Turn input uses the Unity binary layout") {
    const pred::Input input{
        0x0102, 1, pred::Action::Step, -1, 0
    };

    const std::vector<uint8_t> expected{
        // Заголовок.
        0x07,
        0x01, 0x02,
        0x00, 0x11,
        0x00, 0x02,
        0x00,

        // inputId.
        0x00, 0x00, 0x01, 0x02,

        // turn.
        0x00, 0x00, 0x00, 0x01,

        // action.
        0x01,

        // dx = -1.
        0xFF, 0xFF, 0xFF, 0xFF,

        // dy = 0.
        0x00, 0x00, 0x00, 0x00
    };

    auto bytes = pred::SerializeInput(input);
    REQUIRE(bytes == expected);

    auto parsed = parsePacket(bytes.data(), bytes.size());
    REQUIRE(std::holds_alternative<Packet>(parsed));

    auto decoded = pred::ParseInput(std::get<Packet>(parsed));
    REQUIRE(decoded.has_value());

    CHECK(decoded->id == input.id);
    CHECK(decoded->turn == 1);
    CHECK(decoded->dx == -1);
    CHECK(decoded->dy == 0);
}

TEST_CASE("Turn input rejects a mismatched header sequence") {
    auto bytes = pred::SerializeInput(
        pred::Input{1, 1, pred::Action::Step, 0, 1});

    bytes[2] = 2;

    auto parsed = parsePacket(bytes.data(), bytes.size());
    REQUIRE(std::holds_alternative<Packet>(parsed));

    CHECK_FALSE(
        pred::ParseInput(std::get<Packet>(parsed)).has_value());
}

TEST_CASE("Initial snapshot matches the Unity layout") {
    const std::vector<uint8_t> expected{
        // type=8, sequence=1, payload=47, version=2, ack=0.
        0x08, 0x00, 0x01, 0x00, 0x2F, 0x00, 0x02, 0x00,

        // revision=1, player=0, processed=0, result=Accepted.
        0x00, 0x00, 0x00, 0x01,
        0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00,

        // turn=1, activePlayer=0.
        0x00, 0x00, 0x00, 0x01,
        0x00,

        // Unit 0: x=0, y=0, ap=3, ready=1.
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x03,
        0x00, 0x00, 0x00, 0x01,

        // Unit 1: x=5, y=5, ap=3, ready=1.
        0x00, 0x00, 0x00, 0x05,
        0x00, 0x00, 0x00, 0x05,
        0x00, 0x00, 0x00, 0x03,
        0x00, 0x00, 0x00, 0x01
    };

    auto bytes = pred::SerializeWorld(
        pred::World{},
        0,
        1,
        0,
        pred::Result::Accepted);

    REQUIRE(bytes.size() == 55);
    CHECK(bytes == expected);
}

TEST_CASE("Duplicate input does not spend AP twice") {
    pred::Match match;
    const pred::Input input{1, 1, pred::Action::Step, 0, 1};

    REQUIRE(match.Process(0, input));
    CHECK_FALSE(match.Process(0, input));

    CHECK(match.State().units[0].y == 1);
    CHECK(match.State().units[0].ap == 2);
    CHECK(match.Processed(0) == 1);
}

TEST_CASE("Rejected march is processed without changing the unit") {
    pred::Match match;

    REQUIRE(match.Process(
        0, pred::Input{1, 1, pred::Action::March, 1, 0}));

    CHECK(match.LastResult(0) == pred::Result::Blocked);
    CHECK(match.Processed(0) == 1);
    CHECK(match.State().units[0].x == 0);
    CHECK(match.State().units[0].ap == 3);

    // Отказ не блокирует следующий приказ.
    REQUIRE(match.Process(
        0, pred::Input{2, 1, pred::Action::Step, 0, 1}));

    CHECK(match.LastResult(0) == pred::Result::Accepted);
    CHECK(match.State().units[0].y == 1);
    CHECK(match.State().units[0].ap == 2);
}

TEST_CASE("Out of order input can be retried after its predecessor") {
    pred::Match match;

    const pred::Input first{1, 1, pred::Action::Step, 0, 1};
    const pred::Input second{2, 1, pred::Action::Step, 1, 0};

    CHECK_FALSE(match.Process(0, second));
    REQUIRE(match.Process(0, first));
    REQUIRE(match.Process(0, second));

    CHECK(match.State().units[0].x == 1);
    CHECK(match.State().units[0].y == 1);
    CHECK(match.State().units[0].ap == 1);
}

TEST_CASE("Player cannot finish another player's turn") {
    pred::Match match;

    REQUIRE(match.Process(
        1, pred::Input{1, 1, pred::Action::EndTurn, 0, 0}));

    CHECK(match.LastResult(1) == pred::Result::WrongTurn);
    CHECK(match.State().activePlayer == 0);
    CHECK(match.State().turn == 1);
}

TEST_CASE("An input from an earlier turn is rejected") {
    pred::Match match;

    REQUIRE(match.Process(
        0, pred::Input{1, 1, pred::Action::EndTurn, 0, 0}));

    REQUIRE(match.Process(
        1, pred::Input{1, 2, pred::Action::EndTurn, 0, 0}));

    REQUIRE(match.State().turn == 3);

    REQUIRE(match.Process(
        0, pred::Input{2, 1, pred::Action::Step, 0, 1}));

    CHECK(match.LastResult(0) == pred::Result::WrongTurn);
    CHECK(match.State().units[0].y == 0);
}

TEST_CASE("March cooldown lasts until the configured turn") {
    pred::Match match;

    REQUIRE(match.Process(
        0, pred::Input{1, 1, pred::Action::March, 0, 1}));

    REQUIRE(match.LastResult(0) == pred::Result::Accepted);
    CHECK(match.State().units[0].marchReadyTurn == 5);

    REQUIRE(match.Process(
        0, pred::Input{2, 1, pred::Action::EndTurn, 0, 0}));

    REQUIRE(match.Process(
        1, pred::Input{1, 2, pred::Action::EndTurn, 0, 0}));

    REQUIRE(match.Process(
        0, pred::Input{3, 3, pred::Action::March, 0, 1}));

    CHECK(match.LastResult(0) == pred::Result::Cooldown);
    CHECK(match.State().units[0].ap == 3);

    REQUIRE(match.Process(
        0, pred::Input{4, 3, pred::Action::EndTurn, 0, 0}));

    REQUIRE(match.Process(
        1, pred::Input{2, 4, pred::Action::EndTurn, 0, 0}));

    REQUIRE(match.Process(
        0, pred::Input{5, 5, pred::Action::March, 0, 1}));

    CHECK(match.LastResult(0) == pred::Result::Accepted);
    CHECK(match.State().units[0].y == 4);
}

TEST_CASE("Movement validates AP, map bounds and occupied cells") {
    SECTION("Not enough AP") {
        pred::World world;
        world.units[0].ap = 0;

        CHECK(pred::Apply(
            world, 0, pred::Input{1, 1, pred::Action::Step, 0, 1})
            == pred::Result::NotEnoughAp);
    }

    SECTION("Outside map") {
        pred::World world;

        CHECK(pred::Apply(
            world, 0, pred::Input{1, 1, pred::Action::Step, -1, 0})
            == pred::Result::OutOfMap);
    }

    SECTION("Occupied cell") {
        pred::World world;
        world.units[1].x = 0;
        world.units[1].y = 1;

        CHECK(pred::Apply(
            world, 0, pred::Input{1, 1, pred::Action::Step, 0, 1})
            == pred::Result::Occupied);
    }
}