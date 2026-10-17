// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

namespace Tastra
{

class InputMethodBackend;

class KeyboardController
{
public:
    explicit KeyboardController(InputMethodBackend &backend);

    void tapText(const QString &text);
    void backspace();
    void deleteForward();
    void moveLeft();
    void moveRight();
    void moveHome();
    void moveEnd();
    void moveUp();
    void moveDown();
    void tab();
    void enter();
    bool deleteBeforeCursor(const QString &text);
    bool deleteAroundCursor(const QString &before, const QString &after);
    bool setPreedit(const QString &text);

private:
    InputMethodBackend &m_backend;
};

}
