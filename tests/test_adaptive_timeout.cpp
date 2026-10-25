#include "reliability/adaptive_timeout.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

TEST_CASE("RTO is 1 s before the first sample") {
    AdaptiveTimeout timeout;
    REQUIRE_FALSE(timeout.HasSample());
    REQUIRE(timeout.RtoMs() == Approx(1000.0));
}

TEST_CASE("First sample initializes SRTT and RTTVAR") {
    AdaptiveTimeout timeout;
    timeout.OnSample(100.0);
    REQUIRE(timeout.SrttMs() == Approx(100.0));
    REQUIRE(timeout.RttVarMs() == Approx(50.0));
    REQUIRE(timeout.RtoMs() == Approx(300.0));   // 100 + 4 * 50
}

TEST_CASE("Stable RTT shrinks RTTVAR") {
    AdaptiveTimeout timeout;
    timeout.OnSample(100.0);
    timeout.OnSample(100.0);
    REQUIRE(timeout.RttVarMs() == Approx(37.5));  // 0.75 * 50 + 0.25 * 0
    REQUIRE(timeout.RtoMs() == Approx(250.0));
}

TEST_CASE("RTO is clamped from below") {
    AdaptiveTimeout timeout;
    timeout.OnSample(1.0);                        // 1 + 4 * 0.5 = 3 мс
    REQUIRE(timeout.RtoMs() == Approx(100.0));
}

TEST_CASE("RTO is clamped from above") {
    AdaptiveTimeout timeout;
    timeout.OnSample(2000.0);                     // 2000 + 4 * 1000 = 6000 мс
    REQUIRE(timeout.RtoMs() == Approx(3000.0));
}

TEST_CASE("Latency spike increases RTO") {
    AdaptiveTimeout timeout;
    for (int i = 0; i < 20; ++i) {
        timeout.OnSample(50.0);
    }
    const double before = timeout.RtoMs();
    timeout.OnSample(300.0);
    REQUIRE(timeout.SrttMs() == Approx(0.875 * 50.0 + 0.125 * 300.0));
    REQUIRE(timeout.RtoMs() > before);
}