#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

// Протокол общения клиента и сервера поверх UDP. Соединения нет: клиент
// шлёт команду одной датаграммой, сервер проверяет её и отвечает одной
// датаграммой с тем же packetType и sequenceNumber, по которым клиент
// сопоставляет ответ с запросом. Некорректные датаграммы сервер молча
// отбрасывает, без ответа.
//
// Формат датаграммы (все многобайтовые поля в сетевом порядке, big-endian):
//   0  u8  packetType       CommandType: 1 = UNIT_MOVE, 2 = UNIT_ATTACK,
//                           3 = PING, 4 = PONG, 5 = ACK
//   1  u16 sequenceNumber   номер пакета у клиента; в ответе и ACK — номер запроса
//   3  u16 payloadSize      длина payload в байтах
//   5  u16 protocolVersion  PROTOCOL_VERSION
//   7  u8  requiresAck      1 — получатель обязан ответить ACK, 0 — нет
//   8  payload[payloadSize]
// Вся датаграмма не длиннее MAX_DATAGRAM_SIZE.
//
// Payload запросов:
//   UNIT_MOVE   (12 байт): i32 unitId, i32 targetX, i32 targetY
//   UNIT_ATTACK  (8 байт): i32 attackerId, i32 targetId
//   PING         (8 байт): u64 clientSendTimeUs
//   PONG        (24 байт): u64 clientSendTimeUs, u64 serverReceiveTimeUs, u64 serverSendTimeUs
// Payload ответа сервера — ASCII-текст без завершающего нуля.
//
// Надёжные пакеты (requiresAck = 1): UNIT_MOVE, UNIT_ATTACK. Сам ACK
// подтверждения не требует: пакет ACK с requiresAck = 1 отклоняется.
//
// Пример: UNIT_MOVE, sequence=1, юнит 1 в точку (2, 1) — 20 байт:
//   01 | 00 01 | 00 0C | 00 02 | 01 | 00 00 00 01 | 00 00 00 02 | 00 00 00 01
//
// Структуры никогда не копируются в буфер целиком: каждое поле пишется и
// читается явно, поэтому формат не зависит от выравнивания и порядка байтов
// платформы.

constexpr uint16_t PROTOCOL_VERSION = 2;
constexpr size_t HEADER_SIZE = 8;
constexpr size_t MAX_DATAGRAM_SIZE = 1024;

enum class CommandType : uint8_t {
    UNIT_MOVE = 1,
    UNIT_ATTACK = 2,
    PING = 3,
    PONG = 4,
    ACK = 5
};

inline bool isKnownCommandType(uint8_t value) {
    switch (static_cast<CommandType>(value)) {
        case CommandType::UNIT_MOVE:
        case CommandType::UNIT_ATTACK:
        case CommandType::PING:
        case CommandType::PONG:
        case CommandType::ACK:
            return true;
    }
    return false;
}

struct PacketHeader {
    CommandType type;
    uint16_t sequence;
    uint16_t payloadSize;
    uint16_t protocolVersion;
    bool requiresAck;
};

struct UnitMove {
    int32_t unitId;
    int32_t targetX;
    int32_t targetY;
};
constexpr uint16_t UNIT_MOVE_PAYLOAD_SIZE = 12;

struct UnitAttack {
    int32_t attackerId;
    int32_t targetId;
};
constexpr uint16_t UNIT_ATTACK_PAYLOAD_SIZE = 8;

struct Ping {
    uint64_t clientSendTimeUs;
};
constexpr uint16_t PING_PAYLOAD_SIZE = 8;

struct Pong {
    uint64_t clientSendTimeUs;
    uint64_t serverReceiveTimeUs;
    uint64_t serverSendTimeUs;
};
constexpr uint16_t PONG_PAYLOAD_SIZE = 24;

struct Ack {
    uint16_t acknowledgedSequence;
};
constexpr uint16_t ACK_PAYLOAD_SIZE = 2;

enum class ParseError {
    TooShort,         // датаграмма короче заголовка
    UnknownType,      // packetType не из CommandType
    BadVersion,       // protocolVersion != PROTOCOL_VERSION
    LengthMismatch,   // HEADER_SIZE + payloadSize != длина датаграммы
    UnexpectedType,   // тип пакета не тот, что ожидает парсер
    BadPayloadSize,   // payloadSize не совпадает с размером команды
    BadAckFlag        // requiresAck не 0/1
};

