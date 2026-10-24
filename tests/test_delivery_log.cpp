#include "reliability/delivery_log.hpp"

#include <catch2/catch_test_macros.hpp>

#include <sstream>

TEST_CASE("Delivery CSV rows") {
    DeliveryRecord delivered;
    delivered.experimentId = "t";
    delivered.sequence = 1;
    delivered.command = "UNIT_MOVE";
    delivered.firstSentUs = 1'000'000;
    delivered.finishedUs = 1'050'000;
    delivered.attempts = 1;
    delivered.rtoMs = 300;
    delivered.srttMs = 50;
    delivered.status = DeliveryStatus::Delivered;

    DeliveryRecord failed = delivered;
    failed.sequence = 2;
    failed.command = "UNIT_ATTACK";
    failed.firstSentUs = 1'200'000;
    failed.finishedUs = 3'000'000;
    failed.attempts = 5;
    failed.rtoMs = 1000;
    failed.status = DeliveryStatus::Failed;

    std::ostringstream out;
    WriteDeliveryCsvRows(out, {delivered, failed});
    REQUIRE(out.str() ==
        "t;1;UNIT_MOVE;1;0.000;50.000;50.000;300.000;50.000;delivered\n"
        "t;2;UNIT_ATTACK;5;200.000;2000.000;;1000.000;50.000;failed\n");
}