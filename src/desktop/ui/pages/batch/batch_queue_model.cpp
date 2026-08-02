#include "ui/pages/batch/batch_queue_model.h"
#include "ui/shared/theme/theme.h"
#include "domain/batch/batch_enums.h"

#include <QColor>
#include <QSet>
#include <QSortFilterProxyModel>

// =============================================================================
// BatchQueueModel
// =============================================================================

BatchQueueModel::BatchQueueModel(QObject *parent)
    : QAbstractTableModel(parent) {
}

int BatchQueueModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : rows_.size();
}

int BatchQueueModel::columnCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant BatchQueueModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size()) {
        return {};
    }
    const Row &row = rows_[index.row()];

    if (role == RoleEntryId) {
        return row.id;
    }

    const QVariantMap &m = row.data;
    switch (index.column()) {
        case ColumnFile:
            if (role == Qt::DisplayRole) {
                return m.value(QStringLiteral("file"));
            }
            if (role == Qt::ToolTipRole || role == Qt::AccessibleTextRole) {
                return m.value(QStringLiteral("file_path"));
            }
            break;
        case ColumnSource:
            if (role == Qt::DisplayRole || role == Qt::ToolTipRole) {
                return m.value(QStringLiteral("source"));
            }
            break;
        case ColumnTarget:
            if (role == Qt::DisplayRole || role == Qt::ToolTipRole) {
                return m.value(QStringLiteral("target"));
            }
            break;
        case ColumnState: {
            const int state = m.value(QStringLiteral("state")).toInt();
            if (role == RoleState) {
                return state;
            }
            if (role == Qt::DisplayRole || role == Qt::ToolTipRole) {
                switch (static_cast<BatchEntryState>(state)) {
                    case BatchEntryState::Queued:
                        return QStringLiteral("Queued");
                    case BatchEntryState::Processing:
                        return QStringLiteral("Translating");
                    case BatchEntryState::Completed:
                        return QStringLiteral("Done");
                    case BatchEntryState::Failed:
                        return QStringLiteral("Failed");
                    case BatchEntryState::Cancelled:
                        return QStringLiteral("Cancelled");
                }
                return QStringLiteral("Unknown");
            }
            if (role == Qt::ForegroundRole) {
                switch (static_cast<BatchEntryState>(state)) {
                    case BatchEntryState::Completed:
                        return QColor(Theme::Color::success);
                    case BatchEntryState::Failed:
                        return QColor(Theme::Color::error);
                    case BatchEntryState::Processing:
                        return QColor(Theme::Color::primary);
                    default:
                        break;
                }
            }
            break;
        }
        case ColumnProgress: {
            const int done = m.value(QStringLiteral("segments_done")).toInt();
            const int total = m.value(QStringLiteral("segments_total")).toInt();
            if (role == RoleProgress) {
                return done;
            }
            if (role == RoleTotal) {
                return total;
            }
            if (role == Qt::DisplayRole || role == Qt::ToolTipRole) {
                if (total <= 0) {
                    return QStringLiteral("\u2014");
                }
                return QStringLiteral("%1 / %2").arg(done).arg(total);
            }
            break;
        }
        default:
            break;
    }

    if (role == RoleState) {
        return m.value(QStringLiteral("state"));
    }
    if (role == RoleProgress) {
        return m.value(QStringLiteral("segments_done"));
    }
    if (role == RoleTotal) {
        return m.value(QStringLiteral("segments_total"));
    }
    if (role == RoleFilePath) {
        return m.value(QStringLiteral("file_path"));
    }
    if (role == RoleSavePath) {
        return m.value(QStringLiteral("save_path"));
    }
    if (role == RoleSaved) {
        return m.value(QStringLiteral("saved"));
    }
    return {};
}

QVariant BatchQueueModel::headerData(int section, Qt::Orientation orientation,
                                     int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    switch (section) {
        case ColumnFile:
            return QStringLiteral("File");
        case ColumnSource:
            return QStringLiteral("Source");
        case ColumnTarget:
            return QStringLiteral("Target");
        case ColumnState:
            return QStringLiteral("State");
        case ColumnProgress:
            return QStringLiteral("Progress");
        default:
            return {};
    }
}

