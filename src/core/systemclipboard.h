// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

namespace Tastra
{

// The desktop clipboard as the keyboard sees it. On KWin the Wayland
// clipboard is only offered to the window with keyboard focus, which an
// input panel never has, so the platform layer provides a data-control
// implementation; QClipboard is the fallback (and what tests use).
class SystemClipboard : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    virtual QString text() const = 0;
    // A password manager's secret (KeePassXC and others mark it with the
    // x-kde-passwordManagerHint type, which Klipper honours): pasted on
    // request, but never kept in history or shown.
    virtual bool isSensitive() const { return false; }
    // Owns the clipboard with `text` (Copy/Cut in the text-editing panel).
    virtual void setText(const QString &text) = 0;
    virtual void clear() = 0;
    // A copied image or screenshot when the clipboard holds no text (PNG,
    // JPEG, WebP, GIF or BMP, as offered), and its type.
    virtual QByteArray image() const { return {}; }
    virtual QString imageMime() const { return {}; }
    // Owns the clipboard with an image (a picture pasted from the history).
    virtual void setImage(const QByteArray &data, const QString &mime) { Q_UNUSED(data) Q_UNUSED(mime) }

    // The image types read, in order of preference.
    static QStringList imageMimes()
    {
        return {QStringLiteral("image/png"), QStringLiteral("image/jpeg"), QStringLiteral("image/webp"),
                QStringLiteral("image/gif"), QStringLiteral("image/bmp")};
    }

Q_SIGNALS:
    // The clipboard holds something new (text() may be empty).
    void changed();
};

}
