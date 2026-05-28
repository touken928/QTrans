#include "app/ui/image_utils.h"

#include <QColor>
#include <QSize>
#include <QtGlobal>

namespace {

bool colorsSimilar(const QColor &a, const QColor &b, int tolerance) {
    return qAbs(a.red() - b.red()) <= tolerance && qAbs(a.green() - b.green()) <= tolerance && qAbs(a.blue() - b.blue()) <= tolerance;
}

bool rowIsBorder(const QImage &image, int y, const QColor &background, int tolerance) {
    for (int x = 0; x < image.width(); ++x) {
        if (!colorsSimilar(image.pixelColor(x, y), background, tolerance)) {
            return false;
        }
    }
    return true;
}

bool columnIsBorder(const QImage &image, int x, const QColor &background, int tolerance) {
    for (int y = 0; y < image.height(); ++y) {
        if (!colorsSimilar(image.pixelColor(x, y), background, tolerance)) {
            return false;
        }
    }
    return true;
}

}  // namespace

QImage trimNearSolidBorder(const QImage &image, int tolerance) {
    if (image.isNull()) {
        return image;
    }

    const QImage source = image.convertToFormat(QImage::Format_ARGB32);
    const QColor background = source.pixelColor(0, 0);

    int top = 0;
    int bottom = source.height() - 1;
    int left = 0;
    int right = source.width() - 1;

    while (top <= bottom && rowIsBorder(source, top, background, tolerance)) {
        ++top;
    }
    while (bottom >= top && rowIsBorder(source, bottom, background, tolerance)) {
        --bottom;
    }
    while (left <= right && columnIsBorder(source, left, background, tolerance)) {
        ++left;
    }
    while (right >= left && columnIsBorder(source, right, background, tolerance)) {
        --right;
    }

    if (top > bottom || left > right) {
        return source;
    }

    return source.copy(left, top, right - left + 1, bottom - top + 1);
}

QPixmap scaledPixmapForWidth(const QImage &image, int logical_width, qreal device_pixel_ratio) {
    if (image.isNull() || logical_width <= 0 || device_pixel_ratio <= 0.0) {
        return QPixmap();
    }

    const int physical_width = qMax(1, qRound(logical_width * device_pixel_ratio));
    const int physical_height = qMax(
        1,
        qRound(physical_width * static_cast<qreal>(image.height()) / static_cast<qreal>(image.width())));

    const QImage scaled = image.scaled(
        QSize(physical_width, physical_height),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation);

    QPixmap pixmap = QPixmap::fromImage(scaled);
    pixmap.setDevicePixelRatio(device_pixel_ratio);
    return pixmap;
}
