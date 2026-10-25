#include "reliability/dedup_window.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Repeated sequence is detected") {
    DedupWindow window;
    REQUIRE(window.MarkProcessed(10));
    REQUIRE_FALSE(window.MarkProcessed(10));
    REQUIRE(window.MarkProcessed(11));
}

TEST_CASE("Sequence wrap-around does not break the window") {
    DedupWindow window;
    REQUIRE(window.MarkProcessed(65535));
    REQUIRE(window.MarkProcessed(0));
    REQUIRE_FALSE(window.MarkProcessed(65535));
    REQUIRE_FALSE(window.MarkProcessed(0));
}

TEST_CASE("Oldest sequences are evicted") {
    DedupWindow window(/*capacity=*/3);
    for (std::uint16_t s = 1; s <= 4; ++s) {
        REQUIRE(window.MarkProcessed(s));
    }
    REQUIRE(window.MarkProcessed(1));        // вытеснен, считается новым
    REQUIRE_FALSE(window.MarkProcessed(4));  // ещё в окне
}