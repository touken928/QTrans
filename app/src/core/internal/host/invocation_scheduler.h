#pragma once

#include "qtrans/core.h"

#include <array>
#include <deque>
#include <memory>

namespace qtrans::core::host_detail {

struct ScheduledInvocation {
    InvocationId id;
    WorkClass work_class;
};

class InvocationScheduler {
public:
    static constexpr std::size_t kCapacity = 64;

    bool push(ScheduledInvocation invocation);
    [[nodiscard]] std::optional<ScheduledInvocation> pop();
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    static std::size_t index(WorkClass work_class);
    std::array<std::deque<ScheduledInvocation>, 4> queues_;
    std::size_t size_ = 0;
    std::size_t next_interactive_ = 0;
    std::size_t interactive_since_batch_ = 0;
};

}  // namespace qtrans::core::host_detail
