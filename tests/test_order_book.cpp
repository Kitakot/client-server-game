#include "client/order_book.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Unconfirmed move is rolled back") {
    OrderBook orders;
    orders.OnMoveSent(1, UnitMove{7, 1, 0});
    orders.OnDelivered(1);
    orders.OnMoveSent(2, UnitMove{7, 2, 0});
    REQUIRE(orders.Unit(7).x == 2);          // показан сразу

    orders.OnFailed(2);
    REQUIRE(orders.Unit(7).x == 1);          // вернулся на подтверждённую клетку
    REQUIRE(orders.PendingOrders() == 0);
}

TEST_CASE("Failed old move does not undo a newer one") {
    OrderBook orders;
    orders.OnMoveSent(1, UnitMove{7, 1, 0});
    orders.OnMoveSent(2, UnitMove{7, 2, 0});
    orders.OnFailed(1);
    REQUIRE(orders.Unit(7).x == 2);
}