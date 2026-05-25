#pragma once

#include <QWidget>

class QFrame;
class QLabel;
class QTimer;
class QVBoxLayout;

class PopupWindow : public QWidget
{
    Q_OBJECT

public:
    explicit PopupWindow(QWidget* parent = nullptr);

    void showLoading(const QString& sourceText);
    void appendChunk(const QString& chunk);
    void finishStreaming();
    void showError(const QString& message);

    void setAutoCloseMs(int ms);
    int autoCloseMs() const;

    bool isStreaming() const;

signals:
    void dismissed();

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void setupUI();
    void positionNearCursor();
    void startAutoClose();
    void adjustPopupSize();

    QFrame* m_frame = nullptr;
    QLabel* m_resultLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QTimer* m_closeTimer = nullptr;

    int m_autoCloseMs = 5000;
    bool m_isStreaming = false;

    static constexpr int CURSOR_OFFSET_X = 20;
    static constexpr int CURSOR_OFFSET_Y = 20;
    static constexpr int EDGE_MARGIN = 10;
    static constexpr int MAX_WIDTH = 480;
    static constexpr int MIN_WIDTH = 200;
};
