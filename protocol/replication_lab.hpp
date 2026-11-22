#pragma once

#include "protocol/protocol.hpp"

#include <array>
#include <optional>
#include <stdexcept>
#include <string>

namespace replication {

constexpr uint16_t PORT = 54020;
constexpr int OBJECT_COUNT = 64;
constexpr int STATIC_COUNT = 56;

constexpr uint8_t HELLO = 20;
constexpr uint8_t FULL = 21;
constexpr uint8_t COMPACT = 22;
constexpr uint8_t ACK = 23;

struct Entity {
    uint16_t id = 0;
    uint8_t x = 0;
    uint8_t y = 0;
    uint8_t kind = 0; // 0 — объект карты, 1 — юнит
    uint32_t revision = 1;
};

struct Mode {
    bool dirtyOnly = false;
    bool compact = false;
};

inline Mode ParseMode(const std::string& name) {
    if (name == "baseline") return {false, false};
    if (name == "quantized") return {false, true};
    if (name == "dirty") return {true, false};
    if (name == "optimized") return {true, true};

    throw std::invalid_argument("Unknown replication mode");
}

inline std::array<Entity, OBJECT_COUNT> InitialWorld() {
    std::array<Entity, OBJECT_COUNT> world{};

    for (int i = 0; i < OBJECT_COUNT; ++i) {
        world[i] = Entity{
            static_cast<uint16_t>(i),
            static_cast<uint8_t>(i % 8),
            static_cast<uint8_t>(i / 8),
            static_cast<uint8_t>(i >= STATIC_COUNT ? 1 : 0),
            1
        };
    }

    return world;
}

inline std::vector<uint8_t> Wrap(
    uint8_t type,
    const std::vector<uint8_t>& payload)
{
    return serializePacket(
        static_cast<CommandType>(type),
        0,
        payload,
        false);
}

struct PacketView {
    uint8_t type;
    const uint8_t* payload;
    uint16_t size;
};

inline std::optional<PacketView> Parse(
    const uint8_t* data,
    size_t size)
{
    if (!data || size < HEADER_SIZE || size > MAX_DATAGRAM_SIZE) {
        return std::nullopt;
    }

    const uint8_t* p = data;
    const uint8_t* end = data + size;

    auto type = ReadU8(p, end);
    auto sequence = ReadU16(p, end);
    auto payloadSize = ReadU16(p, end);
    auto version = ReadU16(p, end);
    auto ackFlag = ReadU8(p, end);

    if (!type || !sequence || !payloadSize || !version || !ackFlag) {
        return std::nullopt;
    }

    if (*type < HELLO || *type > ACK ||
        *sequence != 0 ||
        *version != PROTOCOL_VERSION ||
        *ackFlag != 0 ||
        HEADER_SIZE + *payloadSize != size) {
        return std::nullopt;
    }

    return PacketView{*type, p, *payloadSize};
}

inline std::vector<uint8_t> EncodeEntity(
    const Entity& entity,
    bool compact)
{
    std::vector<uint8_t> payload;

    if (compact) {
        WriteU16(payload, entity.id);
        WriteU8(payload, entity.x);
        WriteU8(payload, entity.y);
        WriteU8(payload, entity.kind);
        WriteU32(payload, entity.revision);
    } else {
        // Базовый формат: координаты в тысячных долях клетки.
        WriteU32(payload, entity.id);
        WriteI32(payload, static_cast<int32_t>(entity.x) * 1000);
        WriteI32(payload, static_cast<int32_t>(entity.y) * 1000);
        WriteU32(payload, entity.kind);
        WriteU32(payload, entity.revision);
    }

    return Wrap(compact ? COMPACT : FULL, payload);
}

inline std::vector<uint8_t> EncodeAck(
    uint16_t id,
    uint32_t revision)
{
    std::vector<uint8_t> payload;
    WriteU16(payload, id);
    WriteU32(payload, revision);
    return Wrap(ACK, payload);
}

struct Acknowledgement {
    uint16_t id;
    uint32_t revision;
};

inline std::optional<Acknowledgement> DecodeAck(
    const PacketView& packet)
{
    if (packet.type != ACK || packet.size != 6) {
        return std::nullopt;
    }

    const uint8_t* p = packet.payload;
    const uint8_t* end = p + packet.size;

    auto id = ReadU16(p, end);
    auto revision = ReadU32(p, end);

    if (!id || !revision || *id >= OBJECT_COUNT || *revision == 0) {
        return std::nullopt;
    }

    return Acknowledgement{*id, *revision};
}

} // namespace replication