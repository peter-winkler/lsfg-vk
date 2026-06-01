// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../widgets"

Dialog {
    id: control
    anchors.centerIn: Overlay.overlay
    modal: true
    dim: true
    width: 540
    padding: 22
    title: "Global Settings"
    standardButtons: Dialog.Close

    SystemPalette { id: sys; colorGroup: SystemPalette.Active }
    background: Rectangle {
        radius: 14
        color: Qt.lighter(Qt.rgba(sys.window.r, sys.window.g, sys.window.b, 1), 1.16)
        border.color: Qt.rgba(sys.windowText.r, sys.windowText.g, sys.windowText.b, 0.12)
        border.width: 1
    }
    header: Label {
        text: control.title
        color: sys.windowText
        font.pixelSize: 16; font.bold: true
        leftPadding: 22; rightPadding: 22; topPadding: 20; bottomPadding: 4
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 18

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            Label { text: "Path to Lossless Scaling"; font.bold: true }
            Label { text: "Location of Lossless.dll"; opacity: 0.6; font.pixelSize: 12 }
            FileEdit {
                Layout.fillWidth: true
                title: "Select Lossless.dll"
                filter: "Dynamic Link Library Files (*.dll)"
                text: backend.dll
                onUpdate: (text) => backend.dll = text
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 16
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label { text: "Allow half-precision"; font.bold: true }
                Label { text: "Acceleration through fp16 (a large speed-up on RDNA)"; opacity: 0.6; font.pixelSize: 12 }
            }
            Switch {
                checked: backend.allow_fp16
                onToggled: backend.allow_fp16 = checked
            }
        }
    }
}
