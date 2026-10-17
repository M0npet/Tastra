// SPDX-License-Identifier: GPL-3.0-or-later

#include "keyboardcontroller.h"
#include "inputmethodbackend.h"

namespace Tastra
{

KeyboardController::KeyboardController(InputMethodBackend &backend)
    : m_backend(backend)
{
}

void KeyboardController::tapText(const QString &text)
{
    m_backend.commitText(text);
}

void KeyboardController::backspace()
{
    m_backend.backspace();
}

void KeyboardController::deleteForward()
{
    m_backend.deleteForward();
}

void KeyboardController::moveLeft()
{
    m_backend.moveLeft();
}

void KeyboardController::moveRight()
{
    m_backend.moveRight();
}

void KeyboardController::moveHome()
{
    m_backend.moveHome();
}

void KeyboardController::moveEnd()
{
    m_backend.moveEnd();
}

void KeyboardController::moveUp() { m_backend.moveUp(); }
void KeyboardController::moveDown() { m_backend.moveDown(); }
void KeyboardController::tab() { m_backend.tab(); }

void KeyboardController::enter()
{
    m_backend.enter();
}


bool KeyboardController::deleteAroundCursor(const QString &before, const QString &after)
{
    return m_backend.deleteAroundCursor(before, after);
}

bool KeyboardController::deleteBeforeCursor(const QString &text)
{
    return m_backend.deleteBeforeCursor(text);
}


bool KeyboardController::setPreedit(const QString &text)
{
    return m_backend.setPreedit(text);
}

}
