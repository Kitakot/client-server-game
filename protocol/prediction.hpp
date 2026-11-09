#pragma once

#include "protocol/protocol.hpp"

#include <array>
#include <optional>

namespace prediction {

constexpr uint16_t INPUT_PAYLOAD_SIZE = 17;
constexpr uint16_t SNAPSHOT_PAYLOAD_SIZE = 47;

enum class Action : uint8_t {
    Step = 1,
    March = 2,
    EndTurn = 3
};

enum class Result : uint8_t {
    Accepted = 0,
    WrongTurn = 1,
    BadAction = 2,
    BadDirection = 3,
    NotEnoughAp = 4,
    Cooldown = 5,
    Blocked = 6,
    Occupied = 7,
    OutOfMap = 8
};

struct Unit {
    int32_t x = 0;
    int32_t y = 0;
    int32_t ap = 3;
    int32_t marchReadyTurn = 1;
};

struct World {
    int32_t turn = 1;
    uint8_t activePlayer = 0;

    std::array<Unit, 2> units{
        Unit{0, 0, 3, 1},
        Unit{5, 5, 3, 1}
    };
};

struct Input {
    uint32_t id;
    int32_t turn;
    Action action;
    int32_t dx;
    int32_t dy;
};

inline std::optional<Input> ParseInput(const Packet& packet) {
    if (packet.header.type != CommandType::TURN_INPUT ||
        packet.header.payloadSize != INPUT_PAYLOAD_SIZE ||
        packet.header.requiresAck) {
        return std::nullopt;
    }

    const uint8_t* p = packet.payload;
    const uint8_t* end = p + packet.header.payloadSize;

    auto id = ReadU32(p, end);
    auto turn = ReadI32(p, end);
    auto action = ReadU8(p, end);
    auto dx = ReadI32(p, end);
    auto dy = ReadI32(p, end);

    if (!id || !turn || !action || !dx || !dy || p != end) {
        return std::nullopt;
    }

    if (*id == 0 ||
        packet.header.sequence != static_cast<uint16_t>(*id)) {
        return std::nullopt;
    }

    return Input{
        *id,
        *turn,
        static_cast<Action>(*action),
        *dx,
        *dy
    };
}

inline std::vector<uint8_t> SerializeInput(const Input& input) {
    std::vector<uint8_t> payload;
    payload.reserve(INPUT_PAYLOAD_SIZE);

    WriteU32(payload, input.id);
    WriteI32(payload, input.turn);
    WriteU8(payload, static_cast<uint8_t>(input.action));
    WriteI32(payload, input.dx);
    WriteI32(payload, input.dy);

    return serializePacket(
        CommandType::TURN_INPUT,
        static_cast<uint16_t>(input.id),
        payload,
        false);
}

inline std::vector<uint8_t> SerializeWorld(
    const World& world,
    uint8_t player,
    uint32_t revision,
    uint32_t processedInput,
    Result result)
{
    std::vector<uint8_t> payload;
    payload.reserve(SNAPSHOT_PAYLOAD_SIZE);

    WriteU32(payload, revision);
    WriteU8(payload, player);
    WriteU32(payload, processedInput);
    WriteU8(payload, static_cast<uint8_t>(result));

    WriteI32(payload, world.turn);
    WriteU8(payload, world.activePlayer);

    for (const Unit& unit : world.units) {
        WriteI32(payload, unit.x);
        WriteI32(payload, unit.y);
        WriteI32(payload, unit.ap);
        WriteI32(payload, unit.marchReadyTurn);
    }

    return serializePacket(
        CommandType::WORLD_STATE,
        static_cast<uint16_t>(revision),
        payload,
        false);
}

} // namespace prediction