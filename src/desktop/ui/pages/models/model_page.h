#pragma once

#include "domain/storage/app_paths.h"
#include "domain/settings/settings.h"

#include <QMap>
#include <QString>
#include <QWidget>

class RuntimeCapabilities;

class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

class ModelPage : public QWidget {
    Q_OBJECT

public:
    explicit ModelPage(QWidget *parent = nullptr);

    void setSettings(const AppPaths &paths, const AppSettings &settings);
    void setRuntimeCapabilities(const RuntimeCapabilities &caps);
    void setLoadedModelId(const QString &model_id);
    void setModelLoaded(bool loaded);
    void setBusy(bool busy);
    void applyTo(AppSettings &settings) const;

signals:
    void loadModelRequested(const QString &model_id);
    void unloadModelRequested(const QString &model_id);
    void deleteModelRequested(const QString &model_id);
    void modelEdited();

private:
    void rebuildCards();
    void refreshCardStates();
    void chooseModelsDir();
    QString modelFilePath(const QString &model_id) const;
    bool modelFileExists(const QString &model_id) const;
    bool modelIsAvailable(const QString &model_id) const;

    AppPaths paths_;
    AppSettings settings_;
    bool has_runtime_caps_ = false;
    const RuntimeCapabilities *runtime_caps_ = nullptr;
    bool busy_ = false;
    bool model_loaded_ = false;
    QString loaded_model_id_;

    QLineEdit *dir_edit_ = nullptr;
    QPushButton *browse_btn_ = nullptr;
    QScrollArea *scroll_ = nullptr;
    QWidget *cards_container_ = nullptr;
    QVBoxLayout *cards_layout_ = nullptr;

    struct CardWidgets {
        QWidget *frame = nullptr;
        QLabel *status_badge = nullptr;
        QPushButton *load_btn = nullptr;
        QPushButton *unload_btn = nullptr;
        QPushButton *delete_btn = nullptr;
    };
    QMap<QString, CardWidgets> cards_;
};