inline const char* toString(ParseError error) {
    switch (error) {
        case ParseError::TooShort: return "too short";
        case ParseError::UnknownType: return "unknown packet type";
        case ParseError::BadVersion: return "unsupported protocol version";
        case ParseError::LengthMismatch: return "payload size does not match datagram length";
        case ParseError::UnexpectedType: return "unexpected packet type";
        case ParseError::BadPayloadSize: return "invalid payload size for packet type";
        case ParseError::BadAckFlag: return "invalid requiresAck flag";
    }
    return "unknown error";
}

// ---------------------------------------------------------------------------
// Запись и чтение в сетевом порядке байтов
// ---------------------------------------------------------------------------

inline void WriteU8(std::vector<uint8_t>& out, uint8_t value) {
    out.push_back(value);
}

inline void WriteU16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value));
}

inline void WriteU32(std::vector<uint8_t>& out, uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<uint8_t>(value >> shift));
    }
}

inline void WriteU64(std::vector<uint8_t>& out, uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<uint8_t>(value >> shift));
    }
}

inline void WriteI32(std::vector<uint8_t>& out, int32_t value) {
    WriteU32(out, static_cast<uint32_t>(value));
}

// Каждая функция Read* проверяет, что в [p, end) хватает байтов, и только
// после этого сдвигает p.
inline std::optional<uint8_t> ReadU8(const uint8_t*& p, const uint8_t* end) {
    if (end - p < 1) {
        return std::nullopt;
    }
    return *p++;
}

inline std::optional<uint16_t> ReadU16(const uint8_t*& p, const uint8_t* end) {
    if (end - p < 2) {
        return std::nullopt;
    }
    auto value = static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
    p += 2;
    return value;
}

inline std::optional<uint32_t> ReadU32(const uint8_t*& p, const uint8_t* end) {
    if (end - p < 4) {
        return std::nullopt;
    }
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value = (value << 8) | *p++;
    }
    return value;
}

inline std::optional<uint64_t> ReadU64(const uint8_t*& p, const uint8_t* end) {
    if (end - p < 8) {
        return std::nullopt;
    }
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | *p++;
    }
    return value;
}

inline std::optional<int32_t> ReadI32(const uint8_t*& p, const uint8_t* end) {
    auto value = ReadU32(p, end);
    if (!value) {
        return std::nullopt;
    }
    return static_cast<int32_t>(*value);
}

// ---------------------------------------------------------------------------
// Сериализация
// ---------------------------------------------------------------------------

inline std::vector<uint8_t> serializePacket(
    CommandType type,
    uint16_t sequence,
    const std::vector<uint8_t>& payload,
    bool requiresAck = false
) {
    // Скобки вокруг max защищают от макроса max из <windows.h>.
    if (payload.size() > (std::numeric_limits<uint16_t>::max)() ||
        HEADER_SIZE + payload.size() > MAX_DATAGRAM_SIZE) {
        throw std::length_error("payload does not fit into a datagram");
    }

    std::vector<uint8_t> packet;
    packet.reserve(HEADER_SIZE + payload.size());
    WriteU8(packet, static_cast<uint8_t>(type));
    WriteU16(packet, sequence);
    WriteU16(packet, static_cast<uint16_t>(payload.size()));
    WriteU16(packet, PROTOCOL_VERSION);
    WriteU8(packet, requiresAck ? 1 : 0);
    packet.insert(packet.end(), payload.begin(), payload.end());
    return packet;
}

// Приказы юнитам надёжны: в пошаговой игре каждый приказ отдаётся один раз,
// и потерянный пакет означает, что юнит пропустит ход.
inline std::vector<uint8_t> serializeUnitMove(uint16_t sequence, const UnitMove& command) {
    std::vector<uint8_t> payload;
    payload.reserve(UNIT_MOVE_PAYLOAD_SIZE);
    WriteI32(payload, command.unitId);
    WriteI32(payload, command.targetX);
    WriteI32(payload, command.targetY);
    return serializePacket(CommandType::UNIT_MOVE, sequence, payload, /* requiresAck */ true);
}

