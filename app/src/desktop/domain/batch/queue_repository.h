#pragma once

#include "domain/batch/batch_types.h"

#include <string>
#include <vector>

// Persistence port for the batch domain. BatchStore is the file-backed
// implementation; orchestration can depend on this contract in tests or in a
// future database implementation without acquiring file-format concerns.
class QueueRepository {
public:
    virtual ~QueueRepository() = default;

    virtual std::vector<BatchEntry> load() const = 0;
    virtual void save(const std::vector<BatchEntry> &entries) = 0;
    virtual void append(const BatchEntry &entry) = 0;
    virtual void remove(const std::string &entry_id) = 0;
    virtual void update_entry_state(const std::string &entry_id,
                                    BatchEntryState state) = 0;
    virtual void update_segment_state(const std::string &entry_id,
                                      int segment_index,
                                      BatchSegmentState state) = 0;
    virtual void update_segment_translated(
        const std::string &entry_id, int segment_index,
        const std::string &translated_text) = 0;
    virtual bool reset_for_retry(const std::string &entry_id) = 0;
};
