#pragma once

#include <QSize>
#include <QWidget>

class QFrame;
class QVBoxLayout;

class ModalOverlay : public QWidget {
    Q_OBJECT

public:
    explicit ModalOverlay(QWidget *parent = nullptr);

    void setContent(QWidget *content, const QSize &preferred_size = QSize(480, 320));
    void showModal();
    void hideModal();

protected:
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void repositionPanel();

    QFrame *panel_ = nullptr;
    QVBoxLayout *content_layout_ = nullptr;
    QWidget *content_widget_ = nullptr;
    QSize preferred_size_{480, 320};
};
