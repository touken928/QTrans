#include "ui/pages/batch/batch_queue_model.h"
#include "batch/batch_enums.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QItemSelection>
#include <QItemSelectionModel>

namespace {

// Build one snapshot entry for a stable id.
BatchEntryView entry(const QString &id) {
    BatchEntryView view;
    view.id = id;
    view.file = id + QStringLiteral(".txt");
    view.file_path = QStringLiteral("/tmp/") + id + QStringLiteral(".txt");
    view.source = QStringLiteral("Auto");
    view.target = QStringLiteral("English");
    view.state = static_cast<int>(BatchEntryState::Queued);
    view.segments_done = 0;
    view.segments_total = 1;
    view.completed = false;
    view.saved = false;
    view.save_path = QString{};
    return view;
}

QVector<BatchEntryView> snapshotOf(const QStringList &ids) {
    QVector<BatchEntryView> list;
    list.reserve(ids.size());
    for (const QString &id : ids) {
        list.append(entry(id));
    }
    return list;
}

int rowOf(BatchQueueModel &model, const QString &id) {
    return model.rowIndexOfId(id);
}

QStringList idsOf(BatchQueueModel &model) {
    return model.entryIds();
}

}  // namespace

TEST(BatchQueueModel, MiddleRemovalKeepsSurvivorRowsAndSelection) {
    int argc = 1;
    char name[] = "batch-queue-model-middle-removal";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);

    BatchQueueModel model;
    QItemSelectionModel selection(&model);
    model.applySnapshot(snapshotOf({"a", "b", "c", "d"}));
    ASSERT_EQ(model.rowCount(), 4);

    // Select the survivor that sits after the removed middle row.
    selection.select(QItemSelection(model.index(2, 0),
                                    model.index(2, BatchQueueModel::ColumnCount - 1)),
                     QItemSelectionModel::ClearAndSelect);
    ASSERT_TRUE(selection.isRowSelected(2, QModelIndex()));

    // Remove the middle entry (b): the survivor must keep its identity,
    // land on the corrected row, and stay selected.
    model.applySnapshot(snapshotOf({"a", "c", "d"}));

    EXPECT_EQ(idsOf(model), QStringList({"a", "c", "d"}));
    EXPECT_EQ(rowOf(model, QStringLiteral("c")), 1);
    EXPECT_TRUE(selection.isRowSelected(1, QModelIndex()));
    // The surviving row still carries its own data, not a stale neighbour's.
    EXPECT_EQ(model.entryData(QStringLiteral("c")).file, QStringLiteral("c.txt"));
}

TEST(BatchQueueModel, InsertBeforeSurvivorMaintainsOrderAndSelection) {
    int argc = 1;
    char name[] = "batch-queue-model-insert-before-survivor";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);

    BatchQueueModel model;
    QItemSelectionModel selection(&model);
    model.applySnapshot(snapshotOf({"a", "c"}));
    ASSERT_EQ(model.rowCount(), 2);

    selection.select(QItemSelection(model.index(1, 0),
                                    model.index(1, BatchQueueModel::ColumnCount - 1)),
                     QItemSelectionModel::ClearAndSelect);
    ASSERT_TRUE(selection.isRowSelected(1, QModelIndex()));

    // Insert a new entry before the survivor: durable order is preserved,
    // the survivor shifts down and stays selected.
    model.applySnapshot(snapshotOf({"a", "b", "c"}));

    EXPECT_EQ(idsOf(model), QStringList({"a", "b", "c"}));
    EXPECT_EQ(rowOf(model, QStringLiteral("c")), 2);
    EXPECT_TRUE(selection.isRowSelected(2, QModelIndex()));
}

TEST(BatchQueueModel, CombinedMiddleRemovalAndInsertKeepsSurvivorsStable) {
    int argc = 1;
    char name[] = "batch-queue-model-combined-diff";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);

    BatchQueueModel model;
    QItemSelectionModel selection(&model);
    model.applySnapshot(snapshotOf({"a", "b", "c", "d"}));

    selection.select(QItemSelection(model.index(3, 0),
                                    model.index(3, BatchQueueModel::ColumnCount - 1)),
                     QItemSelectionModel::ClearAndSelect);
    ASSERT_TRUE(selection.isRowSelected(3, QModelIndex()));

    // Remove b (middle), insert x between a and c: a single diff pass must
    // produce the exact durable order with every survivor's data intact.
    model.applySnapshot(snapshotOf({"a", "x", "c", "d"}));

    EXPECT_EQ(idsOf(model), QStringList({"a", "x", "c", "d"}));
    EXPECT_EQ(rowOf(model, QStringLiteral("a")), 0);
    EXPECT_EQ(rowOf(model, QStringLiteral("x")), 1);
    EXPECT_EQ(rowOf(model, QStringLiteral("c")), 2);
    EXPECT_EQ(rowOf(model, QStringLiteral("d")), 3);
    EXPECT_TRUE(selection.isRowSelected(3, QModelIndex()));
    EXPECT_EQ(model.entryData(QStringLiteral("d")).file, QStringLiteral("d.txt"));
}

TEST(BatchQueueModel, RowDataUpdatesInPlaceWithoutStructuralChange) {
    int argc = 1;
    char name[] = "batch-queue-model-in-place-update";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);

    BatchQueueModel model;
    model.applySnapshot(snapshotOf({"a", "b"}));

    BatchEntryView updated = entry(QStringLiteral("b"));
    updated.state = static_cast<int>(BatchEntryState::Processing);
    updated.segments_done = 1;
    model.applySnapshot({entry(QStringLiteral("a")), updated});

    EXPECT_EQ(idsOf(model), QStringList({"a", "b"}));
    EXPECT_EQ(model.entryData(QStringLiteral("b")).state,
              static_cast<int>(BatchEntryState::Processing));
    EXPECT_EQ(model.entryData(QStringLiteral("b")).segments_done, 1);
}

TEST(BatchQueueModel, DuplicateIdsInSnapshotAreDeduped) {
    int argc = 1;
    char name[] = "batch-queue-model-dedupe";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);

    BatchQueueModel model;
    QVector<BatchEntryView> snapshot = snapshotOf({"a", "b"});
    snapshot.append(entry(QStringLiteral("a")));  // defensive duplicate
    model.applySnapshot(snapshot);

    EXPECT_EQ(idsOf(model), QStringList({"a", "b"}));
}
