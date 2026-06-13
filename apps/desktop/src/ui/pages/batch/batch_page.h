#pragma once

#include <QWidget>

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

class BatchCard;
class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

// Card-based batch queue page with multi-selection.
// Toolbar: Add File, Delete, Save.
// Footer (right-aligned): Start/Pause.
// Cards use checkbox-style selection; no Select All / Clear buttons.
class BatchPage : public QWidget {
    Q_OBJECT

public:
    explicit BatchPage(QWidget *parent = nullptr);
    ~BatchPage() override;

    void setRunning(bool running);
    void setPaused(bool paused);

    void addCard(const QString &entry_id, const QString &file_name,
                 const QString &source_lang, const QString &target_lang);
    void removeCard(const QString &entry_id);
    void setCardState(const QString &entry_id, int state);
    void setCardProgress(const QString &entry_id, int completed, int total);
    void setCardSaved(const QString &entry_id, const QString &output_path);
    void setStatusText(const QString &text);

    QStringList selectedEntryIds() const;
    int cardCount() const { return cards_.size(); }

signals:
    void addFilesRequested();
    void removeSelectedRequested(const QStringList &entry_ids);
    void startRequested();
    void pauseRequested();
    void resumeRequested();
    void saveRequested(const QStringList &entry_ids);

private slots:
    void onCardClicked(const QString &entry_id);
    void onAddClicked();
    void onRemoveClicked();
    void onStartPauseClicked();
    void onSaveClicked();

private:
    BatchCard *cardForId(const QString &entry_id) const;
    void toggleSelection(const QString &entry_id);
    void clearSelection();
    void updateActionButtons();

    QHash<QString, BatchCard *> cards_;
    QSet<QString> selected_ids_;
    QWidget *card_container_ = nullptr;
    QVBoxLayout *card_layout_ = nullptr;
    QScrollArea *scroll_ = nullptr;

    QPushButton *add_button_ = nullptr;
    QPushButton *remove_button_ = nullptr;
    QPushButton *save_button_ = nullptr;
    QPushButton *start_pause_button_ = nullptr;
    QLabel *status_label_ = nullptr;

    bool running_ = false;
    bool paused_ = false;
};
