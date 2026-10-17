// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "panelmanager.h"

#include <QString>
#include <QVector>

namespace Tastra
{

enum class ToolbarActionKind {
    OpenPanel,
    Command,
};

struct ToolbarAction
{
    QString id;
    QString label;
    QString iconName;
    ToolbarActionKind kind = ToolbarActionKind::OpenPanel;
    PanelId panel = PanelId::Typing;
    bool enabled = true;
    bool visible = true;
    bool movable = true;
};

class ToolbarRegistry
{
public:
    bool registerAction(const ToolbarAction &action)
    {
        if (action.id.isEmpty() || actionById(action.id) != nullptr) {
            return false;
        }
        m_actions.append(action);
        return true;
    }

    const ToolbarAction *actionById(const QString &id) const
    {
        for (const auto &action : m_actions) {
            if (action.id == id) {
                return &action;
            }
        }
        return nullptr;
    }

    const QVector<ToolbarAction> &actions() const { return m_actions; }

    static ToolbarRegistry createDefault()
    {
        ToolbarRegistry registry;

        ToolbarAction language;
        language.id = QStringLiteral("language");
        language.label = QStringLiteral("Language");
        language.iconName = QStringLiteral("language");
        language.panel = PanelId::Language;
        language.visible = false;
        registry.registerAction(language);

        ToolbarAction clipboard;
        clipboard.id = QStringLiteral("clipboard");
        clipboard.label = QStringLiteral("Clipboard");
        clipboard.iconName = QStringLiteral("clipboard");
        clipboard.panel = PanelId::Clipboard;
        clipboard.visible = true;
        registry.registerAction(clipboard);

        ToolbarAction emoji;
        emoji.id = QStringLiteral("emoji");
        emoji.label = QStringLiteral("Emoji");
        emoji.iconName = QStringLiteral("emoji");
        emoji.panel = PanelId::Emoji;
        emoji.visible = true;
        registry.registerAction(emoji);

        ToolbarAction textEditing;
        textEditing.id = QStringLiteral("text-editing");
        textEditing.label = QStringLiteral("Text editing");
        textEditing.iconName = QStringLiteral("text-editing");
        textEditing.panel = PanelId::TextEditing;
        textEditing.visible = true;
        registry.registerAction(textEditing);

        // Gboard "Resize": drag the keyboard's top edge (handled by the UI).
        ToolbarAction resize;
        resize.id = QStringLiteral("resize");
        resize.label = QStringLiteral("Resize");
        resize.iconName = QStringLiteral("resize");
        resize.kind = ToolbarActionKind::Command;
        resize.visible = true;
        registry.registerAction(resize);

        // Gboard "One-handed mode" (off the toolbar until chosen in settings).
        ToolbarAction oneHanded;
        oneHanded.id = QStringLiteral("one-handed");
        oneHanded.label = QStringLiteral("One-handed mode");
        oneHanded.iconName = QStringLiteral("one-handed");
        oneHanded.kind = ToolbarActionKind::Command;
        oneHanded.visible = false;
        registry.registerAction(oneHanded);

        ToolbarAction settings;
        settings.id = QStringLiteral("settings");
        settings.label = QStringLiteral("Settings");
        settings.iconName = QStringLiteral("settings");
        settings.panel = PanelId::Settings;
        settings.visible = true;
        registry.registerAction(settings);

        return registry;
    }

private:
    QVector<ToolbarAction> m_actions;
};

}