inline std::vector<uint8_t> serializeUnitAttack(uint16_t sequence, const UnitAttack& command) {
    std::vector<uint8_t> payload;
    payload.reserve(UNIT_ATTACK_PAYLOAD_SIZE);
    WriteI32(payload, command.attackerId);
    WriteI32(payload, command.targetId);
    return serializePacket(CommandType::UNIT_ATTACK, sequence, payload, /* requiresAck */ true);
}

inline std::vector<uint8_t> serializePing(uint16_t sequence, const Ping& ping) {
    std::vector<uint8_t> payload;
    payload.reserve(PING_PAYLOAD_SIZE);
    WriteU64(payload, ping.clientSendTimeUs);
    return serializePacket(CommandType::PING, sequence, payload);
}

inline std::vector<uint8_t> serializePong(uint16_t sequence, const Pong& pong) {
    std::vector<uint8_t> payload;
    payload.reserve(PONG_PAYLOAD_SIZE);
    WriteU64(payload, pong.clientSendTimeUs);
    WriteU64(payload, pong.serverReceiveTimeUs);
    WriteU64(payload, pong.serverSendTimeUs);
    return serializePacket(CommandType::PONG, sequence, payload);
}

// ACK несёт тот же sequenceNumber, что и подтверждаемый пакет, и сам
// подтверждения не требует: иначе получилась бы бесконечная цепочка ACK на ACK
inline std::vector<uint8_t> serializeAck(uint16_t acknowledgedSequence) {
    std::vector<uint8_t> payload;
    payload.reserve(ACK_PAYLOAD_SIZE);
    WriteU16(payload, acknowledgedSequence);
    return serializePacket(CommandType::ACK, acknowledgedSequence, payload);
}

// Ответ сервера на команду: тот же тип и номер, полезная нагрузка — текст.
inline std::vector<uint8_t> serializeTextResponse(
    CommandType type,
    uint16_t sequence,
    const std::string& text
) {
    return serializePacket(type, sequence, std::vector<uint8_t>(text.begin(), text.end()));
}

// ---------------------------------------------------------------------------
// Разбор
// ---------------------------------------------------------------------------

template <typename T>
using ParseResult = std::variant<T, ParseError>;

// Проверенный пакет. payload указывает внутрь исходной датаграммы, и в нём
// ровно header.payloadSize байт, поэтому датаграмма должна жить дольше Packet.
struct Packet {
    PacketHeader header;
    const uint8_t* payload;
};

// Первый этап разбора: проверяет только заголовок (длину, тип, версию и
// согласованность payloadSize с длиной датаграммы). Содержимое payload
// проверяют parseUnitMove / parseUnitAttack.
inline ParseResult<Packet> parsePacket(const uint8_t* data, size_t size) {
    if (data == nullptr || size < HEADER_SIZE) {
        return ParseError::TooShort;
    }

    const uint8_t* p = data;
    const uint8_t* end = data + size;
    auto type = ReadU8(p, end);
    auto sequence = ReadU16(p, end);
    auto payloadSize = ReadU16(p, end);
    auto version = ReadU16(p, end);
    auto ackFlag = ReadU8(p, end);
    if (!type || !sequence || !payloadSize || !version || !ackFlag) {
        return ParseError::TooShort;
    }

    if (!isKnownCommandType(*type)) {
        return ParseError::UnknownType;
    }
    if (*version != PROTOCOL_VERSION) {
        return ParseError::BadVersion;
    }
    if (*ackFlag > 1 ||
        (*ackFlag == 1 && static_cast<CommandType>(*type) == CommandType::ACK)) {
        return ParseError::BadAckFlag;
    }
    if (HEADER_SIZE + *payloadSize != size) {
        return ParseError::LengthMismatch;
    }

    PacketHeader header{
        static_cast<CommandType>(*type), *sequence, *payloadSize, *version, *ackFlag == 1};
    return Packet{header, p};
}

