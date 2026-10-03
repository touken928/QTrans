#pragma once

#include <QWidget>

class QLabel;
class QProgressBar;

// Persistent bottom status bar that projects the shell's operational state:
// the loaded model (with its backend usage), plus the current lifecycle /
// download / translation activity. It is a pure projection — MainWindow owns
// the underlying state and pushes truthfully derived labels here; no page or
// service ever mutates this widget directly.
//
// Presentation rules (no state lives here): the band surface sits one
// neutral step below the page background (surfaceAlt) with a 2px top
// boundary and a recessed highlight hairline, so it reads as a distinct
// full-width operational strip rather than a page extension; the activity
// chip occupies a fixed-width slot (elided text, tooltip carries the full
// message) so status changes never move the model/backend groups; the
// loaded model is a bounded slot and the backend a fixed-width value, both
// eliding with tooltips; the download surface (slim progress + speed/ETA)
// is transient and right-anchored.
class ShellStatusBar : public QWidget {
    Q_OBJECT

public:
    // Activity only selects the chip's accent colour; the label text is
    // supplied by the shell so it stays truthful to the source signals.
    enum class Activity {
        Idle,         // no model, nothing running
        Loading,      // model lifecycle work (load/unload) in progress
        Ready,        // model loaded, no activity
        Translating,  // own translate or batch job running
        Paused,       // batch job paused (distinct from running)
        Downloading,  // model download in progress
        Failed,       // last lifecycle step failed
    };

    explicit ShellStatusBar(QWidget *parent = nullptr);

    void setLoadedModel(const QString &display_name);  // empty => not loaded
    // Backend usage of the loaded model; empty => no model loaded.
    void setBackend(const QString &label);
    void setActivity(Activity activity, const QString &text);
    void setDownloadProgress(qint64 downloaded, qint64 total,
                             double speed_bps, double eta_seconds);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void refreshChipText();
    void refreshElisions();

    QLabel *chip_dot_ = nullptr;
    QLabel *chip_text_ = nullptr;
    QLabel *loaded_value_ = nullptr;
    QLabel *backend_value_ = nullptr;
    QProgressBar *download_progress_ = nullptr;
    QLabel *speed_label_ = nullptr;

    Activity activity_ = Activity::Idle;
    QString activity_text_;
    // Unelided source text, kept so tooltips and re-elision after resize
    // always have the full string.
    QString loaded_raw_;
    QString backend_raw_;
    QString speed_raw_;
    qint64 downloaded_ = 0;
    qint64 total_ = 0;
    double speed_bps_ = 0.0;
    bool has_download_metrics_ = false;
};
