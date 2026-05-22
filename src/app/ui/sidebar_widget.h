#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QScreen;

class SidebarWidget : public QWidget {
    Q_OBJECT

public:
    explicit SidebarWidget(QWidget * parent = nullptr);

    void setCurrentPage(int index);
    void setNavigationEnabled(bool enabled);

signals:
    void pageSelected(int index);

protected:
    void resizeEvent(QResizeEvent * event) override;
    bool event(QEvent * event) override;

private:
    void refreshLogo();
    qreal currentDevicePixelRatio() const;

    QLabel * logo_label_ = nullptr;
    QPushButton * translate_button_ = nullptr;
    QPushButton * model_button_ = nullptr;
};
