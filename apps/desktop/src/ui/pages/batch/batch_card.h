#pragma once

#include <QWidget>

class QLabel;
class QTimer;

// Compact selectable card representing one batch entry.
// Selection shown as a painted square checkbox.
// Status shown as a colored circle + text; Processing state pulses.
class BatchCard : public QWidget {
    Q_OBJECT

public:
    explicit BatchCard(const QString &entry_id, const QString &file_name,
                       const QString &source_lang, const QString &target_lang,
                       QWidget *parent = nullptr);

    QString entryId() const { return entry_id_; }

    void setSelected(bool selected);
    bool isSelected() const { return selected_; }

    void setState(int state);
    void setProgress(int completed, int total);
    void setSaved(bool saved, const QString &output_path = {});

signals:
    void clicked(const QString &entry_id);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void updateStyle();
    QColor pulseColor() const;
    int pulseAlpha() const;

    QString entry_id_;
    QString file_path_;
    bool selected_ = false;

    QLabel *file_label_ = nullptr;
    QLabel *lang_label_ = nullptr;
    QLabel *status_icon_ = nullptr;  // colored circle via stylesheet
    QLabel *status_text_ = nullptr;
    QLabel *progress_label_ = nullptr;
    QLabel *save_label_ = nullptr;

    int state_ = 0;
    int completed_ = 0;
    int total_ = 0;
    bool saved_ = false;

    // Pulse animation for Processing state
    QTimer *pulse_timer_ = nullptr;
    int pulse_step_ = 0;
};
