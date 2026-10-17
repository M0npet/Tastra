// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <functional>

namespace Tastra
{

// Gboard "paste sections of text": the e-mail addresses, web addresses, phone
// numbers, numeric dates and times and other numbers found in a copied text,
// each offered on its own (at most `limit`, in that order, none twice, none
// that is part of an earlier one, never the whole text itself).
QStringList clipboardParts(const QString &text, int limit = 3);

// Clipboard history as on Gboard: items are kept for one hour unless pinned.
// Only pinned items are written to disk; everything else lives in memory.
class ClipboardHistory
{
public:
    ClipboardHistory();

    void setEnabled(bool enabled);
    bool enabled() const;
    void capture(const QString &text);
    // Gboard keeps copied images and screenshots too. An image is an item
    // whose text is a key (isImage()); its bytes stay in memory (at most
    // MaxImages unpinned ones, each at most MaxImageBytes), a pinned one is
    // also written to the data folder.
    void captureImage(const QByteArray &data, const QString &mime);
    static bool isImage(const QString &item);
    // The key an image is kept under: a private-use mark, its SHA-1 and an
    // extension for its type ("\uE000image:<sha1>.png").
    static QString imageKey(const QByteArray &data, const QString &mime);
    static QString imageMimeForKey(const QString &key);
    QByteArray imageData(const QString &item) const;
    // Pinned items first, then the newest unpinned ones.
    QStringList items() const;
    void removeAt(int index);
    void clear();

    bool isPinned(const QString &text) const;
    void setPinned(const QString &text, bool pinned);

    void setClockForTesting(std::function<qint64()> clock);

    static constexpr int MaxImages = 5;
    static constexpr qsizetype MaxImageBytes = 24 * 1024 * 1024;

private:
    struct Item {
        QString text;
        qint64 capturedMs = 0;
        bool pinned = false;
        mutable QByteArray image;      // images only; a pin loaded from disk reads it when needed
    };
    static QString imageFolder();
    static QString imageFile(const QString &key);
    void writePinnedImage(const Item &item) const;
    static void removePinnedImage(const QString &key);
    void dropExtraImages();
    void load();
    void persist() const;
    void expire() const;
    qint64 now() const;
    QString sanitized(const QString &text) const;

    bool m_enabled = true;
    mutable QList<Item> m_items;
    int m_limit = 20;
    std::function<qint64()> m_clock;
    static constexpr qint64 LifetimeMs = 60 * 60 * 1000;
};

}
