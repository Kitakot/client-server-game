#include "protocol/protocol.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

ParseResult<Ping> parsePingBytes(const std::vector<uint8_t>& bytes) {
    auto packet = parsePacket(bytes.data(), bytes.size());
    if (auto* error = std::get_if<ParseError>(&packet)) {
        return *error;
    }
    return parsePing(std::get<Packet>(packet));
}

bool failsWith(const ParseResult<Ping>& result, ParseError expected) {
    auto* error = std::get_if<ParseError>(&result);
    return error != nullptr && *error == expected;
}

}  // namespace

TEST_CASE("PING is written in network byte order") {
    auto bytes = serializePing(0x0102, Ping{0x1122334455667788ULL});
    const std::vector<uint8_t> expected = {
        0x03,                    // packetType = PING
        0x01, 0x02,              // sequenceNumber
        0x00, 0x08,              // payloadSize
        0x00, 0x01,              // protocolVersion
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    REQUIRE(bytes == expected);
}

TEST_CASE("PING round trip") {
    auto bytes = serializePing(17, Ping{987654321ULL});
    auto ping = parsePingBytes(bytes);
    REQUIRE(std::holds_alternative<Ping>(ping));
    REQUIRE(std::get<Ping>(ping).clientSendTimeUs == 987654321ULL);
}

TEST_CASE("PONG round trip") {
    auto bytes = serializePong(65535, Pong{1, 2, 3});
    REQUIRE(bytes.size() == HEADER_SIZE + PONG_PAYLOAD_SIZE);

    auto packet = parsePacket(bytes.data(), bytes.size());
    REQUIRE(std::holds_alternative<Packet>(packet));
    REQUIRE(std::get<Packet>(packet).header.sequence == 65535);

    auto pong = parsePong(std::get<Packet>(packet));
    REQUIRE(std::holds_alternative<Pong>(pong));
    REQUIRE(std::get<Pong>(pong).clientSendTimeUs == 1);
    REQUIRE(std::get<Pong>(pong).serverReceiveTimeUs == 2);
    REQUIRE(std::get<Pong>(pong).serverSendTimeUs == 3);
}

TEST_CASE("Datagram shorter than header is rejected") {
    const std::vector<uint8_t> bytes = {0x03, 0x00, 0x01};
    REQUIRE(failsWith(parsePingBytes(bytes), ParseError::TooShort));
}

TEST_CASE("Truncated PING is rejected") {
    auto bytes = serializePing(17, Ping{100});
    bytes.pop_back();
    REQUIRE(failsWith(parsePingBytes(bytes), ParseError::LengthMismatch));
}

TEST_CASE("PING with trailing bytes is rejected") {
    auto bytes = serializePing(17, Ping{100});
    bytes.push_back(0xFF);
    REQUIRE(failsWith(parsePingBytes(bytes), ParseError::LengthMismatch));
}

TEST_CASE("Unknown packet type is rejected") {
    auto bytes = serializePing(17, Ping{100});
    bytes[0] = 0x7F;
    REQUIRE(failsWith(parsePingBytes(bytes), ParseError::UnknownType));
}

TEST_CASE("Wrong protocol version is rejected") {
    auto bytes = serializePing(17, Ping{100});
    bytes[6] = 0x02;
    REQUIRE(failsWith(parsePingBytes(bytes), ParseError::BadVersion));
}

TEST_CASE("PONG is not accepted as PING") {
    auto bytes = serializePong(17, Pong{1, 2, 3});
    REQUIRE(failsWith(parsePingBytes(bytes), ParseError::UnexpectedType));
}

TEST_CASE("PING with wrong payload size is rejected") {
    // Заголовок согласован с длиной датаграммы, но payload не 8 байт.
    auto bytes = serializePacket(CommandType::PING, 17, {0x01, 0x02, 0x03, 0x04});
    REQUIRE(failsWith(parsePingBytes(bytes), ParseError::BadPayloadSize));
}