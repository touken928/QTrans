#pragma once

#include "qtrans/core.h"

namespace qtrans::core::host_detail {

struct StopOutcome {
    FinishReason finish_reason = FinishReason::Completed;
    Failure failure;
};

inline StopOutcome map_stop_reason(StopReason reason) {
    switch (reason) {
        case StopReason::Shutdown:
            return {FinishReason::Cancelled, {FailureCode::Shutdown, "invocation stopped by shutdown"}};
        case StopReason::UserCancel:
            return {FinishReason::Cancelled, {FailureCode::Cancelled, "invocation cancelled"}};
        case StopReason::Deadline:
            return {FinishReason::Deadline, {FailureCode::Deadline, "invocation deadline reached"}};
        case StopReason::Preempted:
            return {FinishReason::Preempted, {FailureCode::Cancelled, "invocation preempted"}};
        case StopReason::None:
            return {};
    }
    return {FinishReason::Failed, {FailureCode::Runtime, "unknown stop reason"}};
}

}  // namespace qtrans::core::host_detail
