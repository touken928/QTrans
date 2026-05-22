#pragma once

#include "core/app_paths.h"
#include "settings/settings.h"

#include <QWidget>

class QComboBox;
class QLineEdit;
class QPushButton;

class ModelPage : public QWidget {
    Q_OBJECT

public:
    explicit ModelPage(QWidget * parent = nullptr);

    void setSettings(const AppPaths & paths, const AppSettings & settings);
    void applyTo(AppSettings & settings) const;
    void setBusy(bool busy);
    void setModelLoaded(bool loaded);

signals:
    void saveRequested();
    void loadModelRequested();
    void unloadModelRequested();
    void modelEdited();

private:
    void updateActions();

    AppPaths paths_;
    AppSettings settings_;
    QComboBox * model_combo_ = nullptr;
    QLineEdit * models_dir_edit_ = nullptr;
    QPushButton * save_button_ = nullptr;
    QPushButton * load_button_ = nullptr;
    QPushButton * unload_button_ = nullptr;

    bool busy_ = false;
    bool model_loaded_ = false;
};