// Второй этап разбора: payload должен быть ровно UNIT_MOVE_PAYLOAD_SIZE байт.
inline ParseResult<UnitMove> parseUnitMove(const Packet& packet) {
    if (packet.header.type != CommandType::UNIT_MOVE) {
        return ParseError::UnexpectedType;
    }
    if (packet.header.payloadSize != UNIT_MOVE_PAYLOAD_SIZE) {
        return ParseError::BadPayloadSize;
    }

    const uint8_t* p = packet.payload;
    const uint8_t* end = packet.payload + packet.header.payloadSize;
    auto unitId = ReadI32(p, end);
    auto targetX = ReadI32(p, end);
    auto targetY = ReadI32(p, end);
    if (!unitId || !targetX || !targetY || p != end) {
        return ParseError::BadPayloadSize;
    }
    return UnitMove{*unitId, *targetX, *targetY};
}

// Второй этап разбора: payload должен быть ровно UNIT_ATTACK_PAYLOAD_SIZE байт.
inline ParseResult<UnitAttack> parseUnitAttack(const Packet& packet) {
    if (packet.header.type != CommandType::UNIT_ATTACK) {
        return ParseError::UnexpectedType;
    }
    if (packet.header.payloadSize != UNIT_ATTACK_PAYLOAD_SIZE) {
        return ParseError::BadPayloadSize;
    }

    const uint8_t* p = packet.payload;
    const uint8_t* end = packet.payload + packet.header.payloadSize;
    auto attackerId = ReadI32(p, end);
    auto targetId = ReadI32(p, end);
    if (!attackerId || !targetId || p != end) {
        return ParseError::BadPayloadSize;
    }
    return UnitAttack{*attackerId, *targetId};
}

// Второй этап разбора: payload должен быть ровно PING_PAYLOAD_SIZE байт.
inline ParseResult<Ping> parsePing(const Packet& packet) {
    if (packet.header.type != CommandType::PING) {
        return ParseError::UnexpectedType;
    }
    if (packet.header.payloadSize != PING_PAYLOAD_SIZE) {
        return ParseError::BadPayloadSize;
    }

    const uint8_t* p = packet.payload;
    const uint8_t* end = packet.payload + packet.header.payloadSize;
    auto clientSendTimeUs = ReadU64(p, end);
    if (!clientSendTimeUs || p != end) {
        return ParseError::BadPayloadSize;
    }
    return Ping{*clientSendTimeUs};
}

// Второй этап разбора: payload должен быть ровно PONG_PAYLOAD_SIZE байт.
inline ParseResult<Pong> parsePong(const Packet& packet) {
    if (packet.header.type != CommandType::PONG) {
        return ParseError::UnexpectedType;
    }
    if (packet.header.payloadSize != PONG_PAYLOAD_SIZE) {
        return ParseError::BadPayloadSize;
    }

    const uint8_t* p = packet.payload;
    const uint8_t* end = packet.payload + packet.header.payloadSize;
    auto clientSendTimeUs = ReadU64(p, end);
    auto serverReceiveTimeUs = ReadU64(p, end);
    auto serverSendTimeUs = ReadU64(p, end);
    if (!clientSendTimeUs || !serverReceiveTimeUs || !serverSendTimeUs || p != end) {
        return ParseError::BadPayloadSize;
    }
    return Pong{*clientSendTimeUs, *serverReceiveTimeUs, *serverSendTimeUs};
}

// Второй этап разбора: payload должен быть ровно ACK_PAYLOAD_SIZE байт.
inline ParseResult<Ack> parseAck(const Packet& packet) {
    if (packet.header.type != CommandType::ACK) {
        return ParseError::UnexpectedType;
    }
    if (packet.header.payloadSize != ACK_PAYLOAD_SIZE) {
        return ParseError::BadPayloadSize;
    }

    const uint8_t* p = packet.payload;
    const uint8_t* end = packet.payload + packet.header.payloadSize;
    auto acknowledged = ReadU16(p, end);
    if (!acknowledged || p != end) {
        return ParseError::BadPayloadSize;
    }
    return Ack{*acknowledged};
}

// Текст ответа сервера (payload без завершающего нуля).
inline std::string payloadText(const Packet& packet) {
    return std::string(
        reinterpret_cast<const char*>(packet.payload),
        packet.header.payloadSize);
}
