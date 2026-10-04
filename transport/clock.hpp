#pragma once

#include <chrono>
#include <cstdint>

// Микросекунды по монотонным часам. Используется для измерения RTT и синхронизации времени между клиентом и сервером.
inline std::uint64_t NowUs() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(duration_cast<microseconds>
        (steady_clock::now().time_since_epoch()).count());
}