void BatchQueueModel::applySnapshot(const QVariantList &entries) {
    // Incoming entries arrive in durable queue order; dedupe defensively.
    QVector<QVariantMap> incoming;
    incoming.reserve(entries.size());
    QSet<QString> incoming_ids;
    for (const QVariant &value : entries) {
        const QVariantMap map = value.toMap();
        const QString id = map.value(QStringLiteral("id")).toString();
        if (id.isEmpty() || incoming_ids.contains(id)) {
            continue;
        }
        incoming_ids.insert(id);
        incoming.append(map);
    }

    // 1. Drop rows whose id no longer exists (remove from the end so earlier
    //    indexes stay valid).
    for (int row = rows_.size() - 1; row >= 0; --row) {
        if (!incoming_ids.contains(rows_[row].id)) {
            removeRowAt(row);
        }
    }

    // 2. Update surviving rows in place and insert new ids at their durable
    //    position. `anchor` tracks the row position of the last processed
    //    incoming entry; existing rows keep their relative order, so the
    //    anchor always advances monotonically.
    int anchor = 0;
    for (const QVariantMap &map : incoming) {
        const QString id = map.value(QStringLiteral("id")).toString();
        const int existing = rowIndexOfId(id);
        if (existing >= 0) {
            if (rows_[existing].data != map) {
                rows_[existing].data = map;
                emit dataChanged(index(existing, 0),
                                 index(existing, ColumnCount - 1));
            }
            anchor = existing + 1;
        } else {
            insertRowAt(anchor, map);
            ++anchor;
        }
    }
    rebuildIdIndex();
}

QStringList BatchQueueModel::entryIds() const {
    QStringList ids;
    ids.reserve(rows_.size());
    for (const Row &row : rows_) {
        ids.append(row.id);
    }
    return ids;
}

QVariantMap BatchQueueModel::entryData(const QString &entry_id) const {
    const int row = rowIndexOfId(entry_id);
    return row >= 0 ? rows_[row].data : QVariantMap{};
}

int BatchQueueModel::rowIndexOfId(const QString &entry_id) const {
    return id_index_.value(entry_id, -1);
}

void BatchQueueModel::removeRowAt(int row) {
    beginRemoveRows(QModelIndex(), row, row);
    rows_.remove(row);
    endRemoveRows();
    // The id map must stay correct across every structural change: the
    // applySnapshot pass resolves row indexes through it while further
    // removals/inserts are still pending.
    rebuildIdIndex();
}

void BatchQueueModel::insertRowAt(int row, const QVariantMap &data) {
    beginInsertRows(QModelIndex(), row, row);
    Row entry;
    entry.id = data.value(QStringLiteral("id")).toString();
    entry.data = data;
    rows_.insert(row, entry);
    endInsertRows();
    rebuildIdIndex();
}

void BatchQueueModel::rebuildIdIndex() {
    id_index_.clear();
    id_index_.reserve(rows_.size());
    for (int i = 0; i < rows_.size(); ++i) {
        id_index_.insert(rows_[i].id, i);
    }
}

// =============================================================================
// BatchQueueSortProxy
// =============================================================================

BatchQueueSortProxy::BatchQueueSortProxy(QObject *parent)
    : QSortFilterProxyModel(parent) {
}

bool BatchQueueSortProxy::lessThan(const QModelIndex &left,
                                   const QModelIndex &right) const {
    const int column = left.column();
    if (column == BatchQueueModel::ColumnState) {
        const int left_state =
            sourceModel()->data(left, BatchQueueModel::RoleState).toInt();
        const int right_state =
            sourceModel()->data(right, BatchQueueModel::RoleState).toInt();
        if (left_state != right_state) {
            return left_state < right_state;
        }
        // Ties fall back to the file name so the order is stable.
        return sourceModel()->data(left, Qt::DisplayRole).toString() <
               sourceModel()->data(right, Qt::DisplayRole).toString();
    }
    if (column == BatchQueueModel::ColumnProgress) {
        const int left_done =
            sourceModel()->data(left, BatchQueueModel::RoleProgress).toInt();
        const int right_done =
            sourceModel()->data(right, BatchQueueModel::RoleProgress).toInt();
        const int left_total =
            sourceModel()->data(left, BatchQueueModel::RoleTotal).toInt();
        const int right_total =
            sourceModel()->data(right, BatchQueueModel::RoleTotal).toInt();
        // Cross-multiply to compare fractions exactly; empty progress sorts
        // last (empty fraction 0/0 is treated as "no work yet").
        const qint64 left_fraction = left_total > 0 ? left_done * 100000LL / left_total : -1;
        const qint64 right_fraction = right_total > 0 ? right_done * 100000LL / right_total : -1;
        if (left_fraction != right_fraction) {
            return left_fraction < right_fraction;
        }
        return sourceModel()->data(left, Qt::DisplayRole).toString() <
               sourceModel()->data(right, Qt::DisplayRole).toString();
    }
    return QSortFilterProxyModel::lessThan(left, right);
}
