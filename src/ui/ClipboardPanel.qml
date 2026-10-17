// SPDX-License-Identifier: GPL-3.0-or-later
// Clipboard history panel (Gboard), loaded only while open.

import QtQuick
import QtQuick.Window

Column {
    anchors.fill: parent
    spacing: root.keyGap

    Rectangle {
        id: currentClipboardCard
        width: parent.width
        height: root.portrait ? 72 : 58
        radius: 12
        color: root.keyColor
        border.width: keyboardBridge.keyBorders ? 1 : 0
        border.color: root.borderColor

        Image {
            // A copied image or screenshot.
            objectName: "currentClipboardImage"
            visible: keyboardBridge.clipboardImage.length > 0
            anchors.fill: parent
            anchors.margins: 6
            source: keyboardBridge.clipboardImage
            sourceSize.height: height
            fillMode: Image.PreserveAspectFit
            horizontalAlignment: Image.AlignLeft
            cache: false
        }

        Text {
            visible: keyboardBridge.clipboardImage.length === 0
            anchors.fill: parent
            anchors.margins: 10
            text: keyboardBridge.hasClipboardText
                ? keyboardBridge.clipboardPreview
                : qsTr("Clipboard is empty")
            color: keyboardBridge.hasClipboardText ? root.textColor : root.secondaryTextColor
            font.pixelSize: root.portrait ? 15 : 13
            wrapMode: Text.Wrap
            elide: Text.ElideRight
            maximumLineCount: 2
        }
    }

    Row {
        id: clipboardControls
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: root.keyGap

        PanelButton {
            width: root.portrait ? 120 : 108
            label: qsTr("Paste")
            accent: true
            enabled: keyboardBridge.hasClipboardText
                || (keyboardBridge.clipboardImage.length > 0 && keyboardBridge.canPasteImages)
            onTriggered: keyboardBridge.pasteClipboard()
        }
        PanelButton {
            width: root.portrait ? 150 : 132
            label: qsTr("Clear history")
            enabled: keyboardBridge.clipboardHistory.length > 0
            onTriggered: keyboardBridge.clearClipboardHistory()
        }
    }

    ListView {
        width: parent.width
        height: parent.height - currentClipboardCard.height - clipboardControls.height - root.keyGap * 2
        clip: true
        spacing: root.keyGap
        model: keyboardBridge.clipboardHistory

        delegate: Rectangle {
            required property string modelData
            required property int index
            readonly property bool image: keyboardBridge.isClipboardImage(modelData)
            width: ListView.view.width
            height: image ? (root.portrait ? 120 : 100) : (root.portrait ? 66 : 54)
            radius: 12
            color: historyMouse.pressed ? root.specialKeyColor : root.keyColor
            border.width: keyboardBridge.keyBorders ? 1 : 0
            border.color: root.borderColor

            Image {
                // Gboard keeps copied images and screenshots: tap pastes
                // (the application pastes it with Ctrl+V), long-press pins.
                objectName: "clipboardImageItem"
                visible: parent.image
                anchors.left: parent.left
                anchors.right: removeHistory.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.margins: 6
                source: parent.image ? keyboardBridge.clipboardItemImage(modelData) : ""
                sourceSize.height: height
                fillMode: Image.PreserveAspectFit
                horizontalAlignment: Image.AlignLeft
                opacity: keyboardBridge.canPasteImages ? 1.0 : 0.6
                Text {
                    visible: (keyboardBridge.clipboardHistory, keyboardBridge.isClipboardPinned(modelData))
                    anchors.left: parent.left
                    anchors.top: parent.top
                    text: "\uD83D\uDCCC"
                    font.pixelSize: root.portrait ? 18 : 16
                }
            }

            Text {
                visible: !parent.image
                anchors.left: parent.left
                anchors.right: removeHistory.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                // Long-press pins (Gboard): pinned items stay past the hour.
                objectName: "clipboardItem"
                text: parent.image ? "" : (keyboardBridge.clipboardHistory, keyboardBridge.isClipboardPinned(modelData) ? "\uD83D\uDCCC " : "") + modelData
                color: root.textColor
                font.pixelSize: root.portrait ? 15 : 13
                elide: Text.ElideRight
                maximumLineCount: 2
                wrapMode: Text.Wrap
            }

            Rectangle {
                id: removeHistory
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                width: root.portrait ? 40 : 34
                height: width
                radius: width / 2
                color: removeHistoryMouse.pressed ? root.borderColor : "transparent"
                Text {
                    anchors.centerIn: parent
                    text: "×"
                    color: root.secondaryTextColor
                    font.pixelSize: root.portrait ? 22 : 19
                }
                MouseArea {
                    id: removeHistoryMouse
                    anchors.fill: parent
                    onClicked: keyboardBridge.removeClipboardHistory(index)
                }
            }

            MouseArea {
                id: historyMouse
                anchors.left: parent.left
                anchors.right: removeHistory.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                property bool pinToggled: false
                onPressed: pinToggled = false
                onPressAndHold: {
                    pinToggled = true
                    keyboardBridge.toggleClipboardPin(modelData)
                }
                onClicked: {
                    if (pinToggled) return
                    keyboardBridge.pasteClipboardHistory(index)
                }
            }
        }
    }
}
