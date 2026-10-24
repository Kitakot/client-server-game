#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

enum class DeliveryStatus { Pending, Delivered, Failed };

const char* toString(DeliveryStatus status);

// Судьба одного надёжного приказа за серию.
struct DeliveryRecord {
    std::string experimentId;
    std::uint16_t sequence = 0;
    std::string command;             // UNIT_MOVE / UNIT_ATTACK
    std::uint64_t firstSentUs = 0;
    std::uint64_t finishedUs = 0;    // момент ACK или ухода в failed
    std::uint32_t attempts = 1;
    double rtoMs = 0;                // RTO в момент завершения
    double srttMs = 0;
    DeliveryStatus status = DeliveryStatus::Pending;
};

// Строки журнала:
// experiment_id;sequence;command;attempts;first_sent_ms;finished_ms;time_to_ack_ms;rto_ms;srtt_ms;status
// Время отсчитывается от первой отправки в серии; time_to_ack_ms пуст у
// недоставленных приказов.
void WriteDeliveryCsvHeader(std::ostream& out);
void WriteDeliveryCsvRows(std::ostream& out, const std::vector<DeliveryRecord>& records);

// Дописывает серию в файл; заголовок пишется, только если файл пуст.
bool AppendDeliveryCsv(const std::string& path, const std::vector<DeliveryRecord>& records);