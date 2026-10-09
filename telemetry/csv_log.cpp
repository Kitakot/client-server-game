#include "telemetry/csv_log.hpp"

#include <fstream>
#include <iomanip>

void WriteCsvHeader(std::ostream& out) {
    out << "experiment_id;sample;sequence;sent_at_ms;rtt_ms;srtt_ms;status\n";
}

void WriteCsvRows(std::ostream& out, const std::vector<Sample>& samples) {
    if (samples.empty()) {
        return;
    }
    const std::uint64_t startUs = samples.front().sentUs;

    out << std::fixed << std::setprecision(3);
    for (const Sample& sample : samples) {
        out << sample.experimentId << ';'
            << sample.index << ';'
            << sample.sequence << ';'
            << (sample.sentUs - startUs) / 1000.0 << ';';
        // У потерянных измерений rtt_ms и srtt_ms остаются пустыми.
        if (sample.rttMs) {
            out << *sample.rttMs;
        }
        out << ';';
        if (sample.srttMs) {
            out << *sample.srttMs;
        }
        out << ';' << toString(sample.status) << '\n';
    }
}

bool AppendCsvFile(const std::string& path, const std::vector<Sample>& samples) {
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
        WriteCsvHeader(out);
    }
    WriteCsvRows(out, samples);
    return static_cast<bool>(out);
}