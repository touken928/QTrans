#pragma once

#include <QAbstractTableModel>
#include <QHash>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QVector>
#include <QVariantList>
#include <QVariantMap>

// QAbstractTableModel over the durable batch queue. Rows mirror the queue
// file order (insertion order) and are keyed by the stable durable entry id,
// so the underlying order never changes while the user works.
//
// The model is driven by complete queue snapshots from BatchController
// (queueSnapshot). applySnapshot() diffs against the current rows and only
// emits structural changes (insert/remove) for ids that actually appeared or
// disappeared; rows that survive keep their identity, so table selection and
// scroll position survive state updates. Sorting is display-only and
// delegated to BatchQueueSortProxy, which never reorders the source rows.
class BatchQueueModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        ColumnFile = 0,
        ColumnSource,
        ColumnTarget,
        ColumnState,
        ColumnProgress,
        ColumnCount,
    };

    enum Role {
        RoleEntryId = Qt::UserRole,
        RoleState,     // int BatchEntryState
        RoleProgress,  // int segments completed
        RoleTotal,     // int segments total
        RoleFilePath,  // QString
        RoleSavePath,  // QString
        RoleSaved,     // bool
    };

    explicit BatchQueueModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    // Replace the queue contents with the given snapshot. Entries arrive in
    // durable queue order; stable ids keep their row (selection survives),
    // added ids insert, missing ids are removed.
    void applySnapshot(const QVariantList &entries);

    QStringList entryIds() const;
    QVariantMap entryData(const QString &entry_id) const;
    int rowIndexOfId(const QString &entry_id) const;

private:
    struct Row {
        QString id;
        QVariantMap data;
    };

    void removeRowAt(int row);
    void insertRowAt(int row, const QVariantMap &data);
    void rebuildIdIndex();

    QVector<Row> rows_;
    QHash<QString, int> id_index_;
};

// Display-only sorter. Numeric columns (state, progress) compare their typed
// sort roles so "Queued < Processing < Completed" and progress fractions sort
// truthfully; everything else compares display text.
class BatchQueueSortProxy : public QSortFilterProxyModel {
    Q_OBJECT

public:
    explicit BatchQueueSortProxy(QObject *parent = nullptr);

protected:
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;
};
