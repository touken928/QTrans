#pragma once

#include "domain/batch/batch_types.h"

#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

// Durable batch queue store with atomic persistence semantics.
// Thread-safe: all public methods acquire an internal mutex.
class BatchStore {
public:
    explicit BatchStore(std::filesystem::path queue_file);

    // Load all entries from the queue file. Returns empty vector if file does
    // not exist or is unreadable.
    std::vector<BatchEntry> load() const;

    // Replace the entire queue with the given entries (atomic write).
    void save(const std::vector<BatchEntry> &entries);

    // Convenience: load, append one entry, save.
    void append(const BatchEntry &entry);

    // Convenience: load, remove entry by id, save.
    void remove(const std::string &entry_id);

    // Convenience: load, update entry state, save.
    void update_entry_state(const std::string &entry_id, BatchEntryState state);

    // Convenience: load, update a single segment's state within an entry, save.
    void update_segment_state(const std::string &entry_id, int segment_index,
                              BatchSegmentState state);

    // Convenience: load, update a single segment's translated text, save.
    void update_segment_translated(const std::string &entry_id, int segment_index,
                                   const std::string &translated_text);

    // Convenience: load, reset one failed entry for a retry. Every Failed
    // segment becomes Pending again and the entry state returns to Queued so
    // a later batch run re-attempts it. Returns true when the entry was found
    // in the Failed state and was reset; any other entry (or an unknown id)
    // is left untouched and false is returned.
    bool reset_for_retry(const std::string &entry_id);

    // Startup recovery: entries abandoned in the Processing state by an
    // interrupted run return to Queued so a restart picks them up again.
    // Completed segment checkpoints (and translated text) are preserved.
    // Persists only when something changed; returns the number of entries
    // that were normalized.
    std::size_t recover_abandoned_processing();

    // Startup recovery: duplicate entry ids (e.g. from a pre-collision-era
    // queue) are rewritten to unique ids so no UI projection ever collapses
    // two entries onto one row. Queue order is preserved; returns the number
    // of entries whose id changed.
    std::size_t repair_duplicate_ids();

private:
    std::filesystem::path queue_file_;
    mutable std::mutex mutex_;

    // Assumes mutex_ is already held.
    std::vector<BatchEntry> load_unlocked();

    static std::string serialize(const std::vector<BatchEntry> &entries);
    static std::vector<BatchEntry> deserialize(const std::string &content);
    static void atomic_write(const std::filesystem::path &path,
                             const std::string &content);
};
