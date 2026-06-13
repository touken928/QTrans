#pragma once

#include <QWidget>

class QComboBox;

// Modal language-selection panel shown before the file picker opens.
class BatchLangPanel : public QWidget {
    Q_OBJECT

public:
    explicit BatchLangPanel(QWidget *parent = nullptr);

    void setDefaultLanguages(const QString &source_lang, const QString &target_lang);

signals:
    void confirmed(const QString &source_lang, const QString &target_lang);
    void cancelled();

private slots:
    void onConfirm();
    void onCancel();

private:
    int findLanguageIndex(const QString &label) const;
    QComboBox *source_combo_ = nullptr;
    QComboBox *target_combo_ = nullptr;
};
