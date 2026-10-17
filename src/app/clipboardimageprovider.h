// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QQuickImageProvider>

#include <functional>

namespace Tastra
{

// "image://tastraclipboard/<id>": the copied images of the clipboard history
// and the clipboard itself, decoded at the size the panel asks for. The
// bytes come from the bridge (`bytesFor`), so nothing is written to disk for
// them.
class ClipboardImageProvider final : public QQuickImageProvider
{
public:
    explicit ClipboardImageProvider(std::function<QByteArray(const QString &id)> bytesFor);
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    std::function<QByteArray(const QString &id)> m_bytesFor;
};

}
