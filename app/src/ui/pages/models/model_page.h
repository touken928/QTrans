#pragma once

#include "domain/storage/app_paths.h"
#include "domain/settings/settings.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QWidget>

class ModelRow;
class RuntimeCapabilities;

class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

// Operational model library.
//
// A stable configured-model summary card (configured vs loaded vs lifecycle
// state), the model storage directory picker, and one dense row per catalog
// entry. Rows are created once per stable model id and updated in place —
// refreshing never rebuilds the list, so the configured highlight, scroll
// position and focus survive every state push.
//
// Lifecycle state is pushed by MainWindow as granular truth: which model is
// loading, whether an unload is in progress, and which model the single
// active download belongs to (with progress). While any operation runs, all
// competing row actions disable, and a downloading/loading model's file can
// never be deleted or mutated.
class ModelPage : public QWidget {
    Q_OBJECT

public:
    explicit ModelPage(QWidget *parent = nullptr);

    void setSettings(const AppPaths &paths, const AppSettings &settings);
    void setRuntimeCapabilities(const RuntimeCapabilities &caps);
    void setLoadedModelId(const QString &model_id);
    void setModelLoaded(bool loaded);
    // Empty model id means no load is in flight.
    void setLoadingModelId(const QString &model_id);
    void setUnloading(bool unloading);
    // Empty model id means no download is active (only one at a time).
    void setDownloadingModelId(const QString &model_id);
    void setDownloadProgress(qint64 downloaded, qint64 total);
    // True while interactive or batch inference runs: lifecycle actions
    // (load/unload/download) disable so work is never displaced mid-run.
    void setInferenceActive(bool active);
    void applyTo(AppSettings &settings) const;

signals:
    void loadModelRequested(const QString &model_id);
    void downloadModelRequested(const QString &model_id);
    void unloadModelRequested(const QString &model_id);
    void deleteModelRequested(const QString &model_id);
    void cancelDownloadRequested();
    void modelEdited();

private:
    void syncRows();
    void refreshRowStates();
    void refreshSummary();
    void chooseModelsDir();
    QString modelFilePath(const QString &model_id) const;
    bool modelFileExists(const QString &model_id) const;
    bool modelIsAvailable(const QString &model_id) const;

    AppPaths paths_;
    AppSettings settings_;
    bool has_runtime_caps_ = false;
    const RuntimeCapabilities *runtime_caps_ = nullptr;
    QString configured_model_id_;
    QString loaded_model_id_;
    bool model_loaded_ = false;
    QString loading_model_id_;
    bool unloading_ = false;
    QString downloading_model_id_;
    qint64 download_done_ = 0;
    qint64 download_total_ = 0;
    bool inference_active_ = false;

    QLineEdit *dir_edit_ = nullptr;
    QPushButton *browse_btn_ = nullptr;
    QScrollArea *scroll_ = nullptr;
    QWidget *rows_container_ = nullptr;
    QVBoxLayout *rows_layout_ = nullptr;

    // Configured-model summary card.
    QLabel *summary_name_ = nullptr;
    QLabel *summary_status_dot_ = nullptr;
    QLabel *summary_status_ = nullptr;
    QLabel *summary_path_ = nullptr;

    // Stable row identity: one widget per catalog entry, never rebuilt.
    QHash<QString, ModelRow *> rows_;
    QStringList row_order_;
};
