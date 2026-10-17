// SPDX-License-Identifier: GPL-3.0-or-later

#include "clipboardimageprovider.h"

#include <QImage>

namespace Tastra
{

ClipboardImageProvider::ClipboardImageProvider(std::function<QByteArray(const QString &id)> bytesFor)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_bytesFor(std::move(bytesFor))
{
}

QImage ClipboardImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    QImage image = QImage::fromData(m_bytesFor(id));
    if (size) *size = image.size();
    if (!image.isNull() && requestedSize.isValid() && !requestedSize.isEmpty()
        && (image.width() > requestedSize.width() || image.height() > requestedSize.height())) {
        image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}

}
