#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2ipdef.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

enum class CommandType : uint16_t {
    UNIT_MOVE = 1,
    UNIT_ATTACK = 2
};

#pragma pack(push, 1)
struct PacketHeader {
    uint16_t type;
    uint32_t sequence;
    uint32_t payloadSize;
};
#pragma pack(pop)

struct UnitMove {
    int32_t unitId;
    int32_t targetX;
    int32_t targetY;
};

struct UnitAttack {
    int32_t attackerId;
    int32_t targetId;
};

inline std::vector<char> makePacket(
    CommandType type,
    uint32_t sequence,
    const void* payload,
    uint32_t payloadSize
) {
    PacketHeader header{
        htons(static_cast<uint16_t>(type)),
        htonl(sequence),
        htonl(payloadSize)
    };

    std::vector<char> packet(sizeof(header) + payloadSize);
    std::memcpy(packet.data(), &header, sizeof(header));
    if (payloadSize > 0) {
        std::memcpy(packet.data() + sizeof(header), payload, payloadSize);
    }

    return packet;
}