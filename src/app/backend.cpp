#include "app/backend.h"

#include "core/model_ops.h"
#include "download/download.h"
#include "models/hymt15.h"

#include <stdexcept>

namespace {

struct BusyGuard {
    std::atomic<bool> & flag;
    ~BusyGuard() { flag.store(false); }
};

} // namespace

Backend::Backend(QObject * parent)
    : QObject(parent) {}

void Backend::setModelPath(const QString & path) {
    std::lock_guard<std::mutex> lock(mutex_);
    model_path_ = path.toUtf8().constData();
}

void Backend::setRemoteSpec(const QString & spec) {
    std::lock_guard<std::mutex> lock(mutex_);
    remote_spec_ = spec.toUtf8().constData();
}

void Backend::setDownloadHub(int hub) {
    std::lock_guard<std::mutex> lock(mutex_);
    download_hub_ = hub;
}

DownloadSpec Backend::makeDownloadSpec() const {
    std::lock_guard<std::mutex> lock(mutex_);

    DownloadSpec spec{};
    switch (download_hub_) {
    case 0:
        spec.hub = ModelHub::HuggingFace;
        break;
    case 1:
        spec.hub = ModelHub::ModelScope;
        break;
    default:
        spec.hub = ModelHub::Auto;
        break;
    }

    if (!download_parse_spec(remote_spec_, spec)) {
        throw std::runtime_error("invalid remote spec: " + remote_spec_);
    }
    return spec;
}

void Backend::downloadModel() {
    if (busy_.exchange(true)) {
        return;
    }

    BusyGuard guard{busy_};

    try {
        emit statusChanged(QStringLiteral("Downloading model..."), true);

        std::string model_path;
        DownloadSpec remote_spec = makeDownloadSpec();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            model_path = model_path_;
        }

        download_set_quiet(true);
        download_set_progress_callback([this](const DownloadProgress & progress) {
            emit downloadProgress(
                progress.downloaded_bytes,
                progress.total_bytes,
                progress.speed_bytes_per_sec,
                progress.eta_seconds);
        });

        download_to_file(model_path, remote_spec, true);
        download_set_progress_callback(nullptr);

        emit statusChanged(QStringLiteral("Download complete"), false);
        emit downloadFinished(true);
    } catch (const std::exception & ex) {
        download_set_progress_callback(nullptr);
        emit statusChanged(QString::fromUtf8(std::string("Error: ") + ex.what()), false);
        emit downloadFinished(false);
    }
}

void Backend::unloadModel() {
    if (busy_.exchange(true)) {
        return;
    }

    BusyGuard guard{busy_};

    {
        std::lock_guard<std::mutex> lock(mutex_);
        model_.reset();
        model_loaded_ = false;
    }

    emit statusChanged(QStringLiteral("Model unloaded"), false);
    emit modelUnloadFinished();
}

void Backend::loadModel() {
    if (busy_.exchange(true)) {
        return;
    }

    BusyGuard guard{busy_};

    try {
        emit statusChanged(QStringLiteral("Loading model into memory..."), true);

        std::string model_path;
        TranslationModelConfig config{};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            model_path = model_path_;
            config = model_config_;
        }

        if (!download_file_exists(model_path)) {
            throw std::runtime_error("model file not found: " + model_path);
        }

        const std::vector<std::uint8_t> data = read_file_bytes(model_path);

        auto model = std::make_unique<Hymt15>();
        model->load(data, config);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            model_ = std::move(model);
            model_loaded_ = true;
        }

        emit statusChanged(QStringLiteral("Model loaded"), false);
        emit modelLoadFinished(true);
    } catch (const std::exception & ex) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            model_loaded_ = false;
            model_.reset();
        }
        emit statusChanged(QString::fromUtf8(std::string("Error: ") + ex.what()), false);
        emit modelLoadFinished(false);
    }
}

void Backend::translate(
    const QString & source,
    const QString & target_language,
    bool reset_output,
    bool output_to_back) {
    if (busy_.exchange(true)) {
        emit statusChanged(QStringLiteral("Busy, please wait..."), true);
        return;
    }

    BusyGuard guard{busy_};

    try {
        TranslationModel * model = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!model_loaded_ || !model_ || !model_->is_loaded()) {
                throw std::runtime_error("model is not loaded");
            }
            model = model_.get();
        }

        if (source.trimmed().isEmpty()) {
            throw std::runtime_error("enter text to translate");
        }

        emit statusChanged(QStringLiteral("Translating..."), true);
        if (reset_output) {
            if (output_to_back) {
                emit backTranslateReset();
            } else {
                emit targetReset();
            }
        }

        const std::string source_text = source.toUtf8().constData();
        const std::string target = target_language.toUtf8().constData();

        const auto on_token = [this, output_to_back](const std::string & piece) {
            const QString chunk = QString::fromUtf8(piece.c_str(), static_cast<int>(piece.size()));
            if (output_to_back) {
                emit backTranslateAppended(chunk);
            } else {
                emit targetAppended(chunk);
            }
        };

        model->translate(source_text, target, on_token);

        emit statusChanged(QStringLiteral("Done"), false);
        emit translationFinished();
    } catch (const std::exception & ex) {
        emit statusChanged(QString::fromUtf8(std::string("Error: ") + ex.what()), false);
        emit translationFinished();
    }
}
