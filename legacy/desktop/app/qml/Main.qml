import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// CloudScope 1.1.0 — SharpCap-style workbench:
// menubar + toolbar, live view w/ reticle, tabbed control panel,
// histogram, status bar.
ApplicationWindow {
    id: root
    visible: true
    width: 1400
    height: 900
    minimumWidth: 1050
    minimumHeight: 640
    title: "CloudScope — Live Cloud Analysis"
    color: "#14171c"

    property int frameTick: 0
    property bool reticleOn: true

    function startFromUI() {
        var t = srcField.text
        if (t.indexOf("://") >= 0 || t.indexOf("/") >= 0)
            controller.startSource(t)
        else
            controller.startSource(String(camList.currentIndex))
    }

    Connections {
        target: controller
        function onFrameReady() { root.frameTick += 1 }
    }

    menuBar: MenuBar {
        Menu {
            title: "File"
            Action {
                text: "Snapshot…"
                onTriggered: controller.snapshot("cloudscope_" + Date.now() + ".jpg")
            }
            MenuSeparator {}
            Action {
                text: "Exit"
                onTriggered: Qt.quit()
            }
        }
        Menu {
            title: "Camera"
            Action {
                text: "Start"
                onTriggered: root.startFromUI()
            }
            Action {
                text: "Stop"
                onTriggered: controller.stop()
            }
            Action {
                text: "Rescan cameras"
                onTriggered: camList.model = controller.probeCameras(5)
            }
        }
        Menu {
            title: "View"
            Action { text: "Rectangle"; onTriggered: controller.setOverlayMode(0) }
            Action { text: "Polygon"; onTriggered: controller.setOverlayMode(1) }
            Action { text: "Symmetry"; onTriggered: controller.setOverlayMode(2) }
            MenuSeparator {}
            Action {
                text: "Reticle"
                checkable: true
                checked: root.reticleOn
                onTriggered: root.reticleOn = checked
            }
        }
        Menu {
            title: "Help"
            Action {
                text: "About"
                onTriggered: aboutBox.open()
            }
        }
    }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            Button {
                text: "● Start"
                highlighted: true
                onClicked: root.startFromUI()
            }
            Button {
                text: "■ Stop"
                onClicked: controller.stop()
            }
            Button {
                text: "Snapshot"
                onClicked: controller.snapshot("cloudscope_" + Date.now() + ".jpg")
            }
            ComboBox {
                id: modeCombo
                model: ["Rectangle", "Polygon", "Symmetry"]
                onActivated: controller.setOverlayMode(currentIndex)
            }
            Item { Layout.fillWidth: true }
            Label {
                text: controller.label + "  " + Number(controller.confidence).toFixed(3)
                font.bold: true
                font.pixelSize: 16
                color: "#e8ecf1"
            }
            Label {
                text: Number(controller.fps).toFixed(1) + " fps"
                color: "#8b93a1"
            }
        }
    }

    footer: Rectangle {
        height: 26
        color: "#1a1e25"
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            Label { text: controller.status; Layout.fillWidth: true; elide: Text.ElideRight; color: "#8b93a1"; font.pixelSize: 12 }
            Label { text: controller.frameW + "x" + controller.frameH; color: "#8b93a1"; font.pixelSize: 12 }
            Label { text: "#" + controller.frameNo; color: "#8b93a1"; font.pixelSize: 12 }
            Label { text: controller.device; color: "#8b93a1"; font.pixelSize: 12 }
        }
    }

    // ---- Live view (left, anchored) ----
    Rectangle {
        id: viewBox
        anchors {
            left: parent.left
            top: parent.top
            bottom: parent.bottom
            leftMargin: 10
            topMargin: 10
            bottomMargin: 10
            rightMargin: 10
        }
        width: parent.width - panel.width - 30
        color: "#0b0d10"
        radius: 8
        border.color: "#2a2f38"
        Image {
            id: live
            anchors.fill: parent
            anchors.margins: 4
            fillMode: Image.PreserveAspectFit
            cache: false
            source: controller.status.indexOf("running") === 0
                    ? "image://frames/live?tick=" + root.frameTick
                    : ""
        }
        // Reticle: crosshair + circle + thirds (pure overlay, no backend).
        Item {
            id: reticle
            anchors.fill: live
            visible: root.reticleOn && live.source != ""
            Canvas {
                anchors.fill: parent
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    ctx.strokeStyle = "rgba(0,255,170,0.75)"
                    ctx.lineWidth = 1
                    var cx = width / 2, cy = height / 2
                    ctx.beginPath()
                    ctx.moveTo(cx - 24, cy)
                    ctx.lineTo(cx + 24, cy)
                    ctx.moveTo(cx, cy - 24)
                    ctx.lineTo(cx, cy + 24)
                    ctx.stroke()
                    ctx.beginPath()
                    ctx.arc(cx, cy, 46, 0, Math.PI * 2)
                    ctx.stroke()
                    ctx.strokeStyle = "rgba(0,255,170,0.30)"
                    ctx.beginPath()
                    ctx.moveTo(width / 3, 0)
                    ctx.lineTo(width / 3, height)
                    ctx.moveTo(2 * width / 3, 0)
                    ctx.lineTo(2 * width / 3, height)
                    ctx.moveTo(0, height / 3)
                    ctx.lineTo(width, height / 3)
                    ctx.moveTo(0, 2 * height / 3)
                    ctx.lineTo(width, 2 * height / 3)
                    ctx.stroke()
                }
            }
        }
        Label {
            anchors.centerIn: parent
            visible: live.source == ""
            text: "Idle — pick a source and press Start"
            color: "#8b93a1"
            font.pixelSize: 18
        }
    }

    // ---- Tabbed control panel (right) ----
    Rectangle {
        id: panel
        anchors {
            right: parent.right
            top: parent.top
            bottom: parent.bottom
            rightMargin: 10
            topMargin: 10
            bottomMargin: 10
        }
        width: 340
        color: "#1a1e25"
        radius: 8
        border.color: "#2a2f38"
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 6
            TabBar {
                id: tabs
                Layout.fillWidth: true
                TabButton { text: "Source" }
                TabButton { text: "Camera" }
                TabButton { text: "Display" }
                TabButton { text: "Info" }
            }
            StackLayout {
                currentIndex: tabs.currentIndex
                Layout.fillWidth: true
                Layout.fillHeight: true

                // Source tab
                ColumnLayout {
                    RowLayout {
                        TextField {
                            id: srcField
                            Layout.fillWidth: true
                            placeholderText: "0 · rtsp://… · file.mp4"
                            text: "0"
                        }
                        Button {
                            text: "Probe"
                            onClicked: camList.model = controller.probeCameras(5)
                        }
                    }
                    ComboBox {
                        id: camList
                        Layout.fillWidth: true
                        model: ["0"]
                    }
                    RowLayout {
                        Button {
                            text: "Start"
                            highlighted: true
                            Layout.fillWidth: true
                            onClicked: root.startFromUI()
                        }
                        Button {
                            text: "Stop"
                            Layout.fillWidth: true
                            onClicked: controller.stop()
                        }
                    }
                    Label { text: "Status: " + controller.status; color: "#8b93a1"; wrapMode: Text.Wrap }
                }

                // Camera tab (ranges are camera-dependent; applied live)
                ColumnLayout {
                    RowLayout {
                        Label { text: "Exposure"; Layout.preferredWidth: 110 }
                        Slider { id: expSlider; from: -13; to: 0; stepSize: 1; value: -6; Layout.fillWidth: true
                            onMoved: controller.setExposure(value) }
                        Label { text: Number(expSlider.value).toFixed(0); Layout.preferredWidth: 36 }
                    }
                    RowLayout {
                        Label { text: "Gain"; Layout.preferredWidth: 110 }
                        Slider { id: gainSlider; from: 0; to: 100; stepSize: 1; value: 0; Layout.fillWidth: true
                            onMoved: controller.setGain(value) }
                        Label { text: Number(gainSlider.value).toFixed(0); Layout.preferredWidth: 36 }
                    }
                    RowLayout {
                        CheckBox { id: autoExp; text: "Auto exposure"; checked: true
                            onClicked: controller.setAutoExposure(checked) }
                    }
                    RowLayout {
                        Label { text: "Resolution"; Layout.preferredWidth: 110 }
                        ComboBox {
                            id: resCombo
                            Layout.fillWidth: true
                            model: ["Default", "640x480", "1280x720", "1920x1080"]
                            onActivated: {
                                var wh = currentText.split("x")
                                if (wh.length === 2)
                                    controller.setResolution(parseInt(wh[0]), parseInt(wh[1]))
                            }
                        }
                    }
                    Label {
                        text: "Ranges vary by camera; unsupported knobs are ignored by the driver."
                        color: "#5b6472"
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }

                // Display tab
                ColumnLayout {
                    ButtonGroup { id: modeGroup }
                    RadioButton { text: "Rectangle"; checked: true; ButtonGroup.group: modeGroup; onClicked: controller.setOverlayMode(0) }
                    RadioButton { text: "Polygon"; ButtonGroup.group: modeGroup; onClicked: controller.setOverlayMode(1) }
                    RadioButton { text: "Symmetry (outline + highlight)"; ButtonGroup.group: modeGroup; onClicked: controller.setOverlayMode(2) }
                    CheckBox { text: "Reticle"; checked: root.reticleOn; onClicked: root.reticleOn = checked }
                    RowLayout {
                        Label { text: "Min conf" }
                        Slider { id: confSlider; from: 0; to: 1; stepSize: 0.05; value: 0.3; Layout.fillWidth: true; onMoved: controller.setMinConf(value) }
                        Label { text: Number(confSlider.value).toFixed(2) }
                    }
                    Label { text: "Histogram (luma)"; color: "#8b93a1" }
                    Canvas {
                        id: hist
                        Layout.fillWidth: true
                        Layout.preferredHeight: 90
                        Connections {
                            target: controller
                            function onHistogramReady() { hist.requestPaint() }
                        }
                        onPaint: {
                            var ctx = getContext("2d")
                            var h = controller.histogram
                            ctx.clearRect(0, 0, width, height)
                            ctx.fillStyle = "#232936"
                            ctx.fillRect(0, 0, width, height)
                            if (!h || h.length === 0)
                                return
                            ctx.fillStyle = "#00e5aa"
                            var bw = width / h.length
                            for (var i = 0; i < h.length; ++i) {
                                var bh = Math.min(1, h[i] * 8) * (height - 4)
                                ctx.fillRect(i * bw, height - bh, Math.max(1, bw - 1), bh)
                            }
                        }
                    }
                }

                // Info tab
                ColumnLayout {
                    GridLayout {
                        columns: 2
                        Label { text: "Type:"; color: "#8b93a1" }
                        Label { text: controller.label; font.bold: true; font.pixelSize: 20; color: "#e8ecf1" }
                        Label { text: "Confidence:"; color: "#8b93a1" }
                        Label { text: Number(controller.confidence).toFixed(3) }
                        Label { text: "Clouds:"; color: "#8b93a1" }
                        Label { text: controller.objectCount + (controller.maskOk ? "" : " (mask uncertain)") }
                        Label { text: "Frame:"; color: "#8b93a1" }
                        Label { text: controller.frameW + "x" + controller.frameH + " · #" + controller.frameNo }
                        Label { text: "FPS / device:"; color: "#8b93a1" }
                        Label { text: Number(controller.fps).toFixed(1) + " · " + controller.device }
                        Label { text: "Version:"; color: "#8b93a1" }
                        Label { text: controller.version }
                    }
                    GroupBox {
                        title: "Log"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        ListView {
                            anchors.fill: parent
                            clip: true
                            model: logModel
                            delegate: Text { text: modelData; color: "#aeb6c2"; font.pixelSize: 12 }
                        }
                        ListModel { id: logModel }
                        Connections {
                            target: controller
                            function onLogAdded(line) {
                                logModel.append({line: line})
                                if (logModel.count > 100) logModel.remove(0)
                            }
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: aboutBox
        title: "About CloudScope"
        standardButtons: Dialog.Ok
        anchors.centerIn: parent
        Label {
            text: "CloudScope " + controller.version + "\nLive ground-based cloud analysis.\nClassifier 58.2% / segmenter mIoU 0.71.\nBackend: " + inferBackend
        }
    }
}
