import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    visible: true
    width: 1400
    height: 860
    title: "CloudScope — Live Cloud Analysis"
    color: "#14171c"

    property int frameTick: 0

    Connections {
        target: controller
        function onFrameReady() { root.frameTick += 1 }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        // ---- Live view ----
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
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
            Label {
                anchors.centerIn: parent
                visible: live.source == ""
                text: "Idle — pick a source and press Start"
                color: "#8b93a1"
                font.pixelSize: 18
            }
        }

        // ---- Control panel ----
        ColumnLayout {
            Layout.preferredWidth: 320
            Layout.fillHeight: true
            spacing: 8

            GroupBox {
                title: "Source"
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
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
                            onClicked: {
                                            var t = srcField.text
                                            if (t.indexOf("://") >= 0 || t.indexOf("/") >= 0)
                                                controller.startSource(t)
                                            else
                                                controller.startSource(String(camList.currentIndex))
                                        }
                        }
                        Button {
                            text: "Stop"
                            onClicked: controller.stop()
                        }
                    }
                    Label { text: "Status: " + controller.status; color: "#8b93a1" }
                }
            }

            GroupBox {
                title: "Overlay"
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    ButtonGroup { id: modeGroup }
                    RadioButton { text: "Rectangle"; checked: true; ButtonGroup.group: modeGroup; onClicked: controller.setOverlayMode(0) }
                    RadioButton { text: "Polygon"; ButtonGroup.group: modeGroup; onClicked: controller.setOverlayMode(1) }
                    RadioButton { text: "Symmetry (outline + highlight)"; ButtonGroup.group: modeGroup; onClicked: controller.setOverlayMode(2) }
                    RowLayout {
                        Label { text: "Min conf" }
                        Slider { id: confSlider; from: 0; to: 1; stepSize: 0.05; value: 0.3; Layout.fillWidth: true; onMoved: controller.setMinConf(value) }
                        Label { text: Number(confSlider.value).toFixed(2) }
                    }
                }
            }

            GroupBox {
                title: "Now"
                Layout.fillWidth: true
                GridLayout {
                    columns: 2
                    anchors.fill: parent
                    Label { text: "Type:"; color: "#8b93a1" }
                    Label { text: controller.label; font.bold: true; font.pixelSize: 20; color: "#e8ecf1" }
                    Label { text: "Confidence:"; color: "#8b93a1" }
                    Label { text: Number(controller.confidence).toFixed(3) }
                    Label { text: "Clouds:"; color: "#8b93a1" }
                    Label { text: controller.objectCount + (controller.maskOk ? "" : " (mask uncertain)") }
                    Label { text: "FPS / device:"; color: "#8b93a1" }
                    Label { text: Number(controller.fps).toFixed(1) + " · " + controller.device }
                }
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

            Label {
                text: "CloudScope " + controller.version + " · backend " + inferBackend
                color: "#5b6472"
                font.pixelSize: 11
            }
        }
    }
}
