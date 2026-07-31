#include "invocation_scheduler.h"

namespace qtrans::core::host_detail {

std::size_t InvocationScheduler::index(WorkClass work_class) {
    return static_cast<std::size_t>(work_class);
}

bool InvocationScheduler::push(ScheduledInvocation invocation) {
    if (size_ >= kCapacity) return false;
    queues_[index(invocation.work_class)].push_back(invocation);
    ++size_;
    return true;
}

std::optional<ScheduledInvocation> InvocationScheduler::pop() {
    if (size_ == 0) return std::nullopt;
    if (!queues_[index(WorkClass::Batch)].empty() && interactive_since_batch_ >= 3) {
        auto value = queues_[index(WorkClass::Batch)].front();
        queues_[index(WorkClass::Batch)].pop_front();
        --size_;
        interactive_since_batch_ = 0;
        return value;
    }
    for (std::size_t offset = 0; offset < 3; ++offset) {
        const std::size_t candidate = (next_interactive_ + offset) % 3;
        if (!queues_[candidate].empty()) {
            next_interactive_ = (candidate + 1) % 3;
            auto value = queues_[candidate].front();
            queues_[candidate].pop_front();
            --size_;
            ++interactive_since_batch_;
            return value;
        }
    }
    if (!queues_[index(WorkClass::Batch)].empty()) {
        auto value = queues_[index(WorkClass::Batch)].front();
        queues_[index(WorkClass::Batch)].pop_front();
        --size_;
        interactive_since_batch_ = 0;
        return value;
    }
    return std::nullopt;
}

bool InvocationScheduler::empty() const noexcept {
    return size_ == 0;
}

std::size_t InvocationScheduler::size() const noexcept {
    return size_;
}

}  // namespace qtrans::core::host_detail
