#pragma once

#include "download/download.h"
#include "models/translation_model.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <memory>
#include <mutex>

class Backend : public QObject {
    Q_OBJECT

public:
    explicit Backend(QObject * parent = nullptr);

    void setModelPath(const QString & path);
    void setRemoteSpec(const QString & spec);
    void setDownloadHub(int hub);

public slots:
    void downloadModel();
    void loadModel();
    void unloadModel();
    void translate(
        const QString & source,
        const QString & target_language,
        bool reset_output,
        bool output_to_back);

signals:
    void statusChanged(const QString & message, bool busy);
    void modelLoadFinished(bool success);
    void modelUnloadFinished();
    void downloadProgress(qint64 downloaded_bytes, qint64 total_bytes, double speed_bps, double eta_seconds);
    void downloadFinished(bool success);
    void targetReset();
    void targetAppended(const QString & piece);
    void backTranslateReset();
    void backTranslateAppended(const QString & piece);
    void translationFinished();

private:
    DownloadSpec makeDownloadSpec() const;

    mutable std::mutex mutex_;
    std::string model_path_;
    std::string remote_spec_ = "AngelSlim/Hy-MT2-1.8B-1.25Bit-GGUF/Hy-MT2-1.8B-1.25Bit.gguf";
    int download_hub_ = 2;
    TranslationModelConfig model_config_{};
    std::unique_ptr<TranslationModel> model_;
    bool model_loaded_ = false;
    std::atomic<bool> busy_{false};
};
