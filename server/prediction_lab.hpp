#pragma once

#include "protocol/replication_lab.hpp"
#include "transport/clock.hpp"
#include "transport/udp_socket.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

class ReplicationLab {
public:
    ReplicationLab(
        const std::string& modeName,
        const std::string& csvPath)
        : modeName_(modeName),
          mode_(replication::ParseMode(modeName)),
          world_(replication::InitialWorld()),
          csv_(csvPath)
    {
        if (!socket_.IsOpen() || !socket_.Bind(replication::PORT)) {
            throw std::runtime_error("Cannot bind replication UDP port 54020");
        }

        if (!csv_) {
            throw std::runtime_error("Cannot open replication CSV");
        }

        csv_ << "mode,event,time_s,client,object_id,kind,bytes\n";

        std::cout
            << "[REPLICATION] mode=" << modeName_
            << " port=" << replication::PORT
            << " waiting for two clients\n";
    }

    void Tick() {
        Receive();

        if (!started_ || finished_) {
            return;
        }

        const uint64_t now = NowUs();
        const uint64_t elapsed = now - startedAt_;

        if (elapsed >= 60'000'000) {
            bool converged = true;

            for (const auto& peer : peers_) {
                for (const auto& entity : world_) {
                    if (peer->acked[entity.id] != entity.revision) {
                        converged = false;
                    }
                }
            }

            csv_
                << modeName_ << ","
                << (converged ? "end_ok" : "end_unconfirmed")
                << ",60,-1,-1,-1,0\n";

            csv_.flush();
            finished_ = true;

            std::cout
                << "[REPLICATION] finished, final state acknowledged="
                << converged << "\n";

            return;
        }

        const int targetTurn =
            static_cast<int>((std::min)(elapsed / 1'000'000, uint64_t{55}));

        while (simulatedTurn_ < targetTurn) {
            ++simulatedTurn_;
            AdvanceTurn(simulatedTurn_);
        }

        if (now < nextPublish_) {
            return;
        }

        nextPublish_ = now + 100'000;

        for (int client = 0; client < 2; ++client) {
            Peer& peer = *peers_[client];

            for (const auto& entity : world_) {
                const int id = entity.id;

                if (mode_.dirtyOnly) {
                    if (peer.acked[id] == entity.revision) {
                        continue;
                    }

                    const bool sameVersionAlreadySent =
                        peer.sentRevision[id] == entity.revision;

                    if (sameVersionAlreadySent &&
                        now - peer.lastSentAt[id] < 300'000) {
                        continue;
                    }
                }

                auto bytes = replication::EncodeEntity(
                    entity, mode_.compact);

                if (!socket_.SendTo(peer.endpoint, bytes)) {
                    continue;
                }

                peer.sentRevision[id] = entity.revision;
                peer.lastSentAt[id] = now;

                csv_
                    << modeName_ << ",send,"
                    << elapsed / 1'000'000.0 << ","
                    << client << ","
                    << id << ","
                    << static_cast<int>(entity.kind) << ","
                    << bytes.size() << "\n";
            }
        }
    }

private:
    struct Peer {
        Endpoint endpoint;
        std::array<uint32_t, replication::OBJECT_COUNT> acked{};
        std::array<uint32_t, replication::OBJECT_COUNT> sentRevision{};
        std::array<uint64_t, replication::OBJECT_COUNT> lastSentAt{};
    };

    int FindPeer(const Endpoint& endpoint) const {
        for (int i = 0; i < 2; ++i) {
            if (peers_[i] && peers_[i]->endpoint == endpoint) {
                return i;
            }
        }

        return -1;
    }

    void Receive() {
        for (int count = 0; count < 256; ++count) {
            uint8_t buffer[MAX_DATAGRAM_SIZE];
            Endpoint from{};

            auto size = socket_.ReceiveFrom(
                buffer,
                sizeof(buffer),
                from,
                std::chrono::milliseconds(0));

            if (!size) {
                break;
            }

            auto packet = replication::Parse(buffer, *size);

            if (!packet) {
                continue;
            }

            int client = FindPeer(from);

            if (packet->type == replication::HELLO &&
                packet->size == 0) {

                if (client < 0 && !started_) {
                    for (int i = 0; i < 2; ++i) {
                        if (!peers_[i]) {
                            peers_[i] = Peer{from};
                            client = i;

                            std::cout
                                << "[REPLICATION] client=" << i
                                << " endpoint=" << ToString(from) << "\n";

                            break;
                        }
                    }
                }

                if (!started_ && peers_[0] && peers_[1]) {
                    started_ = true;
                    startedAt_ = NowUs();
                    nextPublish_ = startedAt_;

                    csv_ << modeName_ << ",start,0,-1,-1,-1,0\n";
                    std::cout << "[REPLICATION] measurement started\n";
                }

                continue;
            }

            if (client < 0 || finished_) {
                continue;
            }

            auto ack = replication::DecodeAck(*packet);

            if (!ack) {
                continue;
            }

            Peer& peer = *peers_[client];

            // Подтверждать можно только уже отправленную версию.
            if (ack->revision <= peer.sentRevision[ack->id]) {
                peer.acked[ack->id] = (std::max)(
                    peer.acked[ack->id],
                    ack->revision);
            }
        }
    }

    void AdvanceTurn(int turn) {
        // Восемь юнитов двигаются раз в условный ход.
        for (int i = replication::STATIC_COUNT;
             i < replication::OBJECT_COUNT;
             ++i) {

            world_[i].x = static_cast<uint8_t>(
                (i % 8 + turn) % 16);

            ++world_[i].revision;
        }

        // Проверка пробуждения ранее статичного объекта.
        if (turn % 10 == 0) {
            const int id = turn / 10 - 1;
            world_[id].y = static_cast<uint8_t>(
                world_[id].y + 1);

            ++world_[id].revision;
        }
    }

    std::string modeName_;
    replication::Mode mode_;

    UdpSocket socket_;
    std::array<replication::Entity, replication::OBJECT_COUNT> world_;
    std::array<std::optional<Peer>, 2> peers_;

    std::ofstream csv_;

    bool started_ = false;
    bool finished_ = false;

    uint64_t startedAt_ = 0;
    uint64_t nextPublish_ = 0;
    int simulatedTurn_ = 0;
};