// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Window

Rectangle {
    id: root

    readonly property bool portrait: Screen.height > Screen.width
    readonly property var metrics: {
        var dependency = keyboardBridge.keyScale
        return keyboardBridge.layoutMetrics(portrait)
    }
    readonly property real keyGap: metrics.keyGap
    readonly property real keyHeight: metrics.keyHeight
    readonly property real toolbarHeight: portrait ? 54 : 48
    readonly property bool compact: keyboardBridge.layoutMode === "left" || keyboardBridge.layoutMode === "right"
    // Split (Gboard on tablets): the two halves move apart for thumb typing;
    // number pads stay whole.
    readonly property bool splitMode: keyboardBridge.layoutMode === "split" && !numpadShown
    readonly property real splitGap: splitMode ? width * 0.28 : 0
    // Compact mode: a narrower keyboard docked to one side, for one-handed use
    // of a large tablet (the input panel itself stays docked by KWin).
    readonly property real contentWidth: Math.min(
        width - metrics.outerMargin * 2,
        width * metrics.contentWidthRatio,
        metrics.maxContentWidth
    ) * (compact ? 0.62 : 1.0) - splitGap
    readonly property real baseKeyWidth: (contentWidth - keyGap * 9) / 10
    // Palette: Gboard-like light and dark themes; "system" follows Plasma.
    readonly property string themeName: keyboardBridge.effectiveTheme
    readonly property bool lightTheme: themeName === "light"
    readonly property bool amoledTheme: themeName === "amoled"
    // Gboard theme gallery: a colour tints the light or dark palette; an own
    // photo sits behind translucent keys.
    readonly property bool tinted: keyboardBridge.themeColor !== ""
    readonly property color themeTint: tinted ? keyboardBridge.themeColor : "#000000"
    // A photo or a gradient: the keys are translucent over it.
    readonly property bool photoTheme: keyboardBridge.themeImage !== "" || keyboardBridge.themeGradient !== ""
    readonly property var gradientStops: {
        for (const entry of keyboardBridge.themeGradients) {
            const parts = entry.split(":")
            if (parts[0] === keyboardBridge.themeGradient) return [parts[1], parts[2]]
        }
        return []
    }
    function tintOf(base, amount) {
        return tinted ? Qt.tint(base, Qt.rgba(themeTint.r, themeTint.g, themeTint.b, amount)) : base
    }
    function overPhoto(c, alpha) { return photoTheme ? Qt.rgba(c.r, c.g, c.b, alpha) : c }
    function escapeMarkup(text) {
        return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    }
    function phraseMarkup(phrase, headLength) {
        return escapeMarkup(phrase.slice(0, headLength)) + "<font color=\"" + secondaryTextColor
            + "\">" + escapeMarkup(phrase.slice(headLength)) + "</font>"
    }
    readonly property color backgroundColor: tintOf(lightTheme ? "#e8eaed" : amoledTheme ? "#000000" : "#202124", amoledTheme ? 0 : 0.28)
    readonly property color panelColor: tintOf(lightTheme ? "#f1f3f4" : amoledTheme ? "#080808" : "#292a2d", amoledTheme ? 0.05 : 0.18)
    readonly property color keyColor: overPhoto(tintOf(lightTheme ? "#ffffff" : amoledTheme ? "#171717" : "#3c4043", lightTheme ? 0.06 : 0.2), 0.72)
    readonly property color specialKeyColor: overPhoto(tintOf(lightTheme ? "#cdd0d4" : amoledTheme ? "#242424" : "#5f6368", 0.3), 0.8)
    readonly property color borderColor: tintOf(lightTheme ? "#c4c7c5" : amoledTheme ? "#333333" : "#56595e", 0.3)
    readonly property color textColor: lightTheme ? "#202124" : "#f1f3f4"
    readonly property color secondaryTextColor: lightTheme ? "#5f6368" : "#9aa0a6"
    readonly property color accentColor: tinted ? (lightTheme ? themeTint : Qt.lighter(themeTint, 1.35))
                                                : (lightTheme ? "#1a73e8" : "#8ab4f8")
    readonly property color pressedColor: tintOf(lightTheme ? "#d2d5d9" : "#60656a", 0.3)
    readonly property color accentPressedColor: tinted ? Qt.lighter(accentColor, 1.25) : (lightTheme ? "#8ab4f8" : "#9fc3ff")
    readonly property color selectedColor: tinted ? Qt.tint(lightTheme ? "#ffffff" : "#3c4043", Qt.rgba(themeTint.r, themeTint.g, themeTint.b, 0.4))
                                                  : (lightTheme ? "#d2e3fc" : "#465c78")
    property bool toolbarExpanded: false
    // Gboard "Resize": the keys are covered by a frame whose top bar is
    // dragged; the panel's bottom stays where it is.
    property bool resizing: false
    property string emojiCategory: "All"
    property var glideKeys: []
    property var glideStartItem: null
    property real glideStartX: 0
    property real glideStartY: 0
    property bool glideActive: false
    property string glideLastValue: ""

    width: Screen.width
    // Gboard shows a number pad in number/phone fields.
    readonly property bool numpadShown: keyboardBridge.inputPurpose === "number" || keyboardBridge.inputPurpose === "phone"
    // Gboard's Morse code layout replaces the letter rows (not symbols).
    readonly property bool morseShown: keyboardBridge.morseLayout && !keyboardBridge.symbolsActive && !numpadShown
    function morseGlyphs(code) { return code.replace(/\./g, "\u2022").replace(/-/g, "\u2014") }
    function morseLabel(text) {
        return keyboardBridge.uppercase ? text.charAt(0).toUpperCase() + text.slice(1) : text
    }
    readonly property bool numberRowShown: keyboardBridge.numberRow && !keyboardBridge.symbolsActive && !numpadShown
    // Gboard: the fast-access row is there as soon as it is switched on
    // (frequent emoji until some were used).
    readonly property bool emojiRowShown: keyboardBridge.emojiRow && !numpadShown
    // The keyboard keeps its height between letters and ?123: the symbol
    // rows share the number row's space (taller keys), so the panel edge
    // never moves under a finger and the application is not laid out again.
    readonly property bool numberRowSpace: keyboardBridge.numberRow && !numpadShown
    readonly property real layerKeyHeight: numberRowSpace && !numberRowShown
        ? keyHeight + (keyHeight + keyGap) / (emojiRowShown ? 5 : 4) : keyHeight
    height: toolbarHeight + metrics.panelHeight + (numberRowSpace ? keyHeight + keyGap : 0)
            + (emojiRowShown ? keyHeight + keyGap : 0)
    color: backgroundColor

    Rectangle {
        // Gboard "Gradients".
        objectName: "themeGradient"
        anchors.fill: parent
        visible: root.gradientStops.length === 2
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.gradientStops.length === 2 ? root.gradientStops[0] : "transparent" }
            GradientStop { position: 1.0; color: root.gradientStops.length === 2 ? root.gradientStops[1] : "transparent" }
        }
    }

    Image {
        // Gboard "custom" theme: the photo fills the keyboard, cut to fit.
        id: themePhoto
        objectName: "themePhoto"
        anchors.fill: parent
        visible: keyboardBridge.themeImage !== "" && status === Image.Ready
        source: keyboardBridge.themeImage
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        // Decoded at the keyboard's size, not the camera's.
        sourceSize.width: Math.max(1, root.width)
        sourceSize.height: Math.max(1, root.height)
        Rectangle {
            anchors.fill: parent
            color: root.lightTheme ? "#ffffff" : "#000000"
            opacity: keyboardBridge.themeImageDim / 100
        }
    }

    function dynamicKeyWidth(count) {
        return (contentWidth - keyGap * (count - 1)) / count
    }

    function toolbarGlyph(id) {
        if (id === "clipboard") return "▣"
        if (id === "emoji") return "☺"
        if (id === "text-editing") return "↔"
        if (id === "settings") return "⚙"
        if (id === "resize") return "⇕"
        if (id === "one-handed") return "✋"
        return "•"
    }

    function registerGlideKey(item) {
        if (glideKeys.indexOf(item) < 0) glideKeys.push(item)
    }

    function unregisterGlideKey(item) {
        var i = glideKeys.indexOf(item)
        if (i >= 0) glideKeys.splice(i, 1)
    }

    function glideHit(px, py) {
        for (var i = 0; i < glideKeys.length; ++i) {
            var k = glideKeys[i]
            if (!k || !k.visible || !k.glideEligible) continue
            var local = k.mapFromItem(root, px, py)
            if (local.x >= 0 && local.y >= 0 && local.x <= k.width && local.y <= k.height) return k
        }
        return null
    }

    function beginGlideCandidate(item, x, y) {
        if (!keyboardBridge.glideEnabled || keyboardBridge.symbolsActive || !item.glideEligible) return
        if (glideStartItem && glideStartItem !== item) {
            // A second finger is down: this is two-thumb tapping, not a glide.
            if (!glideActive) {
                glideStartItem = null
                glideLastValue = ""
            }
            return
        }
        var p = item.mapToItem(root, x, y)
        glideStartItem = item
        glideStartX = p.x
        glideStartY = p.y
        glideActive = false
        glideLastValue = ""
    }

    function glidePathLength() {
        var length = 0
        for (var i = 1; i < glidePath.length; ++i) {
            var dx = glidePath[i].x - glidePath[i - 1].x, dy = glidePath[i].y - glidePath[i - 1].y
            length += Math.sqrt(dx * dx + dy * dy)
        }
        return length
    }

    // Drops a glide that has not started (a tap was taken from its key).
    function abortGlideCandidate(item) {
        if (item !== glideStartItem || glideActive) return
        glideStartItem = null
        glideLastValue = ""
        glidePath = []
    }

    // LatinIME PointerTracker: a new press releases the older presses that
    // are still plain taps, so letters typed with two thumbs keep the order
    // in which they were pressed ("press a, press l, lift l, lift a" is "al").
    property var heldKeys: []
    function trackHeldKey(key, down) {
        var i = heldKeys.indexOf(key)
        if (down && i < 0) heldKeys.push(key)
        if (!down && i >= 0) heldKeys.splice(i, 1)
    }
    function rollOver(pressedKey) {
        var keys = heldKeys.slice()
        for (var i = 0; i < keys.length; ++i) if (keys[i] !== pressedKey) keys[i].commitHeldTap()
    }

    // The input context went away (or the panel is hidden) with fingers
    // down: no key may stay pressed, repeat or finish a gesture later.
    function cancelAllPresses() {
        var keys = heldKeys.slice()
        for (var i = 0; i < keys.length; ++i) keys[i].cancelPress()
        heldKeys = []
        glideStartItem = null
        glideActive = false
        glideLastValue = ""
        glidePath = []
        glidePoints = []
        glideTrail.requestPaint()
    }

    Connections {
        target: keyboardBridge
        function onPressesCancelled() { root.cancelAllPresses() }
    }
    // Hidden (the hide button, KWin) with a finger down: same.
    Connections {
        target: root.Window.window
        function onVisibleChanged() { if (!root.Window.window.visible) root.cancelAllPresses() }
    }

    // Finds the character key under a point in root coordinates.
    function keyAt(x, y) {
        var item = keyboardRows
        var p = root.mapToItem(keyboardRows, x, y)
        for (var depth = 0; depth < 8 && item; ++depth) {
            var child = item.childAt(p.x, p.y)
            if (!child) return null
            if (child.label !== undefined && child.special === false && child.label.length > 0) return child
            p = item.mapToItem(child, p.x, p.y)
            item = child
        }
        return null
    }


    // Skin-tone picker (long-press an emoji); a tap outside closes it.
    property var toneChoices: []
    function openTonePicker(base, tones, anchorItem) {
        toneChoices = [base].concat(tones)
        var p = anchorItem.mapToItem(root, 0, 0)
        var w = toneChoices.length * tonePicker.cell
        tonePicker.x = Math.max(4, Math.min(root.width - w - 4, p.x + anchorItem.width / 2 - w / 2))
        tonePicker.y = Math.max(4, p.y - tonePicker.height - 6)
    }

    MouseArea {
        anchors.fill: parent
        z: 299
        visible: root.toneChoices.length > 0
        onPressed: root.toneChoices = []
    }

    Rectangle {
        id: tonePicker
        objectName: "emojiTonePicker"
        readonly property real cell: root.portrait ? 54 : 48
        visible: root.toneChoices.length > 0
        z: 300
        width: root.toneChoices.length * cell
        height: cell + 8
        radius: 14
        color: root.specialKeyColor
        border.width: 1
        border.color: root.borderColor
        Row {
            anchors.centerIn: parent
            Repeater {
                model: root.toneChoices
                delegate: Rectangle {
                    required property string modelData
                    width: tonePicker.cell
                    height: tonePicker.cell
                    radius: 10
                    color: toneMouse.pressed ? root.pressedColor : "transparent"
                    Text { anchors.centerIn: parent; text: modelData; font.pixelSize: root.portrait ? 28 : 24 }
                    MouseArea {
                        id: toneMouse
                        anchors.fill: parent
                        onClicked: {
                            keyboardBridge.insertEmoji(modelData)
                            root.toneChoices = []
                        }
                    }
                }
            }
        }
    }

    property var glidePoints: []
    // The whole finger path of the current glide (the trail keeps only its end).
    property var glidePath: []

    // Centres of the letter keys in root coordinates, for the glide decoder.
    function letterKeyGeometry() {
        var centres = {}
        var keyWidth = 0
        for (var i = 0; i < glideKeys.length; ++i) {
            var k = glideKeys[i]
            if (!k || !k.visible || !k.glideEligible || !k.glideValue) continue
            var p = k.mapToItem(root, k.width / 2, k.height / 2)
            centres[k.glideValue.toLowerCase()] = Qt.point(p.x, p.y)
            if (keyWidth === 0 || k.width < keyWidth) keyWidth = k.width
        }
        return { centres: centres, keyWidth: keyWidth }
    }

    Canvas {
        id: glideTrail
        objectName: "glideTrail"
        anchors.fill: parent
        z: 200
        visible: keyboardBridge.glideTrail && root.glidePoints.length > 1
        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            var pts = root.glidePoints
            if (pts.length < 2) return
            ctx.strokeStyle = root.accentColor
            ctx.lineWidth = 6
            ctx.lineCap = "round"
            ctx.lineJoin = "round"
            ctx.globalAlpha = 0.75
            ctx.beginPath()
            ctx.moveTo(pts[0].x, pts[0].y)
            for (var i = 1; i < pts.length; ++i) ctx.lineTo(pts[i].x, pts[i].y)
            ctx.stroke()
        }
    }

    // When the last key was tapped (ms), for the glide start threshold.
    property real lastTapMs: 0
    // A glide starts after the finger moved half a key (a whole one right
    // after typing, as LatinIME's dynamic gesture threshold): a tap that
    // rolls a little on the key stays a tap.
    function glideStartDistance(item) {
        var recent = Date.now() - lastTapMs < 500
        return Math.max(18, item.width * (recent ? 0.9 : 0.5))
    }

    function updateGlideCandidate(item, x, y) {
        // Only the finger that started the candidate can turn it into a glide
        // (its key keeps the touch grab and reports every move).
        if (!glideStartItem || item !== glideStartItem || !keyboardBridge.glideEnabled || item.consumeRelease && !glideActive) return
        var p = item.mapToItem(root, x, y)
        var dx = p.x - glideStartX
        var dy = p.y - glideStartY
        if (!glideActive && Math.sqrt(dx * dx + dy * dy) > glideStartDistance(item)) {
            glideActive = true
            glideStartItem.consumeRelease = true
            item.consumeRelease = true
            keyboardBridge.beginGlide(glideStartItem.glideValue)
            glideLastValue = glideStartItem.glideValue
            glidePath = [Qt.point(glideStartX, glideStartY)]
        }
        if (!glideActive) return
        if (glidePath.length < 1200) glidePath.push(Qt.point(p.x, p.y))
        item.consumeRelease = true
        // Gboard-style gesture trail (Settings: Gesture trail).
        if (!keyboardBridge.glideTrail) { glidePoints = []; }
        else {
        var points = glidePoints.length === 0 ? [Qt.point(glideStartX, glideStartY)] : glidePoints
        points.push(Qt.point(p.x, p.y))
        if (points.length > 96) points.shift()
        glidePoints = points
        glideTrail.requestPaint()
        }
        var hit = glideHit(p.x, p.y)
        if (hit && hit.glideValue !== glideLastValue) {
            keyboardBridge.glideThrough(hit.glideValue)
            glideLastValue = hit.glideValue
        }
    }

    function finishGlideCandidate(item) {
        if (item !== glideStartItem) return
        if (glideActive) {
            item.consumeRelease = true
            // The path decides (LatinIME-style); the key sequence is the fallback.
            var geometry = letterKeyGeometry()
            var word = keyboardBridge.endGlidePath(glidePath, geometry.centres, geometry.keyWidth)
            // Nothing read from a short stroke: it was a tap that rolled.
            if (word.length === 0 && glidePathLength() < geometry.keyWidth * 1.2) item.triggered()
        }
        glidePath = []
        glideStartItem = null
        glideActive = false
        glideLastValue = ""
        glidePoints = []
        glideTrail.requestPaint()
    }

    component Key: Rectangle {
        id: key

        property string label: ""
        property string iconSource: ""
        property string alternate: ""
        // Gboard-style long-press choices; first is preselected. More than
        // one opens a picker: slide to choose, release to insert.
        property var alternates: []
        // Split layout: keys move left or right of the centre; a key that
        // spans the centre (the space bar) stays for both thumbs.
        readonly property real splitShift: {
            if (!root.splitMode || !parent || parent.absorbsSplit === true) return 0
            var mid = parent.width / 2
            // Only a wide key (the space bar) stays in the middle; the centre
            // key of an odd row (g, р) belongs to the left half.
            if (width > root.baseKeyWidth * 1.8 && x < mid - 1 && x + width > mid + 1) return 0
            return (x + width / 2 <= mid + 0.5) ? -root.splitGap / 2 : root.splitGap / 2
        }
        transform: Translate { x: key.splitShift }
        // Touch-down point (key coordinates) for touch-aware correction.
        property real pressX: width / 2
        property real pressY: height / 2
        property string hint: ""
        property bool choosing: false
        property int choiceIndex: 0
        property real preferredWidth: root.baseKeyWidth
        property real labelPixelSize: root.metrics.fontSize
        property bool special: false
        property bool accent: false
        property bool popupEnabled: keyboardBridge.keyPopups
        property bool longPressEnabled: alternate.length > 0
        property bool consumeRelease: false
        property bool glideEligible: false
        property string glideValue: ""
        // Released by a later press (see root.rollOver); not Shift, ?123 or
        // the globe, whose slides and long presses need the finger.
        property bool rollover: true
        // LatinIME: Space, Backspace and Return have their own sound.
        property string sound: ""

        signal triggered()
        signal longPressed()
        signal pressStarted(real x, real y)
        signal pointerMoved(real x, real y)
        signal pressEnded(real x, real y)
        signal pressCancelled()
        signal alternateChosen(string text)

        // A later press takes this key's tap now (LatinIME releases older
        // pointers), unless it is held, choosing or already a gesture.
        function commitHeldTap() {
            if (!mouse.pressed || mouse.held || key.choosing || key.consumeRelease || !key.rollover) return
            root.abortGlideCandidate(key)
            holdTimer.stop()
            key.consumeRelease = true
            key.triggered()
        }
        function cancelPress() {
            if (!mouse.pressed && !key.choosing) return
            mouse.pressed = false
            mouse.held = false
            mouse.pointId = -1
            key.choosing = false
            key.consumeRelease = true
            holdTimer.stop()
            key.pressCancelled()
        }
        function choiceCellWidth() { return Math.max(key.width * 0.86, 44) }
        // The picker starts at the key's left edge, clamped inside the keyboard.
        function choiceOriginX() {
            var total = choiceCellWidth() * alternates.length
            var left = key.mapToItem(root, 0, 0).x
            var shift = Math.min(0, root.width - (left + total) - 4)
            return Math.max(-left + 4, shift)
        }
        function updateChoice(x) {
            var index = Math.floor((x - choiceOriginX()) / choiceCellWidth())
            choiceIndex = Math.max(0, Math.min(alternates.length - 1, index))
        }

        width: preferredWidth
        height: root.layerKeyHeight
        radius: root.metrics.keyRadius
        border.width: keyboardBridge.keyBorders ? 1 : 0
        border.color: root.borderColor

        color: mouse.pressed
            ? (accent ? root.accentPressedColor : root.pressedColor)
            : (accent
                ? root.accentColor
                : (special ? root.specialKeyColor : root.keyColor))

        Text {
            anchors.centerIn: parent
            visible: key.iconSource.length === 0
            text: key.label
            color: key.accent ? root.backgroundColor : root.textColor
            font.pixelSize: key.labelPixelSize
            font.weight: Font.Medium
        }

        Text {
            // Symbol hint in the corner (Gboard "long press for symbols").
            visible: key.hint.length > 0 && key.iconSource.length === 0
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 4
            text: key.hint
            color: root.textColor
            opacity: 0.55
            font.pixelSize: Math.max(10, root.metrics.fontSize * 0.45)
        }

        Rectangle {
            id: choicePicker
            visible: key.choosing
            z: 110
            x: key.choiceOriginX()
            // Stay inside the panel surface: Wayland cannot draw above it, so
            // for the top row the picker overlaps the key instead of being cut.
            y: (key.choosing, Math.max(-key.mapToItem(root, 0, 0).y + 2, -height - 7))
            width: key.choiceCellWidth() * key.alternates.length
            height: root.metrics.popupHeight
            radius: 14
            color: root.specialKeyColor
            border.width: 1
            border.color: root.borderColor

            Row {
                anchors.fill: parent
                Repeater {
                    model: key.choosing ? key.alternates : []
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        width: key.choiceCellWidth()
                        height: choicePicker.height
                        radius: 12
                        color: index === key.choiceIndex ? root.accentColor : "transparent"
                        Text {
                            anchors.centerIn: parent
                            text: modelData
                            color: index === key.choiceIndex ? root.backgroundColor : root.textColor
                            font.pixelSize: root.metrics.fontSize + 3
                            font.weight: Font.Medium
                        }
                    }
                }
            }
        }

        Image {
            anchors.centerIn: parent
            visible: key.iconSource.length > 0
            source: key.iconSource
            width: 30
            height: 30
            fillMode: Image.PreserveAspectFit
            sourceSize.width: 40
            sourceSize.height: 40
        }

        Rectangle {
            id: popup
            visible: mouse.pressed
                && !key.choosing
                && key.popupEnabled
                && key.label.length > 0
                && key.iconSource.length === 0
            z: 100
            width: Math.max(key.width * 0.82, 58)
            height: root.metrics.popupHeight
            radius: 14
            color: root.specialKeyColor
            border.width: keyboardBridge.keyBorders ? 1 : 0
            border.color: root.borderColor
            anchors.horizontalCenter: parent.horizontalCenter
            objectName: "keyPreview"
            // Clamped inside the panel surface (see choicePicker).
            y: (mouse.pressed, Math.max(-key.mapToItem(root, 0, 0).y + 2, -height - 7))

            Text {
                anchors.centerIn: parent
                text: mouse.held && key.alternate.length > 0 ? key.alternate : key.label
                color: root.textColor
                font.pixelSize: root.metrics.fontSize + 5
                font.weight: Font.Medium
            }
        }

        // One finger per key at a time: overlapping two-thumb taps land on
        // different keys and are handled independently; a second finger on
        // the same key takes over after committing the first one's tap.
        // The area reaches into half of the gap around the key, so no touch
        // between keys is lost. Mouse input keeps working (mouseEnabled).
        MultiPointTouchArea {
            id: mouse
            anchors.fill: parent
            anchors.margins: -root.keyGap / 2
            mouseEnabled: true
            maximumTouchPoints: 5
            property int pointId: -1
            property bool held: false
            property bool pressed: false
            property real lastX: 0
            property real lastY: 0

            // Key coordinates of a touch point of this area.
            function keyX(point) { return point.x + mouse.x }
            function keyY(point) { return point.y + mouse.y }
            function tracked(points) {
                for (var i = 0; i < points.length; ++i) if (points[i].pointId === pointId) return points[i]
                return null
            }
            // A release a little outside the key (a rolling thumb) still counts.
            function inside(x, y) {
                var m = root.keyGap / 2 + 6
                return x >= -m && y >= -m && x <= key.width + m && y <= key.height + m
            }

            Timer {
                id: holdTimer
                interval: keyboardBridge.longPressDelay
                onTriggered: {
                    // Not once the press became a glide, a cursor drag or a
                    // swipe-delete (they set consumeRelease).
                    // And not when the finger has wandered off the press point:
                    // that is a glide starting slowly, not a long press.
                    const slop = key.width * 0.25
                    const moved = Math.abs(mouse.lastX - key.pressX) > slop || Math.abs(mouse.lastY - key.pressY) > slop
                    if (mouse.pressed && key.longPressEnabled && !key.consumeRelease && !moved && mouse.inside(mouse.lastX, mouse.lastY)) {
                        root.abortGlideCandidate(key)
                        mouse.held = true
                        if (key.alternates.length > 1) {
                            key.choiceIndex = 0
                            key.choosing = true
                        } else {
                            key.longPressed()
                        }
                    }
                }
            }

            onPressed: (touchPoints) => {
                const point = touchPoints[touchPoints.length - 1]
                root.rollOver(key)
                if (pressed) key.commitHeldTap()          // a second finger on this key
                pointId = point.pointId
                pressed = true
                held = false
                key.choosing = false
                lastX = keyX(point)
                lastY = keyY(point)
                key.pressX = lastX
                key.pressY = lastY
                keyboardBridge.keyFeedback(key.sound) // Gboard "Sound on keypress"
                key.consumeRelease = false
                root.trackHeldKey(key, true)
                holdTimer.restart()
                key.pressStarted(lastX, lastY)
            }
            onUpdated: (touchPoints) => {
                const point = tracked(touchPoints)
                if (!pressed || !point) return
                lastX = keyX(point)
                lastY = keyY(point)
                if (key.choosing) {
                    key.updateChoice(lastX)
                    return
                }
                key.pointerMoved(lastX, lastY)
            }
            onReleased: (touchPoints) => {
                const point = tracked(touchPoints)
                if (!pressed || (touchPoints.length > 0 && !point)) return   // another finger
                const x = point ? keyX(point) : lastX
                const y = point ? keyY(point) : lastY
                pressed = false
                pointId = -1
                holdTimer.stop()
                root.trackHeldKey(key, false)
                if (key.choosing) {
                    key.choosing = false
                    key.pressEnded(x, y)
                    // Only when the finger lifts on (or near) the picker.
                    const allowance = key.height * 0.6
                    const left = key.choiceOriginX() - allowance
                    const right = key.choiceOriginX() + key.choiceCellWidth() * key.alternates.length + allowance
                    if (x >= left && x <= right && y >= choicePicker.y - allowance && y <= key.height + allowance)
                        key.alternateChosen(key.alternates[key.choiceIndex])
                    return
                }
                key.pressEnded(x, y)
                if (inside(x, y) && !held && !key.consumeRelease) {
                    root.lastTapMs = Date.now()
                    key.triggered()
                }
            }
            onCanceled: (touchPoints) => {
                if (tracked(touchPoints) === null && touchPoints.length > 0) return
                root.trackHeldKey(key, false)
                root.abortGlideCandidate(key)
                if (root.glideStartItem === key) root.cancelAllPresses()   // a glide in progress
                pressed = false
                held = false
                pointId = -1
                key.choosing = false
                holdTimer.stop()
                key.pressCancelled()
            }
        }
    }


    Rectangle {
        id: topToolbar
        objectName: "topToolbar"
        z: 120
        width: root.contentWidth
        height: root.toolbarHeight
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        // Over a photo theme the strip shows the photo, as Gboard's does.
        color: root.photoTheme ? "transparent" : root.backgroundColor

        Item {
            anchors.fill: parent

            Text {
                // Voice status replaces the suggestions while it matters.
                id: voiceStatus
                visible: keyboardBridge.voiceBuilt && !root.toolbarExpanded
                    && (keyboardBridge.voiceState === "recording" || keyboardBridge.voiceState === "recognizing"
                        || keyboardBridge.voiceMessage.length > 0)
                anchors.centerIn: parent
                width: parent.width * 0.7
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                color: root.textColor
                font.pixelSize: root.portrait ? 17 : 15
                text: keyboardBridge.voiceState === "recording" ? qsTr("Listening… tap 🎤 to finish")
                    : keyboardBridge.voiceState === "recognizing" ? qsTr("Recognizing…")
                    : keyboardBridge.voiceMessage
            }

            Rectangle {
                // Gboard/LatinIME "touch again to save": after keeping an
                // unknown word, offer to add it to the personal dictionary.
                id: saveWordChip
                objectName: "saveWordChip"
                visible: keyboardBridge.saveWordCandidate.length > 0 && !root.toolbarExpanded && !voiceStatus.visible
                anchors.centerIn: parent
                width: saveWordText.implicitWidth + 32
                height: parent.height - 10
                radius: height / 2
                color: saveWordMouse.pressed ? root.pressedColor : root.selectedColor
                Text {
                    id: saveWordText
                    anchors.centerIn: parent
                    text: qsTr("＋ Add “%1” to dictionary").arg(keyboardBridge.saveWordCandidate)
                    color: root.textColor
                    font.pixelSize: root.portrait ? 17 : 15
                    font.weight: Font.Medium
                }
                MouseArea {
                    id: saveWordMouse
                    anchors.fill: parent
                    onClicked: keyboardBridge.addWordToDictionary(keyboardBridge.saveWordCandidate)
                }
            }

            Row {
                id: suggestionRow
                visible: keyboardBridge.activePanel === "typing"
                    && keyboardBridge.suggestionsEnabled
                    && keyboardBridge.suggestions.length > 0
                    && !root.toolbarExpanded
                    && !voiceStatus.visible
                    && !saveWordChip.visible
                    && keyboardBridge.morseCode.length === 0
                anchors.centerIn: parent
                spacing: root.keyGap

                Repeater {
                    model: keyboardBridge.suggestions

                    delegate: Rectangle {
                        width: Math.min(root.portrait ? 180 : 220,
                            (root.contentWidth - toolToggle.width - (keyboardBridge.canHideKeyboard ? 2 * hideKeyboardButton.width : 0)
                             - root.keyGap * 4) / Math.max(1, keyboardBridge.suggestions.length))
                        height: root.portrait ? 42 : 38
                        radius: 12
                        color: suggestionMouse.pressed ? root.specialKeyColor : "transparent"

                        Text {
                            anchors.centerIn: parent
                            width: parent.width - 12
                            horizontalAlignment: Text.AlignHCenter
                            // A phrase (Gboard Smart Compose): the words after its
                            // first one are dimmed, like Gboard's grey completion.
                            readonly property bool phrase: modelData.length > 0
                                && modelData === keyboardBridge.phraseSuggestion
                            objectName: phrase ? "phraseChip" : ""
                            textFormat: phrase ? Text.StyledText : Text.PlainText
                            // Gboard: the typed word is quoted when Space would correct it; the
                            // correction itself is bold.
                            text: phrase ? root.phraseMarkup(modelData, keyboardBridge.phraseHeadLength)
                                : (keyboardBridge.autocorrectSuggestion.length > 0 || keyboardBridge.typedWordUnknown)
                                && modelData === keyboardBridge.currentWord
                                ? "\u201C" + modelData + "\u201D" : modelData
                            elide: Text.ElideRight
                            color: root.textColor
                            font.pixelSize: root.portrait ? 17 : 15
                            font.weight: modelData === keyboardBridge.autocorrectSuggestion ? Font.Bold : Font.Medium
                        }

                        MouseArea {
                            id: suggestionMouse
                            anchors.fill: parent
                            property bool forgot: false
                            onPressed: forgot = false
                            // Gboard: long-press a suggestion to remove it.
                            onPressAndHold: {
                                if (modelData.length > 0 && modelData[0].toLowerCase() !== modelData[0].toUpperCase()) {
                                    forgot = true
                                    keyboardBridge.forgetSuggestion(modelData)
                                }
                            }
                            onClicked: {
                                if (forgot) return
                                // selectSuggestion() replaces the model and
                                // destroys this delegate; touch nothing after it.
                                root.toolbarExpanded = false
                                keyboardBridge.selectSuggestion(modelData)
                            }
                        }
                    }
                }

                Rectangle {
                    id: toolToggle
                    width: root.portrait ? 46 : 42
                    height: root.portrait ? 42 : 38
                    radius: 12
                    color: toolToggleMouse.pressed ? root.specialKeyColor : "transparent"
                    Text {
                        anchors.centerIn: parent
                        text: "•••"
                        color: root.textColor
                        font.pixelSize: root.portrait ? 18 : 16
                    }
                    MouseArea {
                        id: toolToggleMouse
                        anchors.fill: parent
                        onClicked: root.toolbarExpanded = true
                    }
                }
            }

            Row {
                // Morse: the keyed dots and dashes and what they type now (tap:
                // type it), then the signs they can still become (tap one to
                // type it instead) - a chart to learn from while keying.
                id: morseStrip
                objectName: "morseStrip"
                visible: keyboardBridge.morseCode.length > 0 && !root.toolbarExpanded && !voiceStatus.visible
                anchors.centerIn: parent
                height: parent.height - 10
                spacing: 6
                readonly property real hintWidth: root.portrait ? 104 : 96
                readonly property int hintRoom: Math.max(0, Math.floor((parent.width - morseNow.width - 40)
                                                                       / (hintWidth + spacing)))

                Rectangle {
                    id: morseNow
                    objectName: "morseNow"
                    width: morseNowText.implicitWidth + 32
                    height: parent.height
                    radius: height / 2
                    color: morseNowMouse.pressed ? root.pressedColor : root.keyColor
                    border.width: 1
                    border.color: root.accentColor
                    Text {
                        id: morseNowText
                        anchors.centerIn: parent
                        text: root.morseGlyphs(keyboardBridge.morseCode) + "   "
                            + (keyboardBridge.morseText.length > 0 ? root.morseLabel(keyboardBridge.morseText) : "?")
                        color: root.textColor
                        font.pixelSize: root.portrait ? 18 : 16
                        font.weight: Font.DemiBold
                    }
                    MouseArea {
                        id: morseNowMouse
                        anchors.fill: parent
                        onClicked: keyboardBridge.sendMorse()
                    }
                }
                Repeater {
                    model: keyboardBridge.morseHints.slice(0, morseStrip.hintRoom)
                    delegate: Rectangle {
                        required property var modelData
                        objectName: "morseHint_" + modelData.text
                        width: morseStrip.hintWidth
                        height: morseStrip.height
                        radius: 12
                        color: hintMouse.pressed ? root.specialKeyColor : "transparent"
                        Text {
                            anchors.centerIn: parent
                            width: parent.width - 8
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            textFormat: Text.StyledText
                            // The part already keyed is dimmed: what is left to key stands out.
                            text: "<b>" + root.escapeMarkup(root.morseLabel(modelData.text)) + "</b> <font color=\""
                                + root.secondaryTextColor + "\">" + root.morseGlyphs(keyboardBridge.morseCode) + "</font>"
                                + root.morseGlyphs(modelData.code.slice(keyboardBridge.morseCode.length))
                            color: root.textColor
                            font.pixelSize: root.portrait ? 16 : 14
                        }
                        MouseArea {
                            id: hintMouse
                            anchors.fill: parent
                            onClicked: keyboardBridge.chooseMorseHint(modelData.text)
                        }
                    }
                }
            }

            Row {
                // Gboard: paste what was just copied, or only the e-mail,
                // web address, phone number, date or number found in it.
                id: clipboardChips
                visible: keyboardBridge.clipboardSuggestion.length > 0 && keyboardBridge.suggestions.length === 0
                    && !saveWordChip.visible && !voiceStatus.visible && !root.toolbarExpanded
                    && keyboardBridge.morseCode.length === 0
                anchors.centerIn: parent
                height: parent.height - 10
                spacing: 8
                readonly property real room: parent.width - 24

                Rectangle {
                    id: clipboardChip
                    objectName: "clipboardChip"
                    width: Math.min(clipboardChipText.implicitWidth + 32,
                                    clipboardChips.room * (keyboardBridge.clipboardParts.length > 0 ? 0.34 : 0.6))
                    height: parent.height
                    radius: height / 2
                    color: clipboardChipMouse.pressed ? root.pressedColor : root.selectedColor
                    Text {
                        id: clipboardChipText
                        anchors.centerIn: parent
                        width: parent.width - 28
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideMiddle
                        text: "\uD83D\uDCCB " + keyboardBridge.clipboardSuggestion.replace(/\s+/g, " ")
                        color: root.textColor
                        font.pixelSize: root.portrait ? 17 : 15
                    }
                    MouseArea {
                        id: clipboardChipMouse
                        anchors.fill: parent
                        onClicked: keyboardBridge.pasteClipboardSuggestion()
                    }
                }

                Repeater {
                    model: keyboardBridge.clipboardParts
                    delegate: Rectangle {
                        objectName: "clipboardPart" + index
                        // The parts share what the whole-text chip leaves.
                        width: Math.min(partText.implicitWidth + 28,
                                        (clipboardChips.room * 0.64 - clipboardChips.spacing * keyboardBridge.clipboardParts.length)
                                        / keyboardBridge.clipboardParts.length)
                        height: clipboardChips.height
                        radius: height / 2
                        color: partMouse.pressed ? root.pressedColor : root.keyColor
                        border.width: 1
                        border.color: root.selectedColor
                        Text {
                            id: partText
                            anchors.centerIn: parent
                            width: parent.width - 24
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideMiddle
                            text: modelData
                            color: root.textColor
                            font.pixelSize: root.portrait ? 16 : 14
                        }
                        MouseArea {
                            id: partMouse
                            anchors.fill: parent
                            onClicked: keyboardBridge.pasteClipboardPart(index)
                        }
                    }
                }
            }

            Row {
                // Not under the save-word chip: with five buttons the middle one
                // sat under the chip and took its tap. (The voice status is
                // text only; the mic here must stay to finish recording.)
                visible: !suggestionRow.visible && !clipboardChips.visible && !saveWordChip.visible && !morseStrip.visible
                anchors.centerIn: parent
                spacing: root.portrait ? 18 : 14

                Rectangle {
                    visible: root.toolbarExpanded && keyboardBridge.suggestions.length > 0
                    width: root.portrait ? 52 : 46
                    height: root.portrait ? 42 : 38
                    radius: 12
                    color: backToolsMouse.pressed ? root.specialKeyColor : "transparent"
                    Text {
                        anchors.centerIn: parent
                        text: "‹"
                        color: root.textColor
                        font.pixelSize: root.portrait ? 28 : 24
                    }
                    MouseArea {
                        id: backToolsMouse
                        anchors.fill: parent
                        onClicked: root.toolbarExpanded = false
                    }
                }

                Rectangle {
                    // Offline dictation: tap to record, tap again to insert.
                    visible: keyboardBridge.voiceBuilt
                    width: root.portrait ? 52 : 46
                    height: root.portrait ? 42 : 38
                    radius: 12
                    color: keyboardBridge.voiceState === "recording" ? "#c0392b"
                         : voiceMouse.pressed ? root.specialKeyColor : "transparent"
                    opacity: keyboardBridge.voiceState === "unavailable" ? 0.55 : 1.0

                    Text {
                        anchors.centerIn: parent
                        text: keyboardBridge.voiceState === "recognizing" ? "…" : "🎤"
                        color: root.textColor
                        font.pixelSize: root.portrait ? 22 : 19
                    }

                    MouseArea {
                        id: voiceMouse
                        anchors.fill: parent
                        onClicked: keyboardBridge.toggleVoice()
                    }
                }

                Repeater {
                    model: keyboardBridge.toolbarActions

                    delegate: Rectangle {
                        required property var modelData
                        readonly property bool selected: keyboardBridge.activePanel === modelData.id
                        width: root.portrait ? 52 : 46
                        height: root.portrait ? 42 : 38
                        radius: 12
                        color: toolbarMouse.pressed
                            ? root.specialKeyColor
                            : (selected ? root.selectedColor : "transparent")
                        border.width: selected && keyboardBridge.keyBorders ? 1 : 0
                        border.color: root.accentColor
                        opacity: modelData.enabled ? 1.0 : 0.40

                        Text {
                            anchors.centerIn: parent
                            text: root.toolbarGlyph(modelData.id)
                            color: selected ? root.accentColor : root.textColor
                            font.pixelSize: root.portrait ? 24 : 21
                            font.weight: Font.Medium
                        }

                        MouseArea {
                            id: toolbarMouse
                            anchors.fill: parent
                            enabled: modelData.enabled
                            onClicked: {
                                root.toolbarExpanded = false
                                if (modelData.id === "resize") {
                                    keyboardBridge.closePanel()
                                    root.resizing = !root.resizing
                                    return
                                }
                                if (keyboardBridge.activePanel === modelData.id) {
                                    keyboardBridge.closePanel()
                                } else {
                                    keyboardBridge.activateToolbarAction(modelData.id)
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            // Gboard ⌄: hide the keyboard (KWin D-Bus, see KeyboardHider).
            id: hideKeyboardButton
            objectName: "hideKeyboardButton"
            visible: keyboardBridge.canHideKeyboard
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: root.portrait ? 46 : 42
            height: root.portrait ? 42 : 38
            radius: 12
            color: hideKeyboardMouse.pressed ? root.specialKeyColor : "transparent"
            Canvas {
                id: chevron
                anchors.centerIn: parent
                width: 20
                height: 12
                property color ink: root.textColor
                onInkChanged: requestPaint()
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    ctx.strokeStyle = ink
                    ctx.lineWidth = 2.4
                    ctx.lineCap = "round"
                    ctx.lineJoin = "round"
                    ctx.beginPath()
                    ctx.moveTo(2, 2)
                    ctx.lineTo(width / 2, height - 2)
                    ctx.lineTo(width - 2, 2)
                    ctx.stroke()
                }
            }
            MouseArea {
                id: hideKeyboardMouse
                anchors.fill: parent
                onClicked: keyboardBridge.hideKeyboard()
            }
        }

        // Gboard "add word": the word, then an optional shortcut, typed on
        // the keyboard's own keys (Settings -> Dictionary -> Add word).
        Rectangle {
            objectName: "wordEntryBar"
            visible: keyboardBridge.wordEntryStep.length > 0
            anchors.fill: parent
            z: 50
            color: root.backgroundColor
            MouseArea { anchors.fill: parent }

            Rectangle {
                id: wordEntryClose
                anchors.left: parent.left
                anchors.leftMargin: root.keyGap
                anchors.verticalCenter: parent.verticalCenter
                width: parent.height - 10
                height: width
                radius: width / 2
                color: wordEntryCloseMouse.pressed ? root.pressedColor : root.keyColor
                Text { anchors.centerIn: parent; text: "\u2715"; color: root.textColor; font.pixelSize: root.portrait ? 18 : 16 }
                MouseArea { id: wordEntryCloseMouse; anchors.fill: parent; onClicked: keyboardBridge.stopWordEntry() }
            }

            Text {
                id: wordEntryPrompt
                anchors.left: wordEntryClose.right
                anchors.leftMargin: root.keyGap * 2
                anchors.verticalCenter: parent.verticalCenter
                text: keyboardBridge.wordEntryStep === "word" ? qsTr("New word") : qsTr("Shortcut (optional)")
                color: root.secondaryTextColor
                font.pixelSize: root.portrait ? 15 : 13
            }

            Rectangle {
                anchors.left: wordEntryPrompt.right
                anchors.leftMargin: root.keyGap * 2
                anchors.right: wordEntryConfirm.left
                anchors.rightMargin: root.keyGap * 2
                anchors.verticalCenter: parent.verticalCenter
                height: parent.height - 10
                radius: height / 2
                color: root.keyColor
                Text {
                    objectName: "wordEntryText"
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    width: parent.width - 28
                    elide: Text.ElideLeft
                    text: keyboardBridge.wordEntryText
                    color: root.textColor
                    font.pixelSize: root.portrait ? 17 : 15
                }
            }

            Rectangle {
                id: wordEntryConfirm
                objectName: "wordEntryConfirm"
                readonly property bool ready: keyboardBridge.wordEntryStep === "shortcut" || keyboardBridge.wordEntryText.length > 0
                anchors.right: parent.right
                anchors.rightMargin: root.keyGap
                anchors.verticalCenter: parent.verticalCenter
                width: parent.height - 10
                height: width
                radius: width / 2
                opacity: ready ? 1 : 0.4
                color: wordEntryConfirmMouse.pressed ? root.pressedColor : root.selectedColor
                Text { anchors.centerIn: parent; text: "\u2713"; color: root.accentColor; font.pixelSize: root.portrait ? 20 : 18 }
                MouseArea { id: wordEntryConfirmMouse; anchors.fill: parent; onClicked: keyboardBridge.confirmWordEntry() }
            }
        }

        // Gboard emoji search: the query typed on the keyboard's own letters
        // and the matching emoji, in place of the suggestion strip.
        Rectangle {
            objectName: "emojiSearchBar"
            visible: keyboardBridge.emojiSearchActive
            anchors.fill: parent
            z: 50
            color: root.backgroundColor
            MouseArea { anchors.fill: parent }          // nothing underneath reacts

            Rectangle {
                id: emojiSearchClose
                objectName: "emojiSearchClose"
                anchors.left: parent.left
                anchors.leftMargin: root.keyGap
                anchors.verticalCenter: parent.verticalCenter
                width: parent.height - 10
                height: width
                radius: width / 2
                color: emojiSearchCloseMouse.pressed ? root.pressedColor : root.keyColor
                Text { anchors.centerIn: parent; text: "\u2715"; color: root.textColor; font.pixelSize: root.portrait ? 18 : 16 }
                MouseArea { id: emojiSearchCloseMouse; anchors.fill: parent; onClicked: keyboardBridge.stopEmojiSearch() }
            }

            Rectangle {
                id: emojiSearchPill
                anchors.left: emojiSearchClose.right
                anchors.leftMargin: root.keyGap
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(parent.width * 0.32, 320)
                height: parent.height - 10
                radius: height / 2
                color: root.keyColor
                Text {
                    objectName: "emojiSearchText"
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    width: parent.width - 28
                    elide: Text.ElideLeft
                    text: keyboardBridge.emojiSearchText
                    color: root.textColor
                    font.pixelSize: root.portrait ? 17 : 15
                }
                Text {
                    visible: keyboardBridge.emojiSearchText.length === 0
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    text: qsTr("Search emoji")
                    color: root.secondaryTextColor
                    font.pixelSize: root.portrait ? 16 : 14
                }
            }

            ListView {
                anchors.left: emojiSearchPill.right
                anchors.leftMargin: root.keyGap
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                orientation: ListView.Horizontal
                clip: true
                model: keyboardBridge.emojiSearchResults
                delegate: Rectangle {
                    required property string modelData
                    width: height
                    height: ListView.view.height
                    color: resultMouse.pressed ? root.pressedColor : "transparent"
                    radius: 10
                    Text { anchors.centerIn: parent; text: modelData; font.pixelSize: root.portrait ? 28 : 24 }
                    MouseArea { id: resultMouse; anchors.fill: parent; onClicked: keyboardBridge.insertEmoji(modelData) }
                }
            }
        }
    }

    Rectangle {
        id: languageChooserPanel
        z: 150
        visible: keyboardBridge.activePanel === "language"
        // Touches on the panel never reach the keys underneath.
        MouseArea { anchors.fill: parent; z: -1; onPressed: (mouse) => mouse.accepted = true }

        width: Math.min(420, Math.max(330, root.contentWidth * 0.30))
        height: (root.portrait ? 50 : 44)
            + keyboardBridge.languageCodes.length * (root.portrait ? 58 : 50)
            + root.keyGap * 5

        // Above the globe key, or the space bar when the globe is hidden.
        readonly property Item anchorKey: globeKey.visible ? globeKey : spaceKey
        x: Math.max(
            root.metrics.outerMargin,
            Math.min(
                root.width - width - root.metrics.outerMargin,
                anchorKey.mapToItem(root, 0, 0).x + anchorKey.width / 2 - width / 2
            )
        )
        y: Math.max(
            root.toolbarHeight + root.metrics.topPadding,
            anchorKey.mapToItem(root, 0, 0).y - height - root.keyGap
        )

        radius: 18
        color: root.panelColor
        border.width: 1
        border.color: root.borderColor

        Column {
            anchors.fill: parent
            anchors.margins: root.keyGap
            spacing: root.keyGap

            Item {
                width: parent.width
                height: root.portrait ? 42 : 36

                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Keyboard language")
                    color: root.textColor
                    font.pixelSize: root.portrait ? 19 : 17
                    font.weight: Font.Medium
                }

                Rectangle {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.height
                    height: parent.height
                    radius: height / 2
                    color: closeLanguageMouse.pressed ? root.specialKeyColor : "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: "×"
                        color: root.textColor
                        font.pixelSize: root.portrait ? 26 : 23
                    }

                    MouseArea {
                        id: closeLanguageMouse
                        anchors.fill: parent
                        onClicked: keyboardBridge.closePanel()
                    }
                }
            }

            Repeater {
                model: keyboardBridge.languageCodes

                delegate: Rectangle {
                    width: parent.width
                    height: root.portrait ? 58 : 50
                    radius: 13
                    color: keyboardBridge.languageCode === modelData
                        ? root.selectedColor
                        : (languageMouse.pressed ? root.keyColor : "transparent")

                    Rectangle {
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: root.portrait ? 38 : 34
                        height: width
                        radius: width / 2
                        color: keyboardBridge.languageCode === modelData ? root.selectedColor : root.keyColor

                        Text {
                            anchors.centerIn: parent
                            text: modelData.toUpperCase()
                            color: root.textColor
                            font.pixelSize: root.portrait ? 13 : 12
                            font.weight: Font.Medium
                        }
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: root.portrait ? 60 : 54
                        anchors.verticalCenter: parent.verticalCenter
                        text: keyboardBridge.languageLabels[index]
                        color: root.textColor
                        font.pixelSize: root.portrait ? 18 : 16
                        font.weight: Font.Medium
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        visible: keyboardBridge.languageCode === modelData
                        text: "✓"
                        color: root.accentColor
                        font.pixelSize: 22
                    }

                    MouseArea {
                        id: languageMouse
                        anchors.fill: parent
                        onClicked: {
                            keyboardBridge.setLanguage(modelData)
                            keyboardBridge.closePanel()
                        }
                    }
                }
            }
        }
    }

    Column {
        id: keyboardRows
        objectName: "keyboardRows"
        // Above the toolbar (z 120) so key previews and long-press pickers of
        // the top row are not drawn underneath it; panels (140+) stay on top.
        z: 130
        x: keyboardBridge.layoutMode === "left" ? root.metrics.outerMargin
           : keyboardBridge.layoutMode === "right" ? root.width - width - root.metrics.outerMargin
           : (root.width - width) / 2
        anchors {
            top: parent.top
            topMargin: root.toolbarHeight + root.metrics.topPadding
        }
        spacing: root.keyGap

        Row {
            // Gboard "Emoji fast-access row": recently used emoji.
            visible: root.emojiRowShown
            spacing: root.keyGap
            Repeater {
                model: keyboardBridge.emojiRowEmojis
                delegate: Key {
                    required property string modelData
                    objectName: "emojiRowKey_" + modelData
                    preferredWidth: root.dynamicKeyWidth(10)
                    label: modelData
                    special: true
                    onTriggered: keyboardBridge.insertEmoji(modelData)
                }
            }
        }
        Row {
            // Gboard "Number row" preference.
            visible: root.numberRowShown
            spacing: root.keyGap
            Repeater {
                model: ["1", "2", "3", "4", "5", "6", "7", "8", "9", "0"]
                delegate: Key {
                    required property string modelData
                    objectName: "numberKey_" + modelData
                    preferredWidth: root.dynamicKeyWidth(10)
                    label: modelData
                    special: true
                    onTriggered: keyboardBridge.tapText(modelData)
                }
            }
        }
        Column {
            // Number pad for number/phone fields (Gboard behaviour).
            visible: root.numpadShown
            spacing: root.keyGap
            anchors.horizontalCenter: parent.horizontalCenter
            Repeater {
                model: [["1", "2", "3"], ["4", "5", "6"], ["7", "8", "9"],
                        [keyboardBridge.inputPurpose === "phone" ? "+" : ",", "0", "⌫"]]
                delegate: Row {
                    required property var modelData
                    spacing: root.keyGap
                    Repeater {
                        model: modelData
                        delegate: Key {
                            required property string modelData
                            objectName: "numpadKey_" + modelData
                            preferredWidth: (root.contentWidth - root.keyGap * 2) / 3
                            label: modelData
                            special: modelData === "⌫"
                            sound: modelData === "⌫" ? "delete" : ""
                            onTriggered: modelData === "⌫" ? keyboardBridge.backspace() : keyboardBridge.tapText(modelData)
                        }
                    }
                }
            }
        }

        Row {
            // Gboard's Morse code layout: a big dot and dash key, Backspace
            // beside them; the bottom row stays (Space sends the keyed sign).
            id: morseKeys
            objectName: "morseKeys"
            visible: root.morseShown
            property bool absorbsSplit: true
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: root.keyGap
            readonly property real blockHeight: root.layerKeyHeight * 3 + root.keyGap * 2
            readonly property real sideWidth: root.baseKeyWidth * 1.5

            Key {
                objectName: "morseDotKey"
                preferredWidth: (root.contentWidth - morseKeys.sideWidth - root.keyGap * 2) / 2
                height: morseKeys.blockHeight
                label: "\u2022"
                labelPixelSize: root.metrics.fontSize * 2.4
                popupEnabled: false
                onTriggered: keyboardBridge.morseDot()
            }
            Key {
                objectName: "morseDashKey"
                preferredWidth: (root.contentWidth - morseKeys.sideWidth - root.keyGap * 2) / 2
                height: morseKeys.blockHeight
                label: "\u2014"
                labelPixelSize: root.metrics.fontSize * 2.4
                popupEnabled: false
                onTriggered: keyboardBridge.morseDash()
            }
            Key {
                objectName: "morseBackspaceKey"
                preferredWidth: morseKeys.sideWidth
                height: morseKeys.blockHeight
                special: true
                popupEnabled: false
                longPressEnabled: true
                sound: "delete"
                iconSource: "qrc:/tastra/icons/backspace.svg"
                onTriggered: keyboardBridge.backspace()
                Timer {
                    id: morseBackspaceRepeat
                    interval: 75
                    repeat: true
                    onTriggered: keyboardBridge.backspace()
                }
                onLongPressed: {
                    consumeRelease = true
                    keyboardBridge.backspace()
                    morseBackspaceRepeat.start()
                }
                onPressEnded: (x, y) => morseBackspaceRepeat.stop()
                onPressCancelled: morseBackspaceRepeat.stop()
            }
        }

        Row {
            visible: !root.numpadShown && !root.morseShown
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: root.keyGap

            Repeater {
                model: keyboardBridge.symbolsActive
                    ? keyboardBridge.symbolRow1
                    : keyboardBridge.row1

                delegate: Key {
                    id: row1LetterKey
                    preferredWidth: root.dynamicKeyWidth(keyboardBridge.symbolsActive ? 10 : keyboardBridge.row1.length)
                    label: keyboardBridge.symbolsActive
                        ? modelData
                        : (keyboardBridge.uppercase ? modelData.toUpperCase() : modelData)
                    alternate: keyboardBridge.symbolsActive ? "" : keyboardBridge.alternateForKey(modelData)
                    alternates: (keyboardBridge.uppercase, keyboardBridge.symbolsActive, keyboardBridge.symbolPage, keyboardBridge.alternatesForKey(modelData))
                    hint: keyboardBridge.symbolsActive ? "" : (keyboardBridge.symbolHints, keyboardBridge.symbolHintForKey(modelData))
                    longPressEnabled: alternates.length > 0
                    onAlternateChosen: (text) => keyboardBridge.tapAlternateText(text)
                    glideEligible: !keyboardBridge.symbolsActive
                    glideValue: modelData
                    Component.onCompleted: root.registerGlideKey(row1LetterKey)
                    Component.onDestruction: root.unregisterGlideKey(row1LetterKey)
                    onPressStarted: (x, y) => root.beginGlideCandidate(row1LetterKey, x, y)
                    onPointerMoved: (x, y) => root.updateGlideCandidate(row1LetterKey, x, y)
                    onPressEnded: root.finishGlideCandidate(row1LetterKey)

                    onTriggered: {
                        if (keyboardBridge.symbolsActive) keyboardBridge.tapText(modelData)
                        else keyboardBridge.tapLetterAt(modelData, (pressX - width / 2) / width, (pressY - height / 2) / height)
                    }
                    onLongPressed: keyboardBridge.tapAlternateText(alternates[0])
                }
            }
        }

        Row {
            visible: !root.numpadShown && !root.morseShown
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: root.keyGap

            Repeater {
                model: keyboardBridge.symbolsActive
                    ? keyboardBridge.symbolRow2
                    : keyboardBridge.row2

                delegate: Key {
                    id: row2LetterKey
                    preferredWidth: root.dynamicKeyWidth(keyboardBridge.symbolsActive ? 9 : keyboardBridge.row2.length)
                    label: keyboardBridge.symbolsActive
                        ? modelData
                        : (keyboardBridge.uppercase ? modelData.toUpperCase() : modelData)
                    alternate: keyboardBridge.symbolsActive ? "" : keyboardBridge.alternateForKey(modelData)
                    alternates: (keyboardBridge.uppercase, keyboardBridge.symbolsActive, keyboardBridge.symbolPage, keyboardBridge.alternatesForKey(modelData))
                    hint: keyboardBridge.symbolsActive ? "" : (keyboardBridge.symbolHints, keyboardBridge.symbolHintForKey(modelData))
                    longPressEnabled: alternates.length > 0
                    onAlternateChosen: (text) => keyboardBridge.tapAlternateText(text)
                    glideEligible: !keyboardBridge.symbolsActive
                    glideValue: modelData
                    Component.onCompleted: root.registerGlideKey(row2LetterKey)
                    Component.onDestruction: root.unregisterGlideKey(row2LetterKey)
                    onPressStarted: (x, y) => root.beginGlideCandidate(row2LetterKey, x, y)
                    onPointerMoved: (x, y) => root.updateGlideCandidate(row2LetterKey, x, y)
                    onPressEnded: root.finishGlideCandidate(row2LetterKey)

                    onTriggered: {
                        if (keyboardBridge.symbolsActive) keyboardBridge.tapText(modelData)
                        else keyboardBridge.tapLetterAt(modelData, (pressX - width / 2) / width, (pressY - height / 2) / height)
                    }
                    onLongPressed: keyboardBridge.tapAlternateText(alternates[0])
                }
            }
        }

        Row {
            visible: !root.numpadShown && !root.morseShown
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: root.keyGap

            Key {
                id: shiftKey
                objectName: "shiftKey"
                visible: !keyboardBridge.symbolsActive
                preferredWidth: root.baseKeyWidth * 1.25
                special: true
                popupEnabled: false
                iconSource: keyboardBridge.capsLock
                    ? "qrc:/tastra/icons/shift-lock.svg"
                    : "qrc:/tastra/icons/shift.svg"
                rollover: false
                // Gboard: touch Shift and slide onto a letter for one capital.
                // The slide starts a little past the key's edge, so a tap that
                // wobbles over it stays a Shift tap.
                property bool sliding: false
                property bool slideTurnedShiftOn: false
                onPressStarted: (x, y) => { sliding = false; slideTurnedShiftOn = false }
                onPointerMoved: (x, y) => {
                    var m = root.keyGap / 2 + 10
                    if (!sliding && (x < -m || x > width + m || y < -m || y > height + m)) {
                        sliding = true
                        consumeRelease = true
                        if (!keyboardBridge.uppercase) {
                            keyboardBridge.shift()
                            slideTurnedShiftOn = true
                        }
                    }
                }
                onPressEnded: (x, y) => {
                    if (!sliding) return
                    sliding = false
                    var p = mapToItem(root, x, y)
                    var target = root.keyAt(p.x, p.y)
                    if (target && target.glideValue && target.glideValue.length > 0) keyboardBridge.tapLetter(target.glideValue)
                    else if (slideTurnedShiftOn) keyboardBridge.cancelOneShotShift()    // slid to nothing
                }
                onPressCancelled: {
                    if (sliding && slideTurnedShiftOn) keyboardBridge.cancelOneShotShift()
                    sliding = false
                }
                onTriggered: keyboardBridge.shift()
            }

            Key {
                // Symbols layer: switch between "?123" and "=\<" (Gboard).
                objectName: "symbolPageKey"
                rollover: false
                visible: keyboardBridge.symbolsActive
                preferredWidth: root.baseKeyWidth * 1.25
                special: true
                popupEnabled: false
                label: keyboardBridge.symbolPage === 0 ? "=\\<" : "?123"
                onTriggered: keyboardBridge.toggleSymbolPage()
            }

            Repeater {
                model: keyboardBridge.symbolsActive
                    ? keyboardBridge.symbolRow3
                    : keyboardBridge.row3

                delegate: Key {
                    id: row3LetterKey
                    preferredWidth: {
                        // Both layers now have a key on each side (Shift or the
                        // symbol-page key, and Backspace).
                        var count = keyboardBridge.symbolsActive ? keyboardBridge.symbolRow3.length : keyboardBridge.row3.length
                        var sideWidth = root.baseKeyWidth * 2.5
                        var gaps = count + 1
                        return (root.contentWidth - sideWidth - root.keyGap * gaps) / count
                    }
                    label: keyboardBridge.symbolsActive
                        ? modelData
                        : (keyboardBridge.uppercase ? modelData.toUpperCase() : modelData)
                    alternate: keyboardBridge.symbolsActive ? "" : keyboardBridge.alternateForKey(modelData)
                    alternates: (keyboardBridge.uppercase, keyboardBridge.symbolsActive, keyboardBridge.symbolPage, keyboardBridge.alternatesForKey(modelData))
                    hint: keyboardBridge.symbolsActive ? "" : (keyboardBridge.symbolHints, keyboardBridge.symbolHintForKey(modelData))
                    longPressEnabled: alternates.length > 0
                    onAlternateChosen: (text) => keyboardBridge.tapAlternateText(text)
                    glideEligible: !keyboardBridge.symbolsActive
                    glideValue: modelData
                    Component.onCompleted: root.registerGlideKey(row3LetterKey)
                    Component.onDestruction: root.unregisterGlideKey(row3LetterKey)
                    onPressStarted: (x, y) => root.beginGlideCandidate(row3LetterKey, x, y)
                    onPointerMoved: (x, y) => root.updateGlideCandidate(row3LetterKey, x, y)
                    onPressEnded: root.finishGlideCandidate(row3LetterKey)

                    onTriggered: {
                        if (keyboardBridge.symbolsActive) keyboardBridge.tapText(modelData)
                        else keyboardBridge.tapLetterAt(modelData, (pressX - width / 2) / width, (pressY - height / 2) / height)
                    }
                    onLongPressed: keyboardBridge.tapAlternateText(alternates[0])
                }
            }

            Key {
                id: backspaceKey
                sound: "delete"
                property real dragStartX: 0
                property int dragStep: 0
                preferredWidth: root.baseKeyWidth * 1.25
                special: true
                popupEnabled: false
                longPressEnabled: true
                iconSource: "qrc:/tastra/icons/backspace.svg"
                objectName: "backspaceKey"
                onTriggered: keyboardBridge.backspace()
                // Gboard: holding Backspace keeps deleting and speeds up.
                Timer {
                    id: backspaceRepeat
                    interval: 75
                    repeat: true
                    property int ticks: 0
                    onTriggered: {
                        ticks += 1
                        if (ticks === 12) interval = 40
                        keyboardBridge.backspace()
                    }
                }
                onLongPressed: {
                    consumeRelease = true
                    keyboardBridge.backspace()
                    backspaceRepeat.ticks = 0
                    backspaceRepeat.interval = 75
                    backspaceRepeat.start()
                }
                onPressEnded: (x, y) => {
                    backspaceRepeat.stop()
                    keyboardBridge.endDeleteGesture()
                }
                onPressCancelled: {
                    backspaceRepeat.stop()
                    keyboardBridge.cancelDeleteGesture()
                }
                onPressStarted: (x, y) => {
                    backspaceRepeat.stop()
                    dragStartX = x
                    dragStep = 0
                }
                onPointerMoved: (x, y) => {
                    var step = Math.max(0, Math.floor((dragStartX - x) / Math.max(18, width * 0.20)))
                    if (keyboardBridge.deleteGestureSelects) {
                        // Gboard: the slide selects words, sliding back takes
                        // them out again, release deletes the selection.
                        if (step !== dragStep) {
                            backspaceRepeat.stop()
                            consumeRelease = true
                            keyboardBridge.deleteGestureTo(step)
                            dragStep = step
                        }
                    } else if (step > dragStep) {
                        backspaceRepeat.stop()          // swipe-delete takes over
                        consumeRelease = true
                        keyboardBridge.backspaceRepeated(step - dragStep)
                        dragStep = step
                    }
                }
            }
        }

        Row {
            // Split: the space bar grows across the gap so both thumbs reach it;
            // the row itself spreads the halves, so its keys are not shifted.
            property bool absorbsSplit: true
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: root.keyGap

            Key {
                objectName: "symbolsKey"
                rollover: false
                preferredWidth: root.baseKeyWidth * 1.35
                special: true
                popupEnabled: false
                label: keyboardBridge.symbolsActive ? "ABC" : "?123"
                // Gboard fast symbols: touch ?123, slide onto a symbol, release.
                property bool sliding: false
                onPressStarted: (x, y) => { sliding = false }
                onPointerMoved: (x, y) => {
                    if (!sliding && !keyboardBridge.symbolsActive && (y < -8 || x > width + 8)) {
                        sliding = true
                        consumeRelease = true
                        keyboardBridge.toggleSymbols()
                    }
                }
                onPressEnded: (x, y) => {
                    if (!sliding) return
                    sliding = false
                    var p = mapToItem(root, x, y)
                    var target = root.keyAt(p.x, p.y)
                    if (target) keyboardBridge.tapText(target.label)
                    if (keyboardBridge.symbolsActive) keyboardBridge.toggleSymbols()
                }
                onTriggered: keyboardBridge.toggleSymbols()
            }

            Key {
                id: commaKey
                objectName: "commaKey"
                readonly property string glyph: keyboardBridge.inputPurpose === "email" ? "@"
                    : keyboardBridge.inputPurpose === "url" ? "/" : ","
                preferredWidth: root.baseKeyWidth
                label: glyph
                // Gboard: touch and hold the comma for emoji, settings and the
                // one-handed keyboard (slide onto one, release).
                readonly property var actions: ["\u{1F60A}", "\u2699", "\u270B"]
                alternates: actions
                longPressEnabled: true
                onAlternateChosen: (text) => {
                    if (text === actions[0]) keyboardBridge.activateToolbarAction("emoji")
                    else if (text === actions[1]) keyboardBridge.activateToolbarAction("settings")
                    else keyboardBridge.toggleOneHanded()
                }
                onTriggered: keyboardBridge.tapText(glyph)
            }

            Key {
                id: globeKey
                objectName: "globeKey"
                visible: keyboardBridge.showLanguageKey
                rollover: false
                preferredWidth: root.baseKeyWidth
                label: "🌐"
                popupEnabled: false
                longPressEnabled: true
                onTriggered: keyboardBridge.nextLanguage()
                onLongPressed: keyboardBridge.openLanguagePanel()
            }

            Key {
                // Gboard "Show emoji switch key".
                id: emojiKey
                objectName: "emojiKey"
                visible: keyboardBridge.showEmojiKey
                rollover: false
                preferredWidth: root.baseKeyWidth
                label: "\u{1F60A}"
                popupEnabled: false
                onTriggered: keyboardBridge.activateToolbarAction("emoji")
            }

            Key {
                id: spaceKey
                objectName: "spaceKey"
                sound: "space"
                property real dragStartX: 0
                property int cursorStep: 0
                // The space bar takes the place of a hidden globe / emoji key.
                preferredWidth: root.baseKeyWidth * 4.05 + root.splitGap
                    + (keyboardBridge.showLanguageKey || keyboardBridge.showEmojiKey ? 0 : root.baseKeyWidth + root.keyGap)
                label: keyboardBridge.languageLabel
                popupEnabled: false
                longPressEnabled: true
                onTriggered: keyboardBridge.space()
                onLongPressed: keyboardBridge.openLanguagePanel()
                onPressStarted: (x, y) => {
                    dragStartX = x
                    cursorStep = 0
                }
                onPointerMoved: (x, y) => {
                    // A dead zone of half a key first, so a rolling thumb types
                    // its space; then one cursor step per step width.
                    var dx = x - dragStartX
                    var deadZone = root.baseKeyWidth * 0.5
                    var stepWidth = Math.max(24, width * 0.08)
                    var nextStep = Math.abs(dx) < deadZone ? 0
                        : (dx > 0 ? 1 : -1) * (1 + Math.floor((Math.abs(dx) - deadZone) / stepWidth))
                    if (cursorStep === 0 && nextStep === 0) return
                    var delta = nextStep - cursorStep
                    if (delta !== 0) {
                        consumeRelease = true
                        keyboardBridge.moveCursor(delta)
                        cursorStep = nextStep
                    }
                }
            }

            Key {
                preferredWidth: root.baseKeyWidth
                label: "."
                // Gboard: long-press the period for punctuation.
                alternates: keyboardBridge.symbolsActive ? [] : (keyboardBridge.inputPurpose, keyboardBridge.alternatesForKey("."))
                longPressEnabled: alternates.length > 0
                onAlternateChosen: (text) => keyboardBridge.tapAlternateText(text)
                onTriggered: keyboardBridge.tapText(".")
            }

            Key {
                preferredWidth: root.baseKeyWidth * 1.35
                accent: true
                popupEnabled: false
                sound: "return"
                iconSource: "qrc:/tastra/icons/enter.svg"
                onTriggered: keyboardBridge.enter()
            }
        }
    }

    Rectangle {
        id: panelSurface

        // Panels are opaque to touch: a tap on an empty part of a panel must
        // never fall through to the key underneath.
        MouseArea {
            objectName: "panelInputBlocker"
            anchors.fill: parent
            z: -1
            onPressed: (mouse) => mouse.accepted = true
        }
        z: 140
        visible: keyboardBridge.activePanel !== "typing"
            && keyboardBridge.activePanel !== "language"
        width: root.contentWidth
        height: root.metrics.panelHeight
        anchors.top: parent.top
        anchors.topMargin: root.toolbarHeight
        anchors.horizontalCenter: parent.horizontalCenter
        color: root.panelColor
        radius: 16
        border.width: keyboardBridge.keyBorders ? 1 : 0
        border.color: root.borderColor

        Item {
            id: panelHeader
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: root.keyGap * 2
            height: root.portrait ? 42 : 36

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: {
                    if (keyboardBridge.activePanel === "clipboard") return qsTr("Clipboard")
                    if (keyboardBridge.activePanel === "emoji") return qsTr("Emoji")
                    if (keyboardBridge.activePanel === "text-editing") return qsTr("Text editing")
                    if (keyboardBridge.activePanel === "settings") return qsTr("Settings")
                    return ""
                }
                color: root.textColor
                font.pixelSize: root.portrait ? 20 : 18
                font.weight: Font.DemiBold
            }

            Rectangle {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: parent.height
                height: parent.height
                radius: height / 2
                color: closePanelMouse.pressed ? root.specialKeyColor : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "×"
                    color: root.textColor
                    font.pixelSize: root.portrait ? 26 : 23
                }

                MouseArea {
                    id: closePanelMouse
                    anchors.fill: parent
                    onClicked: keyboardBridge.closePanel()
                }
            }
        }

        Item {
            id: panelBody
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: panelHeader.bottom
            anchors.bottom: parent.bottom
            anchors.margins: root.keyGap * 2

            // Built only while open: compiling the panels was most of every
            // keyboard start (docs/ledger.md, startup time).
            Loader {
                objectName: "panelLoader_clipboard"
                anchors.fill: parent
                active: keyboardBridge.activePanel === "clipboard"
                source: "ClipboardPanel.qml"
            }

            // Built only while open: compiling the panels was most of every
            // keyboard start (docs/ledger.md, startup time).
            Loader {
                objectName: "panelLoader_emoji"
                anchors.fill: parent
                active: keyboardBridge.activePanel === "emoji"
                source: "EmojiPanel.qml"
            }

            // Built only while open: compiling the panels was most of every
            // keyboard start (docs/ledger.md, startup time).
            Loader {
                objectName: "panelLoader_text-editing"
                anchors.centerIn: parent
                active: keyboardBridge.activePanel === "text-editing"
                source: "TextEditingPanel.qml"
            }

            // Built only while open: compiling the panels was most of every
            // keyboard start (docs/ledger.md, startup time).
            Loader {
                objectName: "panelLoader_settings"
                anchors.fill: parent
                active: keyboardBridge.activePanel === "settings"
                source: "SettingsPanel.qml"
            }
        }
    }

    Rectangle {
        id: resizeFrame
        objectName: "resizeFrame"
        visible: root.resizing
        z: 300
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.top: parent.top
        anchors.topMargin: root.toolbarHeight
        color: Qt.rgba(root.backgroundColor.r, root.backgroundColor.g, root.backgroundColor.b, 0.82)
        border.width: 2
        border.color: root.accentColor
        // Nothing underneath reacts while resizing.
        MouseArea { anchors.fill: parent }

        Rectangle {
            id: resizeBar
            anchors.top: parent.top
            anchors.topMargin: 10
            anchors.horizontalCenter: parent.horizontalCenter
            width: 96; height: 8; radius: 4
            color: root.accentColor
        }
        MouseArea {
            // Drag the top: the keyboard grows or shrinks by what the finger
            // moved, measured from the bottom edge, which KWin keeps fixed.
            id: resizeDrag
            objectName: "resizeDrag"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: Math.max(56, root.keyHeight)
            property real startDistance: 0
            property real startScale: 1
            property real startKeyHeight: 1
            readonly property int rows: 4 + (root.numberRowSpace ? 1 : 0) + (root.emojiRowShown ? 1 : 0)
            onPressed: (mouse) => {
                startDistance = root.height - mapToItem(root, mouse.x, mouse.y).y
                startScale = keyboardBridge.keyScale
                startKeyHeight = root.keyHeight
            }
            onPositionChanged: (mouse) => {
                const distance = root.height - mapToItem(root, mouse.x, mouse.y).y
                const keyHeight = startKeyHeight + (distance - startDistance) / rows
                keyboardBridge.setKeyScale(startScale * keyHeight / startKeyHeight)
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: resizeBar.bottom
            anchors.topMargin: 14
            text: qsTr("Drag the bar to change the height")
            color: root.textColor
            font.pixelSize: root.portrait ? 17 : 15
        }

        Row {
            anchors.centerIn: parent
            spacing: 16
            PanelButton {
                objectName: "resizeReset"
                width: root.portrait ? 150 : 130
                label: qsTr("Reset")
                onTriggered: keyboardBridge.setKeyScale(1.0)
            }
            PanelButton {
                objectName: "resizeDone"
                width: root.portrait ? 150 : 130
                accent: true
                label: "\u2713"
                onTriggered: root.resizing = false
            }
        }
    }

    Connections {
        target: keyboardBridge
        function onPressesCancelled() { root.resizing = false }
    }
}
