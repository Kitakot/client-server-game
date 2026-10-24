#include "reliability/dedup_window.hpp"

bool DedupWindow::MarkProcessed(std::uint16_t sequence) {
    if (!seen_.insert(sequence).second) {
        return false;
    }
    order_.push_back(sequence);
    while (order_.size() > capacity_) {
        seen_.erase(order_.front());
        order_.pop_front();
    }
    return true;
}