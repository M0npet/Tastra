// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/systemclipboard.h"

class QClipboard;

namespace Tastra
{

// QClipboard (needs a QGuiApplication). Under KWin it only works while the
// keyboard would have focus, i.e. never; kept as the fallback.
class QtSystemClipboard final : public SystemClipboard
{
    Q_OBJECT

public:
    explicit QtSystemClipboard(QObject *parent = nullptr);

    QString text() const override;
    void setText(const QString &text) override;
    void clear() override;
    QByteArray image() const override;
    QString imageMime() const override;
    void setImage(const QByteArray &data, const QString &mime) override;

private:
    QClipboard *m_clipboard = nullptr;
};

}
