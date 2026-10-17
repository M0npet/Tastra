// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

namespace Tastra
{

class InputMethodBackend
{
public:
    virtual ~InputMethodBackend() = default;

    virtual void commitText(const QString &text) = 0;
    virtual void backspace() = 0;
    virtual void deleteForward() = 0;
    virtual void moveLeft() = 0;
    virtual void moveRight() = 0;
    virtual void moveHome() = 0;
    virtual void moveEnd() = 0;
    virtual void enter() = 0;
    virtual void moveUp() {}
    virtual void moveDown() {}
    // The Tab key (the voice command "next": the next field).
    virtual void tab() {}

    // Deletes `text`, which must be exactly the text before the cursor, via
    // the same channel as commitText(). Returns false when the backend or the
    // current client cannot do that; callers then fall back to backspace().
    virtual bool deleteBeforeCursor(const QString &text)
    {
        Q_UNUSED(text);
        return false;
    }

    // Deletes `before` (ending at the cursor) and `after` (starting at it) in
    // one request, e.g. to replace the word the cursor sits in.
    virtual bool deleteAroundCursor(const QString &before, const QString &after)
    {
        Q_UNUSED(before);
        Q_UNUSED(after);
        return false;
    }

    // Shows `text` as the client's preedit (composition), replacing any
    // previous one; an empty string clears it. The next commitText() replaces
    // the preedit atomically. Returns false when unsupported.
    virtual bool setPreedit(const QString &text)
    {
        Q_UNUSED(text);
        return false;
    }
};

}
