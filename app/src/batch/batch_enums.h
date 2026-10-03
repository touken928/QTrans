#pragma once

// Batch domain type identifiers. Qt-free.

enum class BatchFileType {
    Unknown = -1,
    PlainText = 0,
    Txt = 1,
    Md = 2,
    Srt = 3,
};

enum class BatchEntryState {
    Queued = 0,
    Processing = 1,
    Completed = 2,
    Failed = 3,
    Cancelled = 4,
};

enum class BatchSegmentState {
    Pending = 0,
    Completed = 1,
    Failed = 2,
};
