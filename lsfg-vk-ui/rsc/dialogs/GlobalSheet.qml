// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../widgets"

Dialog {
    id: control
    anchors.centerIn: Overlay.overlay
    modal: true
    width: 540
    title: "Global Settings"
    standardButtons: Dialog.Close

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
