// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/inputmethodbackend.h"

#include <QString>
#include <QtGlobal>

namespace Tastra::KWin
{

class InputMethodV1Context
{
public:
    virtual ~InputMethodV1Context() = default;

    virtual void commitString(quint32 serial, const QString &text) = 0;
    virtual void deleteSurroundingText(qint32 index, quint32 length) = 0;
    virtual void preeditString(quint32 serial, const QString &text, const QString &commit) = 0;
    virtual void preeditCursor(qint32 index) = 0;

    virtual void keySym(
        quint32 serial,
        quint32 time,
        quint32 sym,
        quint32 state,
        quint32 modifiers) = 0;
};

class KWinInputMethodV1Backend final : public Tastra::InputMethodBackend
{
public:
    void setContext(InputMethodV1Context *context);
    void setLatestSerial(quint32 serial);

    void commitText(const QString &text) override;
    void backspace() override;
    void deleteForward() override;
    void moveLeft() override;
    void moveRight() override;
    void moveHome() override;
    void moveEnd() override;
    void moveUp() override;
    void moveDown() override;
    void tab() override;
    void enter() override;
    bool deleteBeforeCursor(const QString &text) override;
    bool deleteAroundCursor(const QString &before, const QString &after) override;
    bool setPreedit(const QString &text) override;

private:
    void sendKeySym(quint32 sym);
    InputMethodV1Context *m_context = nullptr;
    quint32 m_latestSerial = 0;
    // The commit argument of our last preedit_string: KWin keeps it and
    // commits it itself on a touch in the text window, a hardware key or a
    // focus change, until the next preedit_string replaces it.
    QString m_kwinPendingCommit;
};

}
