#pragma once

#include <QImage>
#include <QPixmap>

QImage trimNearSolidBorder(const QImage &image, int tolerance = 12);
QPixmap scaledPixmapForWidth(const QImage &image, int logical_width, qreal device_pixel_ratio);
