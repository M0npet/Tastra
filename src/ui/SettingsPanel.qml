// SPDX-License-Identifier: GPL-3.0-or-later
// Settings panel, loaded only while open.

import QtQuick
import QtQuick.Window

Flickable {
    id: settingsFlick
    objectName: "settingsFlick"
    anchors.fill: parent
    clip: true
    contentWidth: width
    contentHeight: settingsColumn.implicitHeight + root.keyGap * 2

    // The list is long: show that it scrolls.
    Rectangle {
        objectName: "settingsScrollIndicator"
        parent: settingsFlick
        visible: settingsFlick.contentHeight > settingsFlick.height
        anchors.right: parent.right
        anchors.rightMargin: 4
        width: 5
        radius: 2.5
        color: root.secondaryTextColor
        opacity: 0.7
        y: settingsFlick.visibleArea.yPosition * settingsFlick.height
        height: Math.max(24, settingsFlick.visibleArea.heightRatio * settingsFlick.height)
    }

    Column {
        id: settingsColumn
        width: Math.min(parent.width, root.portrait ? 640 : 780)
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: root.portrait ? 12 : 8

        Text {
            objectName: "settingsSection_Languages"
            width: parent.width
            topPadding: 14
            text: qsTr("Languages")
            color: root.accentColor
            font.pixelSize: root.portrait ? 15 : 13
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
        }

        Repeater {
            // Gboard: choose which languages the globe cycles through.
            model: keyboardBridge.allLanguageCodes
            delegate: Row {
                required property string modelData
                required property int index
                width: parent ? parent.width : 0
                spacing: 12
                Text { width: parent.width - languageToggle.width - parent.spacing; height: languageToggle.height; verticalAlignment: Text.AlignVCenter; text: keyboardBridge.allLanguageLabels[index]; color: root.textColor; font.pixelSize: root.portrait ? 17 : 15 }
                PanelButton {
                    id: languageToggle
                    objectName: "languageToggle_" + modelData
                    width: root.portrait ? 170 : 150
                    label: (keyboardBridge.languageCodes, keyboardBridge.isLanguageEnabled(modelData)) ? qsTr("On") : qsTr("Off")
                    onTriggered: keyboardBridge.setLanguageEnabled(modelData, !keyboardBridge.isLanguageEnabled(modelData))
                }
            }
        }

        Row {
            // Gboard "Select a layout" for the language being typed.
            visible: keyboardBridge.letterLayoutChoice
            width: parent.width; spacing: 12
            Text { width: parent.width - letterLayoutButton.width - parent.spacing; height: letterLayoutButton.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Letter layout (%1)").arg(keyboardBridge.languageLabel); color: root.textColor; font.pixelSize: root.portrait ? 17 : 15 }
            PanelButton { id: letterLayoutButton; objectName: "letterLayoutButton"; width: root.portrait ? 170 : 150; label: keyboardBridge.morseLayout ? qsTr("Morse code") : keyboardBridge.letterLayout; onTriggered: keyboardBridge.cycleLetterLayout() }
        }

        Row {
            // Gboard Morse settings: how long a pause sends the keyed sign
            // ("Never": Space sends it) and whether a longer one ends the word.
            visible: keyboardBridge.morseLayout
            width: parent.width; spacing: 12
            Text { width: parent.width - morseCharacterButton.width - parent.spacing; height: morseCharacterButton.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Morse: character timeout"); color: root.textColor; font.pixelSize: root.portrait ? 17 : 15; wrapMode: Text.WordWrap }
            PanelButton { id: morseCharacterButton; objectName: "morseCharacterTimeoutButton"; width: root.portrait ? 170 : 150; label: keyboardBridge.morseCharacterTimeout === 0 ? qsTr("Never") : qsTr("%1 s").arg((keyboardBridge.morseCharacterTimeout / 1000).toFixed(1)); onTriggered: keyboardBridge.cycleMorseCharacterTimeout() }
        }

        Row {
            visible: keyboardBridge.morseLayout
            width: parent.width; spacing: 12
            Text { width: parent.width - morseWordButton.width - parent.spacing; height: morseWordButton.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Morse: word timeout"); color: root.textColor; font.pixelSize: root.portrait ? 17 : 15; wrapMode: Text.WordWrap }
            PanelButton { id: morseWordButton; objectName: "morseWordTimeoutButton"; width: root.portrait ? 170 : 150; label: keyboardBridge.morseWordTimeout === 0 ? qsTr("Off") : qsTr("%1 s").arg((keyboardBridge.morseWordTimeout / 1000).toFixed(1)); onTriggered: keyboardBridge.cycleMorseWordTimeout() }
        }

        Row {
            // Gboard follows the system language; here it can also be chosen.
            width: parent.width; spacing: 12
            Text { width: parent.width - uiLanguageButton.width - parent.spacing; height: uiLanguageButton.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Interface language"); color: root.textColor; font.pixelSize: root.portrait ? 17 : 15 }
            PanelButton { id: uiLanguageButton; objectName: "uiLanguageButton"; width: root.portrait ? 170 : 150; label: keyboardBridge.uiLanguageLabel; onTriggered: keyboardBridge.cycleUiLanguage() }
        }

        Text {
            objectName: "settingsSection_Preferences"
            width: parent.width
            topPadding: 14
            text: qsTr("Preferences")
            color: root.accentColor
            font.pixelSize: root.portrait ? 15 : 13
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
        }

        Row {
            width: parent.width; spacing: 12
            Text { width: parent.width - numberRowButton.width - parent.spacing; height: numberRowButton.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Number row"); color: root.textColor; font.pixelSize: root.portrait ? 17 : 15 }
            PanelButton { id: numberRowButton; width: root.portrait ? 170 : 150; label: keyboardBridge.numberRow ? qsTr("On") : qsTr("Off"); onTriggered: keyboardBridge.setNumberRow(!keyboardBridge.numberRow) }
        }

        Row {
            width: parent.width; spacing: 12
            Text { width: parent.width - emojiKeyButton.width - parent.spacing; height: emojiKeyButton.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Show emoji switch key"); color: root.textColor; font.pixelSize: root.portrait ? 17 : 15 }
            PanelButton { id: emojiKeyButton; objectName: "emojiKeySetting"; width: root.portrait ? 170 : 150; label: keyboardBridge.showEmojiKey ? qsTr("On") : qsTr("Off"); onTriggered: keyboardBridge.setShowEmojiKey(!keyboardBridge.showEmojiKey) }
        }

        Row {
            width: parent.width; spacing: 12
            // Gboard: greyed out while the emoji key is shown.
            Text { width: parent.width - languageKeyButton.width - parent.spacing; height: languageKeyButton.height; verticalAlignment: Text.AlignVCenter; opacity: keyboardBridge.showEmojiKey ? 0.45 : 1; text: qsTr("Show language switch key"); color: root.textColor; font.pixelSize: root.portrait ? 17 : 15 }
            PanelButton { id: languageKeyButton; objectName: "languageKeySetting"; width: root.portrait ? 170 : 150; enabled: !keyboardBridge.showEmojiKey; label: keyboardBridge.showLanguageKey ? qsTr("On") : qsTr("Off"); onTriggered: keyboardBridge.setShowLanguageKey(!keyboardBridge.showLanguageKey) }
        }

        Row {
            width: parent.width; spacing: 12
            Text { width: parent.width - hintsButton.width - parent.spacing; height: hintsButton.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Long-press symbols (key hints)"); color: root.textColor; font.pixelSize: root.portrait ? 17 : 15 }
            PanelButton { id: hintsButton; width: root.portrait ? 170 : 150; label: keyboardBridge.symbolHints ? qsTr("On") : qsTr("Off"); onTriggered: keyboardBridge.setSymbolHints(!keyboardBridge.symbolHints) }
        }

        Row {
            width: parent.width; spacing: 12
            Text { width: parent.width - emojiRowButton.width - parent.spacing; height: emojiRowButton.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Emoji fast-access row"); color: root.textColor; font.pixelSize: root.portrait ? 17 : 15 }
            PanelButton { id: emojiRowButton; width: root.portrait ? 170 : 150; label: keyboardBridge.emojiRow ? qsTr("On") : qsTr("Off"); onTriggered: keyboardBridge.setEmojiRow(!keyboardBridge.emojiRow) }
        }

        Row {
            visible: keyboardBridge.canPlayKeySound
            width: parent.width; spacing: 12
            Text { width: parent.width - keySoundButton.width - parent.spacing; height: keySoundButton.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Sound on keypress"); color: root.textColor; font.pixelSize: root.portrait ? 17 : 15 }
            PanelButton { id: keySoundButton; width: root.portrait ? 170 : 150; label: keyboardBridge.keySound ? qsTr("On") : qsTr("Off"); onTriggered: keyboardBridge.setKeySound(!keyboardBridge.keySound) }
        }

        Row {
            visible: keyboardBridge.canPlayKeySound && keyboardBridge.keySound
            width: parent.width; spacing: 12
            Text { width: parent.width - keySoundVolumeButton.width - parent.spacing; height: keySoundVolumeButton.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Volume on keypress"); color: root.textColor; font.pixelSize: root.portrait ? 17 : 15 }
            PanelButton { id: keySoundVolumeButton; objectName: "keySoundVolumeButton"; width: root.portrait ? 170 : 150; label: qsTr("%1 %").arg(keyboardBridge.keySoundVolume); onTriggered: keyboardBridge.cycleKeySoundVolume() }
        }

        Row {
            width: parent.width; spacing: 12
            Text { width: parent.width - popupButton.width - parent.spacing; height: popupButton.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Key popups"); color: root.textColor; font.pixelSize: root.portrait ? 17 : 15 }
            PanelButton { id: popupButton; width: root.portrait ? 170 : 150; label: keyboardBridge.keyPopups ? qsTr("On") : qsTr("Off"); onTriggered: keyboardBridge.setKeyPopups(!keyboardBridge.keyPopups) }
        }
        Row {
            width: parent.width; spacing: 12
            Text { width: parent.width - longPressButton.width - parent.spacing; height: longPressButton.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Long-press delay"); color: root.textColor; font.pixelSize: root.portrait ? 17 : 15 }
            PanelButton { id: longPressButton; width: root.portrait ? 170 : 150; label: qsTr("%1 ms").arg(keyboardBridge.longPressDelay); onTriggered: keyboardBridge.cycleLongPressDelay() }
        }

        Row {
            width: parent.width; spacing: 12
            Text { width: parent.width - keySizeControls.width - parent.spacing; height: keySizeControls.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Key size"); color: root.textColor; font.pixelSize: root.portrait ? 17 : 15 }
            Row {
                id: keySizeControls; spacing: 8
                PanelButton { width: root.portrait ? 62 : 54; label: "−"; enabled: keyboardBridge.keyScale > 0.751; onTriggered: keyboardBridge.setKeyScale(keyboardBridge.keyScale - 0.05) }
                Rectangle { width: root.portrait ? 82 : 72; height: root.portrait ? 52 : 44; radius: 12; color: root.keyColor; Text { anchors.centerIn: parent; text: Math.round(keyboardBridge.keyScale * 100) + "%"; color: root.textColor; font.pixelSize: root.portrait ? 16 : 14 } }
                PanelButton { width: root.portrait ? 62 : 54; label: "+"; enabled: keyboardBridge.keyScale < 1.399; onTriggered: keyboardBridge.setKeyScale(keyboardBridge.keyScale + 0.05) }
            }
        }

        Row {
            width: parent.width; spacing: 12
            Text { width: parent.width - layoutButton.width - parent.spacing; height: layoutButton.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Keyboard layout"); color: root.textColor; font.pixelSize: root.portrait ? 17 : 15 }
            PanelButton { id: layoutButton; width: root.portrait ? 170 : 150; label: keyboardBridge.layoutMode === "left" ? qsTr("Compact left") : keyboardBridge.layoutMode === "right" ? qsTr("Compact right") : keyboardBridge.layoutMode === "split" ? qsTr("Split") : qsTr("Full width"); onTriggered: keyboardBridge.cycleLayoutMode() }
        }

        Text {
            objectName: "settingsSection_Toolbar"
            width: parent.width
            topPadding: 14
            text: qsTr("Toolbar")
            color: root.accentColor
            font.pixelSize: root.portrait ? 15 : 13
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
        }

        Repeater {
            // Gboard: choose the features on the toolbar and their order.
            model: keyboardBridge.toolbarChoices
            delegate: Row {
                required property string modelData
                required property int index
                readonly property bool shown: (keyboardBridge.toolbarActionIds, keyboardBridge.isToolbarItemShown(modelData))
                width: parent ? parent.width : 0
                spacing: 12
                function title(id) {
                    if (id === "clipboard") return qsTr("Clipboard")
                    if (id === "emoji") return qsTr("Emoji")
                    if (id === "text-editing") return qsTr("Text editing")
                    if (id === "resize") return qsTr("Resize")
                    if (id === "one-handed") return qsTr("One-handed mode")
                    if (id === "settings") return qsTr("Settings")
                    return id
                }
                Text {
                    width: parent.width - toolbarUp.width - toolbarShown.width - parent.spacing * 2
                    height: toolbarShown.height
                    verticalAlignment: Text.AlignVCenter
                    text: parent.title(modelData)
                    color: root.textColor
                    opacity: parent.shown ? 1 : 0.6
                    font.pixelSize: root.portrait ? 17 : 15
                }
                PanelButton {
                    id: toolbarUp
                    objectName: "toolbarUp_" + modelData
                    width: root.portrait ? 62 : 54
                    label: "↑"
                    enabled: parent.shown && index > 0
                    onTriggered: keyboardBridge.moveToolbarItem(modelData, -1)
                }
                PanelButton {
                    id: toolbarShown
                    objectName: "toolbarShown_" + modelData
                    width: root.portrait ? 100 : 90
                    enabled: modelData !== "settings"
                    label: parent.shown ? qsTr("On") : qsTr("Off")
                    onTriggered: keyboardBridge.setToolbarItemShown(modelData, !parent.shown)
                }
            }
        }

        Text {
            objectName: "settingsSection_Theme"
            width: parent.width
            topPadding: 14
            text: qsTr("Theme")
            color: root.accentColor
            font.pixelSize: root.portrait ? 15 : 13
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
        }

        Row {
            width: parent.width; spacing: 12
            Text { width: parent.width - themeButton.width - parent.spacing; height: themeButton.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Theme"); color: root.textColor; font.pixelSize: root.portrait ? 17 : 15 }
            PanelButton { id: themeButton; width: root.portrait ? 170 : 150; label: keyboardBridge.theme === "system" ? qsTr("System") : keyboardBridge.theme === "light" ? qsTr("Light") : keyboardBridge.theme === "amoled" ? "AMOLED" : qsTr("Dark"); onTriggered: keyboardBridge.cycleTheme() }
        }

        Text {
            width: parent.width
            text: qsTr("Colour")
            color: root.textColor
            font.pixelSize: root.portrait ? 17 : 15
        }

        Flow {
            // Gboard "Colors": the plain theme first, then the colours.
            width: parent.width
            spacing: 10
            Repeater {
                model: [""].concat(keyboardBridge.themeColors)
                delegate: Rectangle {
                    required property string modelData
                    objectName: "themeColor_" + (modelData === "" ? "none" : modelData)
                    width: root.portrait ? 46 : 40; height: width; radius: width / 2
                    color: modelData === "" ? root.keyColor : modelData
                    border.width: keyboardBridge.themeColor === modelData ? 3 : 1
                    border.color: keyboardBridge.themeColor === modelData ? root.textColor : root.borderColor
                    Text { anchors.centerIn: parent; visible: parent.modelData === ""; text: "A"; color: root.textColor; font.pixelSize: 15 }
                    MouseArea { anchors.fill: parent; onClicked: keyboardBridge.setThemeColor(parent.modelData) }
                }
            }
        }

        Text {
            width: parent.width
            topPadding: 6
            text: qsTr("Gradients")
            color: root.textColor
            font.pixelSize: root.portrait ? 17 : 15
        }

        Flow {
            width: parent.width
            spacing: 10
            Repeater {
                model: keyboardBridge.themeGradients
                delegate: Rectangle {
                    required property string modelData
                    readonly property var parts: modelData.split(":")
                    objectName: "themeGradient_" + parts[0]
                    width: root.portrait ? 74 : 64; height: root.portrait ? 46 : 40; radius: 10
                    border.width: keyboardBridge.themeGradient === parts[0] ? 3 : 0
                    border.color: root.textColor
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: parts[1] }
                        GradientStop { position: 1.0; color: parts[2] }
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: keyboardBridge.setThemeGradient(keyboardBridge.themeGradient === parent.parts[0] ? "" : parent.parts[0])
                    }
                }
            }
        }

        Text {
            width: parent.width
            topPadding: 6
            text: qsTr("Background photo")
            color: root.textColor
            font.pixelSize: root.portrait ? 17 : 15
        }

        Flow {
            // Gboard "My themes": a photo of one's own (from the Pictures folder).
            id: photoChoices
            width: parent.width
            spacing: 10
            property var pictures: []
            Component.onCompleted: pictures = keyboardBridge.pictureFiles()
            onVisibleChanged: if (visible) pictures = keyboardBridge.pictureFiles()
            PanelButton {
                objectName: "themePhotoNone"
                width: root.portrait ? 110 : 96
                label: qsTr("None")
                accent: keyboardBridge.themeImage === ""
                onTriggered: keyboardBridge.setThemeImage("")
            }
            Repeater {
                model: photoChoices.pictures
                delegate: Rectangle {
                    required property string modelData
                    required property int index
                    objectName: "themePhoto_" + index
                    width: root.portrait ? 110 : 96; height: root.portrait ? 62 : 54
                    radius: 8
                    color: root.keyColor
                    border.width: keyboardBridge.themeImage === modelData ? 3 : 0
                    border.color: root.accentColor
                    clip: true
                    Image {
                        anchors.fill: parent; anchors.margins: parent.border.width
                        source: parent.modelData
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        sourceSize.width: 220; sourceSize.height: 124
                    }
                    MouseArea { anchors.fill: parent; onClicked: keyboardBridge.setThemeImage(parent.modelData) }
                }
            }
        }

        Text {
            visible: photoChoices.pictures.length === 0
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr("No pictures in %1").arg(keyboardBridge.picturesFolder)
            color: root.secondaryTextColor
            font.pixelSize: root.portrait ? 15 : 13
        }

        Row {
            visible: keyboardBridge.themeImage !== ""
            width: parent.width; spacing: 12
            Text { width: parent.width - photoDimButton.width - parent.spacing; height: photoDimButton.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Photo brightness"); color: root.textColor; font.pixelSize: root.portrait ? 17 : 15 }
            PanelButton { id: photoDimButton; objectName: "photoDimButton"; width: root.portrait ? 170 : 150; label: qsTr("%1 %").arg(100 - keyboardBridge.themeImageDim); onTriggered: keyboardBridge.cycleThemeImageDim() }
        }

        Row {
            width: parent.width; spacing: 12
            Text { width: parent.width - borderButton.width - parent.spacing; height: borderButton.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Key borders"); color: root.textColor; font.pixelSize: root.portrait ? 17 : 15 }
            PanelButton { id: borderButton; width: root.portrait ? 170 : 150; label: keyboardBridge.keyBorders ? qsTr("On") : qsTr("Off"); onTriggered: keyboardBridge.setKeyBorders(!keyboardBridge.keyBorders) }
        }

        Text {
            objectName: "settingsSection_Text correction"
            width: parent.width
            topPadding: 14
            text: qsTr("Text correction")
            color: root.accentColor
            font.pixelSize: root.portrait ? 15 : 13
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
        }

        Row {
            width: parent.width; spacing: 12
            Text { width: parent.width - suggestionsButton.width - parent.spacing; height: suggestionsButton.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Suggestions"); color: root.textColor; font.pixelSize: root.portrait ? 17 : 15 }
            PanelButton { id: suggestionsButton; width: root.portrait ? 170 : 150; label: keyboardBridge.suggestionsEnabled ? qsTr("On") : qsTr("Off"); onTriggered: keyboardBridge.setSuggestionsEnabled(!keyboardBridge.suggestionsEnabled) }
        }

        Row {
            width: parent.width; spacing: 12
            Text { width: parent.width - nextWordButton.width - parent.spacing; height: nextWordButton.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Next-word suggestions"); color: root.textColor; font.pixelSize: root.portrait ? 17 : 15 }
            PanelButton { id: nextWordButton; objectName: "nextWordSuggestionsButton"; width: root.portrait ? 170 : 150; label: keyboardBridge.nextWordSuggestions ? qsTr("On") : qsTr("Off"); onTriggered: keyboardBridge.setNextWordSuggestions(!keyboardBridge.nextWordSuggestions) }
        }

        Row {
            // Gboard "Smart Compose": the words you usually type next, as a chip.
            width: parent.width; spacing: 12
            Text { width: parent.width - phraseButton.width - parent.spacing; height: phraseButton.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Phrase suggestions"); color: root.textColor; font.pixelSize: root.portrait ? 17 : 15; wrapMode: Text.WordWrap }
            PanelButton { id: phraseButton; objectName: "phraseSuggestionsButton"; width: root.portrait ? 170 : 150; label: keyboardBridge.phraseSuggestions ? qsTr("On") : qsTr("Off"); onTriggered: keyboardBridge.setPhraseSuggestions(!keyboardBridge.phraseSuggestions) }
        }

        Row {
            width: parent.width; spacing: 12
            Text { width: parent.width - emojiSuggestButton.width - parent.spacing; height: emojiSuggestButton.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Emoji suggestions"); color: root.textColor; font.pixelSize: root.portrait ? 17 : 15 }
            PanelButton { id: emojiSuggestButton; width: root.portrait ? 170 : 150; label: keyboardBridge.emojiSuggestionsEnabled ? qsTr("On") : qsTr("Off"); onTriggered: keyboardBridge.setEmojiSuggestionsEnabled(!keyboardBridge.emojiSuggestionsEnabled) }
        }

        Row {
            width: parent.width; spacing: 12
            Text { width: parent.width - offensiveButton.width - parent.spacing; height: offensiveButton.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Block offensive words"); color: root.textColor; font.pixelSize: root.portrait ? 17 : 15 }
            PanelButton { id: offensiveButton; width: root.portrait ? 170 : 150; label: keyboardBridge.blockOffensive ? qsTr("On") : qsTr("Off"); onTriggered: keyboardBridge.setBlockOffensive(!keyboardBridge.blockOffensive) }
        }

        Row {
            width: parent.width; spacing: 12
            Text { width: parent.width - autocorrectButton.width - parent.spacing; height: autocorrectButton.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Autocorrect"); color: root.textColor; font.pixelSize: root.portrait ? 17 : 15 }
            PanelButton { id: autocorrectButton; width: root.portrait ? 170 : 150; label: keyboardBridge.autocorrectEnabled ? qsTr("On") : qsTr("Off"); onTriggered: keyboardBridge.setAutocorrectEnabled(!keyboardBridge.autocorrectEnabled) }
        }

        Row {
            width: parent.width; spacing: 12
            Text { width: parent.width - autoCapButton.width - parent.spacing; height: autoCapButton.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Auto-capitalization"); color: root.textColor; font.pixelSize: root.portrait ? 17 : 15 }
            PanelButton { id: autoCapButton; width: root.portrait ? 170 : 150; label: keyboardBridge.autoCapitalizationEnabled ? qsTr("On") : qsTr("Off"); onTriggered: keyboardBridge.setAutoCapitalizationEnabled(!keyboardBridge.autoCapitalizationEnabled) }
        }

        Row {
            width: parent.width; spacing: 12
            Text { width: parent.width - doubleSpaceButton.width - parent.spacing; height: doubleSpaceButton.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Double-space period"); color: root.textColor; font.pixelSize: root.portrait ? 17 : 15 }
            PanelButton { id: doubleSpaceButton; width: root.portrait ? 170 : 150; label: keyboardBridge.doubleSpacePeriodEnabled ? qsTr("On") : qsTr("Off"); onTriggered: keyboardBridge.setDoubleSpacePeriodEnabled(!keyboardBridge.doubleSpacePeriodEnabled) }
        }

        Row {
            width: parent.width; spacing: 12
            Text { width: parent.width - autoSpaceButton.width - parent.spacing; height: autoSpaceButton.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Auto-space after punctuation"); color: root.textColor; font.pixelSize: root.portrait ? 17 : 15 }
            PanelButton { id: autoSpaceButton; width: root.portrait ? 170 : 150; label: keyboardBridge.autoSpaceAfterPunctuation ? qsTr("On") : qsTr("Off"); onTriggered: keyboardBridge.setAutoSpaceAfterPunctuation(!keyboardBridge.autoSpaceAfterPunctuation) }
        }

        Text {
            objectName: "settingsSection_Glide typing"
            width: parent.width
            topPadding: 14
            text: qsTr("Glide typing")
            color: root.accentColor
            font.pixelSize: root.portrait ? 15 : 13
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
        }

        Row {
            width: parent.width; spacing: 12
            Text { width: parent.width - glideButton.width - parent.spacing; height: glideButton.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Glide typing engine"); color: root.textColor; font.pixelSize: root.portrait ? 17 : 15 }
            PanelButton { id: glideButton; width: root.portrait ? 170 : 150; label: keyboardBridge.glideEnabled ? qsTr("On") : qsTr("Off"); onTriggered: keyboardBridge.setGlideEnabled(!keyboardBridge.glideEnabled) }
        }

        Row {
            visible: true
            width: parent.width; spacing: 12
            Text { width: parent.width - glideTrailButton.width - parent.spacing; height: glideTrailButton.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Gesture trail"); color: root.textColor; font.pixelSize: root.portrait ? 17 : 15 }
            PanelButton { id: glideTrailButton; width: root.portrait ? 170 : 150; label: keyboardBridge.glideTrail ? qsTr("On") : qsTr("Off"); onTriggered: keyboardBridge.setGlideTrail(!keyboardBridge.glideTrail) }
        }

        Text {
            objectName: "settingsSection_Clipboard"
            width: parent.width
            topPadding: 14
            text: qsTr("Clipboard")
            color: root.accentColor
            font.pixelSize: root.portrait ? 15 : 13
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
        }

        Row {
            width: parent.width; spacing: 12
            Text { width: parent.width - clipboardHistoryButton.width - parent.spacing; height: clipboardHistoryButton.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Clipboard history"); color: root.textColor; font.pixelSize: root.portrait ? 17 : 15 }
            PanelButton { id: clipboardHistoryButton; width: root.portrait ? 170 : 150; label: keyboardBridge.clipboardHistoryEnabled ? qsTr("On") : qsTr("Off"); onTriggered: keyboardBridge.setClipboardHistoryEnabled(!keyboardBridge.clipboardHistoryEnabled) }
        }

        Text {
            objectName: "settingsSection_Dictionary"
            width: parent.width
            topPadding: 14
            text: qsTr("Dictionary")
            color: root.accentColor
            font.pixelSize: root.portrait ? 15 : 13
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
        }

        Row {
            width: parent.width; spacing: 12
            Text { width: parent.width - learningButton.width - parent.spacing; height: learningButton.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Local learning"); color: root.textColor; font.pixelSize: root.portrait ? 17 : 15 }
            PanelButton { id: learningButton; width: root.portrait ? 170 : 150; label: keyboardBridge.learningEnabled ? qsTr("On") : qsTr("Off"); onTriggered: keyboardBridge.setLearningEnabled(!keyboardBridge.learningEnabled) }
        }

        PanelButton {
            anchors.horizontalCenter: parent.horizontalCenter
            width: root.portrait ? 230 : 210
            label: qsTr("Clear learned words")
            onTriggered: keyboardBridge.clearLearnedWords()
        }

        Text {
            width: parent.width
            topPadding: 8
            text: qsTr("Personal dictionary (%1) — tap a word to remove it").arg(keyboardBridge.userWords.length)
            color: root.textColor
            font.pixelSize: root.portrait ? 17 : 15
        }

        Flow {
            width: parent.width
            spacing: 8
            Repeater {
                model: keyboardBridge.userWords
                delegate: Rectangle {
                    required property string modelData
                    objectName: "userWord_" + modelData
                    width: userWordText.implicitWidth + 24
                    height: root.portrait ? 40 : 34
                    radius: height / 2
                    color: userWordMouse.pressed ? root.pressedColor : root.keyColor
                    border.width: 1
                    border.color: root.borderColor
                    Text {
                        id: userWordText
                        anchors.centerIn: parent
                        text: modelData + "  \u00D7"
                        color: root.textColor
                        font.pixelSize: root.portrait ? 16 : 14
                    }
                    MouseArea {
                        id: userWordMouse
                        anchors.fill: parent
                        onClicked: keyboardBridge.removeWordFromDictionary(modelData)
                    }
                }
            }
        }

        PanelButton {
            objectName: "addWordButton"
            anchors.horizontalCenter: parent.horizontalCenter
            width: root.portrait ? 230 : 210
            label: qsTr("＋ Add word")
            onTriggered: keyboardBridge.startWordEntry()
        }

        Text {
            width: parent.width
            topPadding: 8
            visible: keyboardBridge.shortcutList.length > 0
            text: qsTr("Shortcuts (%1) — tap one to remove it").arg(keyboardBridge.shortcutList.length)
            color: root.textColor
            font.pixelSize: root.portrait ? 17 : 15
        }

        Flow {
            width: parent.width
            spacing: 8
            Repeater {
                model: keyboardBridge.shortcutList
                delegate: Rectangle {
                    required property string modelData
                    objectName: "shortcut_" + modelData.split(" = ")[0]
                    width: shortcutText.implicitWidth + 24
                    height: root.portrait ? 40 : 34
                    radius: height / 2
                    color: shortcutMouse.pressed ? root.pressedColor : root.keyColor
                    border.width: 1
                    border.color: root.borderColor
                    Text {
                        id: shortcutText
                        anchors.centerIn: parent
                        text: modelData.replace(" = ", " \u2192 ") + "  \u00D7"
                        color: root.textColor
                        font.pixelSize: root.portrait ? 16 : 14
                    }
                    MouseArea {
                        id: shortcutMouse
                        anchors.fill: parent
                        onClicked: keyboardBridge.removeShortcut(modelData.split(" = ")[0])
                    }
                }
            }
        }

        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            color: root.secondaryTextColor
            font.pixelSize: root.portrait ? 14 : 12
            text: qsTr("Add words with ＋ (a shortcut is optional: typing it offers the word), or tap a typed word in the suggestion strip. Many at once: %1 (one per line) and %2 (shortcut = text).")
                .arg("~/.config/tastra/dictionary.txt").arg("~/.config/tastra/shortcuts.txt")
        }

        Text {
            objectName: "settingsSection_Advanced"
            width: parent.width
            topPadding: 14
            text: qsTr("Advanced")
            color: root.accentColor
            font.pixelSize: root.portrait ? 15 : 13
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
        }

        Row {
            width: parent.width; spacing: 12
            Text { width: parent.width - compositionButton.width - parent.spacing; height: compositionButton.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Underline word while typing"); color: root.textColor; font.pixelSize: root.portrait ? 17 : 15 }
            PanelButton { id: compositionButton; width: root.portrait ? 170 : 150; label: keyboardBridge.compositionEnabled ? qsTr("On") : qsTr("Off"); onTriggered: keyboardBridge.setCompositionEnabled(!keyboardBridge.compositionEnabled) }
        }

        Text {
            // About: version, license and data sources (1.0).
            objectName: "aboutText"
            width: parent.width
            topPadding: 12
            wrapMode: Text.WordWrap
            color: root.secondaryTextColor
            font.pixelSize: root.portrait ? 14 : 12
            text: qsTr("Tastra %1 · GPL-3.0-or-later · works offline").arg(keyboardBridge.version) + "\n"
                + qsTr("Data: FrequencyWords, wordfreq and Universal Dependencies treebanks (CC BY-SA 4.0), UA-GEC (CC BY 4.0), Common Voice sentences (CC0), Unicode CLDR (Unicode License V3), LDNOOBW (CC BY 4.0), Hunspell dictionaries (system packages), LibreOffice uk_UA (MPL-1.1); optional whisper.cpp (MIT).") + "\n"
                + qsTr("Full texts: %1").arg("~/.local/share/doc/tastra")
        }
    }
}
