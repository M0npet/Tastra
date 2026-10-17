// SPDX-License-Identifier: GPL-3.0-or-later

#include "qtsystemclipboard.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>

namespace Tastra
{

QtSystemClipboard::QtSystemClipboard(QObject *parent)
    : SystemClipboard(parent)
{
    if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance())) return;
    m_clipboard = QGuiApplication::clipboard();
    connect(m_clipboard, &QClipboard::dataChanged, this, &SystemClipboard::changed);
}

QString QtSystemClipboard::text() const { return m_clipboard ? m_clipboard->text() : QString(); }

void QtSystemClipboard::setText(const QString &text)
{
    if (m_clipboard) m_clipboard->setText(text);
}

void QtSystemClipboard::clear()
{
    if (m_clipboard) m_clipboard->clear();
}

QString QtSystemClipboard::imageMime() const
{
    const QMimeData *data = m_clipboard ? m_clipboard->mimeData() : nullptr;
    if (!data || data->hasText()) return {};
    for (const QString &mime : imageMimes()) {
        if (data->hasFormat(mime)) return mime;
    }
    return {};
}

QByteArray QtSystemClipboard::image() const
{
    const QString mime = imageMime();
    return mime.isEmpty() ? QByteArray() : m_clipboard->mimeData()->data(mime);
}

void QtSystemClipboard::setImage(const QByteArray &data, const QString &mime)
{
    if (!m_clipboard || data.isEmpty()) return;
    auto *mimeData = new QMimeData;
    mimeData->setData(mime, data);
    m_clipboard->setMimeData(mimeData);
}

}
