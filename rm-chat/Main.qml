import QtQuick
import QtQuick.Window
import rm_chat_module

Window {
    id: root

    readonly property bool screenshotMode: cfgScreenshot.length > 0
    // Offscreen "Screen" is 800x600; pin the real panel size there or pen
    // coordinates land outside the window.
    readonly property bool pinSize: screenshotMode || cfgOffscreen

    width: pinSize ? 1404 : Screen.width
    height: pinSize ? 1872 : Screen.height
    visible: true
    title: "AI Chat"
    color: "white"

    property bool showSettings: !config.configured
    property bool kbMode: false          // false = handwriting, true = keyboard
    property int pendingUserIndex: -1
    property bool pendingImage: false

    Rectangle { anchors.fill: parent; color: "white" }

    ListModel { id: messages }

    Component.onCompleted: {
        const saved = JSON.parse(config.loadHistoryJson());
        for (const m of saved)
            messages.append(m);
        list.positionViewAtEnd();
    }

    function persistHistory() {
        const arr = [];
        for (let i = 0; i < messages.count; i++) {
            const m = messages.get(i);
            arr.push({ role: m.role, text: m.text });
        }
        config.saveHistoryJson(JSON.stringify(arr));
    }

    function historyForApi() {
        const arr = [];
        for (let i = 0; i < messages.count; i++) {
            const m = messages.get(i);
            if (m.role === "user" || m.role === "assistant")
                arr.push({ role: m.role, content: m.text });
        }
        return arr.slice(-cfgHistoryLimit);
    }

    // E-paper keeps showing old ink after a repaint: a partial-update waveform
    // cannot drive black pixels back to white. Briefly painting the pad solid
    // black forces the panel to do a full refresh of that region, which does
    // clear it. Same trick the picture frame uses between pictures.
    function clearPad() {
        if (cfgEpaperBlink) epaper.blinkLater();
        canvas.clear();
        typedField.text = "";

        // Erasing a QQuickPaintedItem's contents changes no node geometry, so
        // the e-paper backend sees no damage and leaves the old pixels on the
        // panel. Removing the item from the scene DOES produce damage - that
        // is exactly why "New" clears the chat history cleanly - so hide the
        // canvas for a frame and bring it back empty.
        // (Hiding this child item is safe; hiding root.contentItem is what
        // previously destabilised the display pipeline.)
        canvas.visible = false;
        redrawStep.restart();
    }

    function doSend() {
        if (chat.busy || !config.configured)
            return;
        const img = canvas.exportPngBase64();
        const typed = kbMode ? typedField.text.trim() : "";
        if (img === "" && typed === "")
            return;

        pendingImage = img !== "";
        const hist = historyForApi();
        messages.append({ role: "user",
                          text: typed !== "" ? typed : "(handwritten)" });
        pendingUserIndex = messages.count - 1;
        list.positionViewAtEnd();

        chat.send(config.apiBase, config.apiKey, config.model,
                  config.systemPrompt, hist, img, typed);
        clearPad();
    }

    Connections {
        target: chat
        function onReplyReady(text, transcription) {
            if (root.pendingImage && transcription !== "" &&
                root.pendingUserIndex >= 0 && root.pendingUserIndex < messages.count) {
                messages.setProperty(root.pendingUserIndex, "text",
                                     "» " + transcription);
            }
            messages.append({ role: "assistant", text: text });
            root.pendingUserIndex = -1;
            root.persistHistory();
            list.positionViewAtEnd();
        }
        function onFailed(error) {
            messages.append({ role: "error", text: error });
            root.pendingUserIndex = -1;
            root.persistHistory();
            list.positionViewAtEnd();
        }
    }

    Connections {
        target: pen
        function onSample(x, y, down) {
            canvas.penSample(x, y, down);
        }
    }

    // ================================================================ header
    Rectangle {
        id: header
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: 120
        color: "black"

        Text {
            anchors { left: parent.left; leftMargin: 40; verticalCenter: parent.verticalCenter }
            color: "white"
            font.pixelSize: 42
            font.bold: true
            text: "AI Chat"
        }

        Text {
            anchors { left: parent.left; leftMargin: 260; verticalCenter: parent.verticalCenter }
            color: "#b0b0b0"
            font.pixelSize: 26
            text: chat.busy ? "thinking…" : config.model
        }

        Row {
            anchors { right: parent.right; rightMargin: 30; verticalCenter: parent.verticalCenter }
            spacing: 20

            HeaderButton {
                label: "New"
                onClicked: {
                    messages.clear();
                    config.clearHistory();
                }
            }
            HeaderButton {
                label: "Settings"
                onClicked: root.showSettings = true
            }
        }
    }

    // ============================================================== messages
    ListView {
        id: list
        anchors { top: header.bottom; left: parent.left; right: parent.right;
                  bottom: inputArea.top; margins: 20 }
        clip: true
        spacing: 24
        model: messages

        delegate: Item {
            id: msgDelegate
            required property var model

            width: list.width
            height: bubble.height

            Rectangle {
                id: bubble
                width: Math.min(msgText.implicitWidth + 50, list.width * 0.85)
                height: msgText.implicitHeight + 40
                radius: 16
                color: msgDelegate.model.role === "user" ? "#f0f0f0" : "white"
                border.color: msgDelegate.model.role === "error" ? "#909090" : "black"
                border.width: msgDelegate.model.role === "assistant" ? 0 : 3
                anchors.right: msgDelegate.model.role === "user" ? parent.right : undefined

                Text {
                    id: msgText
                    x: 25
                    y: 20
                    width: parent.width - 50
                    wrapMode: Text.Wrap
                    font.pixelSize: 30
                    font.italic: msgDelegate.model.role === "error"
                    color: msgDelegate.model.role === "error" ? "#606060" : "black"
                    text: (msgDelegate.model.role === "error" ? "(!) " : "")
                          + msgDelegate.model.text
                }
            }
        }

        Text {
            anchors.centerIn: parent
            width: parent.width - 200
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            visible: messages.count === 0
            font.pixelSize: 32
            color: "#707070"
            text: config.configured
                  ? "Write a message below with the pen, then tap Send."
                  : "Welcome! Tap Settings to enter your API details."
        }
    }

    // ============================================================ input area
    Item {
        id: inputArea
        anchors { left: parent.left; right: parent.right; bottom: bottomBar.top }
        height: root.kbMode ? 780 : 460

        // ---------------------------------------------------- handwriting pad
        Rectangle {
            anchors.fill: parent
            visible: !root.kbMode
            color: "white"
            border.color: "black"
            border.width: 3

            InkCanvas {
                id: canvas
                anchors.fill: parent
                anchors.margins: 3
            }

            Text {
                anchors.centerIn: parent
                visible: !canvas.hasInk
                font.pixelSize: 34
                color: "#a0a0a0"
                text: "Write here with the pen"
            }

        }

        // ------------------------------------------------------- typed input
        Column {
            anchors.fill: parent
            visible: root.kbMode

            Rectangle {
                width: parent.width
                height: 110
                color: "white"
                border.color: "black"
                border.width: 3

                TextInput {
                    id: typedField
                    anchors { fill: parent; margins: 20 }
                    font.pixelSize: 34
                    verticalAlignment: TextInput.AlignVCenter
                    clip: true
                }
                Text {
                    anchors { left: parent.left; leftMargin: 25; verticalCenter: parent.verticalCenter }
                    visible: typedField.text.length === 0
                    font.pixelSize: 30
                    color: "#a0a0a0"
                    text: "Type a message…"
                }
            }

            KeyBoard {
                width: parent.width
                height: parent.height - 110
                target: typedField
                onDone: root.kbMode = false
            }
        }
    }

    // ============================================================ bottom bar
    Rectangle {
        id: bottomBar
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 130
        color: "white"

        Rectangle { anchors.top: parent.top; width: parent.width; height: 3; color: "black" }

        Row {
            anchors { left: parent.left; leftMargin: 30; verticalCenter: parent.verticalCenter }
            spacing: 24

            BarButton {
                label: "Clear"
                enabled: canvas.hasInk || typedField.text.length > 0
                onClicked: root.clearPad()
            }
            BarButton {
                label: root.kbMode ? "Pen" : "Keys"
                onClicked: root.kbMode = !root.kbMode
            }
        }

        BarButton {
            anchors { right: parent.right; rightMargin: 30; verticalCenter: parent.verticalCenter }
            width: 360
            label: chat.busy ? "Waiting…" : "Send »"
            inverted: true
            enabled: !chat.busy && config.configured &&
                     (canvas.hasInk || (root.kbMode && typedField.text.trim().length > 0))
            onClicked: root.doSend()
        }
    }

    // ============================================================== settings
    Rectangle {
        anchors.fill: parent
        visible: root.showSettings
        color: "white"

        Rectangle {
            id: settingsHeader
            anchors { top: parent.top; left: parent.left; right: parent.right }
            height: 120
            color: "black"
            Text {
                anchors { left: parent.left; leftMargin: 40; verticalCenter: parent.verticalCenter }
                color: "white"; font.pixelSize: 42; font.bold: true
                text: "Settings"
            }
        }

        Column {
            id: settingsForm
            anchors { top: settingsHeader.bottom; topMargin: 40
                      left: parent.left; leftMargin: 50
                      right: parent.right; rightMargin: 50 }
            spacing: 26

            SettingsField { id: fldBase;  label: "API base URL (…/v1)" }

            SettingsField {
                id: fldKey
                label: "API key  -  " + fldKey.value.length + " characters"
                mono: true
                masked: !showKey.on
            }

            SettingsField { id: fldModel; label: "Model (vision-capable for handwriting)" }

            ToggleButton { id: showKey; label: "Show" }

            Text {
                id: testStatus
                width: parent.width
                wrapMode: Text.Wrap
                font.pixelSize: 26
                color: "#404040"
                text: ""
            }

            Row {
                spacing: 24
                BarButton {
                    label: chat.busy ? "…" : "Test"
                    enabled: !chat.busy
                    onClicked: {
                        testStatus.text = "Testing " + fldBase.value + " …";
                        chat.testConnection(fldBase.value, fldKey.value);
                    }
                }
                BarButton {
                    label: "Cancel"
                    visible: config.configured
                    onClicked: root.showSettings = false
                }
                BarButton {
                    label: "Save"
                    inverted: true
                    enabled: fldKey.value.length > 0 && fldModel.value.length > 0
                    onClicked: {
                        config.apiBase = fldBase.value;
                        config.apiKey = fldKey.value;
                        config.model = fldModel.value;
                        config.save();
                        root.showSettings = false;
                    }
                }
            }
        }

        Connections {
            target: chat
            function onTestResult(ok, detail) {
                testStatus.text = (ok ? "✓ " : "✗ ") + detail;
            }
        }

        KeyBoard {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 620
            target: root.activeSettingsField
        }

        Component.onCompleted: {
            fldBase.value = config.apiBase;
            fldKey.value = config.apiKey;
            fldModel.value = config.model;
        }
    }

    property var activeSettingsField: null

    MultiPointTouchArea {
        id: exitGesture
        anchors.fill: parent
        mouseEnabled: false
        minimumTouchPoints: 2
        maximumTouchPoints: 5
        touchPoints: [
            TouchPoint { id: xp1 }, TouchPoint { id: xp2 },
            TouchPoint { id: xp3 }, TouchPoint { id: xp4 },
            TouchPoint { id: xp5 }
        ]

        // Same trigger as the loader: opposite corners held together. A hand
        // resting while you write covers one contiguous area and cannot span
        // both, which a plain "4 fingers" test could not distinguish.
        readonly property int corner: 450
        readonly property bool holding: {
            let a = false, b = false;
            for (const p of [xp1, xp2, xp3, xp4, xp5]) {
                if (!p.pressed)
                    continue;
                if (p.x <= corner && p.y <= corner)
                    a = true;
                if (p.x >= width - corner && p.y >= height - corner)
                    b = true;
            }
            return a && b;
        }
        onHoldingChanged: holding ? exitHold.restart() : exitHold.stop()
    }

    Timer {
        id: exitHold
        interval: 1600
        onTriggered: Qt.quit()
    }

    // Fires after Qt has rendered the cleared frame; refreshing inline would
    // flash the previous (still-inked) content instead.
    // Bring the (now empty) canvas back one frame later: the removal and the
    // re-add are both real damage, so the panel repaints that region.
    Timer {
        id: redrawStep
        interval: 80
        onTriggered: {
            canvas.visible = true;
            refreshAfterClear.restart();
        }
    }

    Timer {
        id: refreshAfterClear
        interval: 250
        onTriggered: if (cfgEpaperBlink) epaper.blink()
    }

    Rectangle {
        anchors.centerIn: parent
        width: 720; height: 130; radius: 24
        color: "black"
        visible: exitHold.running
        Text {
            anchors.centerIn: parent
            color: "white"; font.pixelSize: 40; font.bold: true
            text: "Exiting to tablet…"
        }
    }

    // ============================================================ test hooks
    Timer {
        running: cfgAutoSendMs > 0
        interval: cfgAutoSendMs > 0 ? cfgAutoSendMs : 1
        onTriggered: root.doSend()
    }

    Timer {
        running: cfgAutoClearMs > 0
        interval: cfgAutoClearMs > 0 ? cfgAutoClearMs : 1
        onTriggered: {
            console.info("test: pressing Clear; hasInk before =", canvas.hasInk);
            root.clearPad();
            console.info("test: hasInk after =", canvas.hasInk);
        }
    }

    Timer {
        running: root.screenshotMode
        interval: cfgShotDelayMs > 0 ? cfgShotDelayMs : 1200
        onTriggered: {
            root.contentItem.grabToImage(function (result) {
                result.saveToFile(cfgScreenshot);
                Qt.quit();
            });
        }
    }

    // ======================================================= reusable pieces
    component HeaderButton: Rectangle {
        property string label: ""
        signal clicked()
        width: hbText.implicitWidth + 50
        height: 74
        color: "black"
        border.color: "white"
        border.width: 2
        radius: 10
        Text {
            id: hbText
            anchors.centerIn: parent
            color: "white"; font.pixelSize: 28; font.bold: true
            text: parent.label
        }
        MouseArea { anchors.fill: parent; onClicked: parent.clicked() }
    }

    component BarButton: Rectangle {
        id: bb
        property string label: ""
        property bool inverted: false
        signal clicked()
        width: bbText.implicitWidth + 60
        height: 90
        radius: 12
        color: inverted ? "black" : "white"
        border.color: "black"
        border.width: 3
        opacity: enabled ? 1.0 : 0.35
        Text {
            id: bbText
            anchors.centerIn: parent
            color: bb.inverted ? "white" : "black"
            font.pixelSize: 32; font.bold: true
            text: bb.label
        }
        MouseArea { anchors.fill: parent; enabled: bb.enabled; onClicked: bb.clicked() }
    }

    component SettingsField: Column {
        id: sf
        property string label: ""
        property alias value: sfInput.text
        property bool mono: false
        property bool masked: false
        width: parent.width
        spacing: 8

        Text { font.pixelSize: 26; color: "#404040"; text: sf.label }

        Rectangle {
            width: parent.width
            height: 96
            color: "white"
            border.color: sfInput.activeFocus ? "black" : "#808080"
            border.width: sfInput.activeFocus ? 5 : 2
            radius: 8

            TextInput {
                id: sfInput
                anchors { fill: parent; margins: 18 }
                font.pixelSize: sf.mono ? 26 : 30
                font.family: sf.mono ? "monospace" : sfInput.font.family
                verticalAlignment: TextInput.AlignVCenter
                clip: true
                // Masked view still shows the tail, so you can see where you
                // are while proofreading a long key.
                echoMode: sf.masked ? TextInput.Password : TextInput.Normal
                passwordCharacter: "*"
                passwordMaskDelay: 0
                onActiveFocusChanged: {
                    if (activeFocus)
                        root.activeSettingsField = sfInput;
                }
            }
        }
    }

    component ToggleButton: Rectangle {
        id: tb
        property string label: ""
        property bool on: false
        width: tbText.implicitWidth + 60
        height: 72
        radius: 10
        color: tb.on ? "black" : "white"
        border.color: "black"
        border.width: 3
        Text {
            id: tbText
            anchors.centerIn: parent
            color: tb.on ? "white" : "black"
            font.pixelSize: 28
            font.bold: true
            text: (tb.on ? "Hide " : "Show ") + "key"
        }
        MouseArea { anchors.fill: parent; onClicked: tb.on = !tb.on }
    }

    component KeyBoard: Rectangle {
        id: kb
        property var target: null
        signal done()
        property int mode: 0    // 0 = lower, 1 = upper, 2 = symbols

        color: "#e8e8e8"

        readonly property var layouts: [
            [ ["q","w","e","r","t","y","u","i","o","p"],
              ["a","s","d","f","g","h","j","k","l"],
              ["Shift","z","x","c","v","b","n","m","Del"],
              ["?123","space","done"] ],
            [ ["Q","W","E","R","T","Y","U","I","O","P"],
              ["A","S","D","F","G","H","J","K","L"],
              ["Shift","Z","X","C","V","B","N","M","Del"],
              ["?123","space","done"] ],
            [ ["1","2","3","4","5","6","7","8","9","0"],
              ["-","_",":","/",".","~","+","=","@"],
              ["#","$","%","&","*","(",")","!","Del"],
              ["abc","space","done"] ]
        ]

        function press(key) {
            if (key === "Shift") { mode = mode === 1 ? 0 : 1; return; }
            if (key === "?123")  { mode = 2; return; }
            if (key === "abc")   { mode = 0; return; }
            if (key === "done")  { kb.done(); return; }
            if (!kb.target)
                return;
            if (key === "Del") {
                const p = kb.target.cursorPosition;
                if (p > 0)
                    kb.target.remove(p - 1, p);
                return;
            }
            const ch = key === "space" ? " " : key;
            kb.target.insert(kb.target.cursorPosition, ch);
            if (mode === 1)
                mode = 0;   // shift is one-shot
        }

        Column {
            anchors { fill: parent; margins: 14 }
            spacing: 14

            Repeater {
                model: kb.layouts[kb.mode]

                delegate: Row {
                    required property var modelData
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 12
                    height: (kb.height - 28 - 3 * 14) / 4

                    Repeater {
                        model: modelData

                        delegate: Rectangle {
                            id: keyRect
                            required property string modelData
                            readonly property bool wide: modelData === "space"
                            readonly property bool fn:
                                ["Shift", "Del", "?123", "abc", "done"]
                                    .indexOf(modelData) >= 0
                            width: wide ? 700 : (fn ? 200 : 122)
                            height: parent.height
                            radius: 10
                            color: fn ? "#c8c8c8" : "white"
                            border.color: "#606060"
                            border.width: 2

                            Text {
                                anchors.centerIn: parent
                                font.pixelSize: keyRect.fn ? 28 : 34
                                font.bold: keyRect.fn
                                text: keyRect.modelData === "space" ? "" : keyRect.modelData
                            }

                            MouseArea {
                                anchors.fill: parent
                                onPressed: {
                                    kb.press(keyRect.modelData);
                                    if (keyRect.modelData === "Del")
                                        delRepeat.start();
                                }
                                onReleased: delRepeat.stop()
                                onCanceled: delRepeat.stop()
                            }

                            // Hold Del to keep deleting - important when a
                            // 50-character API key needs fixing.
                            Timer {
                                id: delRepeat
                                interval: 500
                                repeat: true
                                onTriggered: {
                                    interval = 120;
                                    kb.press("Del");
                                }
                                onRunningChanged: if (!running) interval = 500
                            }
                        }
                    }
                }
            }
        }
    }
}
