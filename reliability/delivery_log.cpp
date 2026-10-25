#include "reliability/delivery_log.hpp"

#include <fstream>
#include <iomanip>

const char* toString(DeliveryStatus status) {
    switch (status) {
        case DeliveryStatus::Pending: return "pending";
        case DeliveryStatus::Delivered: return "delivered";
        case DeliveryStatus::Failed: return "failed";
    }
    return "unknown";
}

void WriteDeliveryCsvHeader(std::ostream& out) {
    out << "experiment_id;sequence;command;attempts;first_sent_ms;finished_ms;"
           "time_to_ack_ms;rto_ms;srtt_ms;status\n";
}

void WriteDeliveryCsvRows(std::ostream& out, const std::vector<DeliveryRecord>& records) {
    if (records.empty()) {
        return;
    }
    const std::uint64_t startUs = records.front().firstSentUs;

    out << std::fixed << std::setprecision(3);
    for (const DeliveryRecord& record : records) {
        out << record.experimentId << ';'
            << record.sequence << ';'
            << record.command << ';'
            << record.attempts << ';'
            << (record.firstSentUs - startUs) / 1000.0 << ';';
        if (record.status != DeliveryStatus::Pending) {
            out << (record.finishedUs - startUs) / 1000.0;
        }
        out << ';';
        if (record.status == DeliveryStatus::Delivered) {
            out << (record.finishedUs - record.firstSentUs) / 1000.0;
        }
        out << ';' << record.rtoMs
            << ';' << record.srttMs
            << ';' << toString(record.status) << '\n';
    }
}

bool AppendDeliveryCsv(const std::string& path, const std::vector<DeliveryRecord>& records) {
    bool needHeader = true;
    {
        std::ifstream existing(path);
        needHeader = !existing || existing.peek() == std::ifstream::traits_type::eof();
    }

    std::ofstream out(path, std::ios::app);
    if (!out) {
        return false;
    }
    if (needHeader) {
        WriteDeliveryCsvHeader(out);
    }
    WriteDeliveryCsvRows(out, records);
    return static_cast<bool>(out);
}