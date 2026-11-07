#pragma once

#include "protocol/prediction.hpp"

#include <array>
#include <limits>

namespace prediction {

inline Result Apply(
    World& world,
    uint8_t player,
    const Input& input)
{
    if (player >= world.units.size() ||
        world.activePlayer != player ||
        input.turn != world.turn) {
        return Result::WrongTurn;
    }

    if (input.action == Action::EndTurn) {
        world.activePlayer = 1 - world.activePlayer;
        ++world.turn;
        world.units[world.activePlayer].ap = 3;

        return Result::Accepted;
    }

    if (input.action != Action::Step &&
        input.action != Action::March) {
        return Result::BadAction;
    }

    const bool horizontal =
        (input.dx == -1 || input.dx == 1) && input.dy == 0;

    const bool vertical =
        (input.dy == -1 || input.dy == 1) && input.dx == 0;

    if (!horizontal && !vertical) {
        return Result::BadDirection;
    }

    Unit& unit = world.units[player];
    const int distance = input.action == Action::March ? 2 : 1;

    if (unit.ap < distance) {
        return Result::NotEnoughAp;
    }

    if (input.action == Action::March &&
        world.turn < unit.marchReadyTurn) {
        return Result::Cooldown;
    }

    // Проверяем весь путь до изменения координат или AP.
    for (int step = 1; step <= distance; ++step) {
        const int x = unit.x + input.dx * step;
        const int y = unit.y + input.dy * step;

        if (x < 0 || x >= 6 || y < 0 || y >= 6) {
            return Result::OutOfMap;
        }

        if (x == 2 && y == 0) {
            return Result::Blocked;
        }

        const Unit& other = world.units[1 - player];

        if (other.x == x && other.y == y) {
            return Result::Occupied;
        }
    }

    unit.x += input.dx * distance;
    unit.y += input.dy * distance;
    unit.ap -= distance;

    if (input.action == Action::March) {
        unit.marchReadyTurn = world.turn + 4;
    }

    return Result::Accepted;
}

class Match {
public:
    // true: приказ обработан, включая отказ по игровым правилам.
    // false: неверный игрок, дубликат или нарушение порядка номеров.
    bool Process(uint8_t player, const Input& input) {
        if (player >= world_.units.size()) {
            return false;
        }

        if (processed_[player] ==
            (std::numeric_limits<uint32_t>::max)()) {
            return false;
        }

        if (input.id != processed_[player] + 1) {
            return false;
        }

        results_[player] = Apply(world_, player, input);

        // При отказе номер также продвигается.
        processed_[player] = input.id;
        return true;
    }

    const World& State() const {
        return world_;
    }

    uint32_t Processed(uint8_t player) const {
        return processed_.at(player);
    }

    Result LastResult(uint8_t player) const {
        return results_.at(player);
    }

private:
    World world_;

    std::array<uint32_t, 2> processed_{0, 0};

    std::array<Result, 2> results_{
        Result::Accepted,
        Result::Accepted
    };
};

} // namespace prediction