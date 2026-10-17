// SPDX-License-Identifier: GPL-3.0-or-later

#include "kwininputmethodv1backend.h"

#include <QLoggingCategory>

#include <chrono>
#include <xkbcommon/xkbcommon-keysyms.h>

Q_LOGGING_CATEGORY(lcKWinBackend, "tastra.input.backend", QtWarningMsg)

namespace Tastra::KWin
{
namespace
{

quint32 monotonicMilliseconds()
{
    using namespace std::chrono;

    return static_cast<quint32>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

}

void KWinInputMethodV1Backend::setContext(InputMethodV1Context *context)
{
    m_context = context;
    m_kwinPendingCommit.clear();                 // KWin drops it with the old context
}

void KWinInputMethodV1Backend::setLatestSerial(quint32 serial)
{
    m_latestSerial = serial;
}

void KWinInputMethodV1Backend::commitText(const QString &text)
{
    if (!m_context) {
        return;
    }

    // Trace lengths only: typed text never reaches the log.
    qCDebug(lcKWinBackend) << "commit_string serial" << m_latestSerial << "utf8 bytes" << text.toUtf8().size();
    // A Wayland message is limited (libwayland before 1.23: 4096 bytes, any
    // version: 64 KiB), and a client that sends a larger one is disconnected:
    // a pasted page goes out in pieces of at most MaxCommitBytes, cut between
    // characters. The application joins them; a deletion sent before applies
    // with the first.
    constexpr qsizetype MaxCommitBytes = 3000;
    qsizetype start = 0, bytes = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        const bool pair = ch.isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate();
        const qsizetype size = pair ? 4 : ch.unicode() < 0x80 ? 1 : ch.unicode() < 0x800 ? 2 : 3;
        if (bytes + size > MaxCommitBytes) {
            m_context->commitString(m_latestSerial, text.mid(start, i - start));
            start = i;
            bytes = 0;
        }
        bytes += size;
        if (pair) ++i;
    }
    if (start < text.size() || text.isEmpty()) m_context->commitString(m_latestSerial, text.mid(start));
    // KWin (inputmethod.cpp) keeps the commit argument of the last
    // preedit_string and commits it itself when the user touches the text
    // window, presses a hardware key or focus moves; a commit_string does not
    // clear it. Without this, "hello" + "." and a tap in the text typed
    // "hello" a second time.
    if (!m_kwinPendingCommit.isEmpty()) {
        qCDebug(lcKWinBackend) << "preedit_string utf8 bytes 0 (clears KWin's pending commit)";
        m_context->preeditCursor(0);
        m_context->preeditString(m_latestSerial, QString(), QString());
        m_kwinPendingCommit.clear();
    }
}

bool KWinInputMethodV1Backend::deleteBeforeCursor(const QString &text)
{
    if (!m_context || text.isEmpty()) {
        return false;
    }

    const quint32 bytes = static_cast<quint32>(text.toUtf8().size());
    // KWin maps (index, length) to text-input-v2/v3 (before, after) as
    // before = -index, after = index + length; this deletes `bytes` before
    // the cursor and is applied by the client together with the following
    // commit_string on the same text-input object.
    qCDebug(lcKWinBackend) << "delete_surrounding_text bytes before cursor" << bytes;
    m_context->deleteSurroundingText(-static_cast<qint32>(bytes), bytes);
    return true;
}

bool KWinInputMethodV1Backend::deleteAroundCursor(const QString &before, const QString &after)
{
    if (!m_context || (before.isEmpty() && after.isEmpty())) return false;
    const quint32 b = static_cast<quint32>(before.toUtf8().size());
    const quint32 a = static_cast<quint32>(after.toUtf8().size());
    // KWin: before = -index, after = index + length.
    qCDebug(lcKWinBackend) << "delete_surrounding_text bytes around cursor" << b << a;
    m_context->deleteSurroundingText(-static_cast<qint32>(b), b + a);
    return true;
}

bool KWinInputMethodV1Backend::setPreedit(const QString &text)
{
    if (!m_context) {
        return false;
    }
    // KWin applies the cursor on the next preedit_string (bytes). The commit
    // argument is committed by KWin itself if keyboard focus moves to another
    // surface while the word is still unfinished.
    qCDebug(lcKWinBackend) << "preedit_string utf8 bytes" << text.toUtf8().size();
    m_context->preeditCursor(static_cast<qint32>(text.toUtf8().size()));
    m_context->preeditString(m_latestSerial, text, text);
    m_kwinPendingCommit = text;
    return true;
}

void KWinInputMethodV1Backend::backspace()
{
    sendKeySym(XKB_KEY_BackSpace);
}

void KWinInputMethodV1Backend::deleteForward()
{
    sendKeySym(XKB_KEY_Delete);
}

void KWinInputMethodV1Backend::moveLeft()
{
    sendKeySym(XKB_KEY_Left);
}

void KWinInputMethodV1Backend::moveRight()
{
    sendKeySym(XKB_KEY_Right);
}

void KWinInputMethodV1Backend::moveHome()
{
    sendKeySym(XKB_KEY_Home);
}

void KWinInputMethodV1Backend::moveEnd()
{
    sendKeySym(XKB_KEY_End);
}

void KWinInputMethodV1Backend::moveUp() { sendKeySym(XKB_KEY_Up); }
void KWinInputMethodV1Backend::moveDown() { sendKeySym(XKB_KEY_Down); }
void KWinInputMethodV1Backend::tab() { sendKeySym(XKB_KEY_Tab); }

void KWinInputMethodV1Backend::enter()
{
    sendKeySym(XKB_KEY_Return);
}

void KWinInputMethodV1Backend::sendKeySym(quint32 sym)
{
    if (!m_context) {
        return;
    }

    qCDebug(lcKWinBackend) << "keysym" << Qt::hex << sym;
    const quint32 time = monotonicMilliseconds();
    constexpr quint32 released = 0;
    constexpr quint32 pressed = 1;
    constexpr quint32 modifiers = 0;

    m_context->keySym(m_latestSerial, time, sym, pressed, modifiers);
    m_context->keySym(m_latestSerial, time, sym, released, modifiers);
}

}
