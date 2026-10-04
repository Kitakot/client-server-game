#include "telemetry/csv_log.hpp"
#include "telemetry/telemetry.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <sstream>

using Catch::Approx;

TEST_CASE("Mean, median and jitter") {
REQUIRE(Mean({10.0, 20.0, 60.0}) == Approx(30.0));
REQUIRE(Median({30.0, 10.0, 20.0}) == Approx(20.0));
REQUIRE(Median({40.0, 10.0, 20.0, 30.0}) == Approx(25.0));
// |20-10| + |15-20| = 15, делим на n-1 = 2
REQUIRE(MeanJitter({10.0, 20.0, 15.0}) == Approx(7.5));
REQUIRE(MeanJitter({10.0}) == Approx(0.0));
REQUIRE(Mean({}) == Approx(0.0));
}

TEST_CASE("First RTT initializes SRTT, next ones are smoothed") {
Telemetry telemetry("test");
telemetry.OnPingSent(1, 0);
REQUIRE(telemetry.OnPong(1, 10'000) == PongResult::Received); // RTT 10 мс
REQUIRE(*telemetry.Samples()[0].rttMs == Approx(10.0));
REQUIRE(*telemetry.Samples()[0].srttMs == Approx(10.0));

telemetry.OnPingSent(2, 200'000);
REQUIRE(telemetry.OnPong(2, 220'000) == PongResult::Received); // RTT 20 мс
REQUIRE(*telemetry.Samples()[1].srttMs == Approx(0.875 * 10.0 + 0.125 * 20.0));
}

TEST_CASE("PING without PONG becomes timeout") {
Telemetry telemetry("test");
telemetry.OnPingSent(1, 0);
REQUIRE(telemetry.Expire(1'000'000) == 0); // ровно тайм-аут — ещё ждём
REQUIRE(telemetry.Expire(1'000'001) == 1);
REQUIRE(telemetry.Samples()[0].status == SampleStatus::Timeout);
REQUIRE(telemetry.Summarize().lossPercent == Approx(100.0));
}

TEST_CASE("PONG after timeout is a late response and stays a loss") {
Telemetry telemetry("test");
telemetry.OnPingSent(1, 0);
telemetry.Expire(1'500'000);
REQUIRE(telemetry.OnPong(1, 1'600'000) == PongResult::Late);
REQUIRE(telemetry.Samples()[0].status == SampleStatus::Timeout);
REQUIRE_FALSE(telemetry.Samples()[0].rttMs.has_value());
// Второй ответ на тот же номер — уже дубликат.
REQUIRE(telemetry.OnPong(1, 1'700'000) == PongResult::Duplicate);
}

TEST_CASE("Second PONG is a duplicate and does not change SRTT") {
Telemetry telemetry("test");
telemetry.OnPingSent(1, 0);
REQUIRE(telemetry.OnPong(1, 10'000) == PongResult::Received);
REQUIRE(telemetry.OnPong(1, 50'000) == PongResult::Duplicate);
REQUIRE(telemetry.Summarize().srttMs == Approx(10.0));
REQUIRE(telemetry.Summarize().duplicate == 1);
}

TEST_CASE("PONG with unknown sequence is rejected") {
Telemetry telemetry("test");
telemetry.OnPingSent(1, 0);
REQUIRE(telemetry.OnPong(999, 10'000) == PongResult::Unknown);
REQUIRE(telemetry.Summarize().received == 0);
}

TEST_CASE("inFlight is bounded: evicted sequence becomes unknown") {
Telemetry telemetry("test", 1'000'000, 2);
telemetry.OnPingSent(1, 0);
telemetry.OnPingSent(2, 100);
telemetry.OnPingSent(3, 200); // вытесняет sequence 1
REQUIRE(telemetry.OnPong(1, 300) == PongResult::Unknown);
REQUIRE(telemetry.Samples()[0].status == SampleStatus::Timeout);
REQUIRE(telemetry.OnPong(3, 400) == PongResult::Received);
}

TEST_CASE("Summary counts packets and loss rate") {
Telemetry telemetry("test");
telemetry.OnPingSent(1, 0);
telemetry.OnPingSent(2, 200'000);
telemetry.OnPingSent(3, 400'000);
telemetry.OnPingSent(4, 600'000);
telemetry.OnPong(1, 10'000); // 10 мс
telemetry.OnPong(2, 230'000); // 30 мс
telemetry.OnPong(4, 620'000); // 20 мс
telemetry.Expire(5'000'000); // sequence 3 потерян

const Summary s = telemetry.Summarize();
REQUIRE(s.sent == 4);
REQUIRE(s.received == 3);
REQUIRE(s.timeouts == 1);
REQUIRE(s.minMs == Approx(10.0));
REQUIRE(s.maxMs == Approx(30.0));
REQUIRE(s.meanMs == Approx(20.0));
REQUIRE(s.medianMs == Approx(20.0));
REQUIRE(s.jitterMs == Approx(15.0)); // (|30-10| + |20-30|) / 2
REQUIRE(s.lossPercent == Approx(25.0));
}

TEST_CASE("CSV rows: timeout has empty rtt and srtt") {
Telemetry telemetry("loss_5");
telemetry.OnPingSent(42, 1'000'000);
telemetry.OnPingSent(43, 1'200'000);
telemetry.OnPong(42, 1'001'730);
telemetry.Expire(9'000'000);

std::ostringstream out;
WriteCsvHeader(out);
WriteCsvRows(out, telemetry.Samples());
REQUIRE(out.str() ==
"experiment_id;sample;sequence;sent_at_ms;rtt_ms;srtt_ms;status\n"
"loss_5;1;42;0.000;1.730;1.730;received\n"
"loss_5;2;43;200.000;;;timeout\n");
}