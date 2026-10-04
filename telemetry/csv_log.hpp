#pragma once

#include "telemetry/telemetry.hpp"

#include <ostream>
#include <string>
#include <vector>

// Строки журнала: experiment_id;sample;sequence;sent_at_ms;rtt_ms;srtt_ms;status
// sent_at_ms отсчитывается от отправки первого PING серии.
void WriteCsvHeader(std::ostream& out);
void WriteCsvRows(std::ostream& out, const std::vector<Sample>& samples);

// Дописывает серию в файл; заголовок пишется, только если файл пуст.
bool AppendCsvFile(const std::string& path, const std::vector<Sample>& samples);