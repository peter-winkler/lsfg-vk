// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../widgets"

Dialog {
    property string name: "Active In"
    default property alias content: inner.children
    signal confirm()

    id: root
    title: name
    onAccepted: root.confirm()

    modal: true
    dim: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(560, Overlay.overlay ? Overlay.overlay.width * 0.85 : 560)
    height: Math.min(520, Overlay.overlay ? Overlay.overlay.height * 0.85 : 520)
    padding: 22

    SystemPalette { id: sys; colorGroup: SystemPalette.Active }

    background: Rectangle {
        radius: 14
        color: Qt.lighter(Qt.rgba(sys.window.r, sys.window.g, sys.window.b, 1), 1.16)
        border.color: Qt.rgba(sys.windowText.r, sys.windowText.g, sys.windowText.b, 0.12)
        border.width: 1
    }
    header: Label {
        text: root.title
        color: sys.windowText
        font.pixelSize: 16
        font.bold: true
        leftPadding: 22; rightPadding: 22; topPadding: 20; bottomPadding: 4
    }

    footer: Item {
        implicitHeight: 60
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 22; anchors.rightMargin: 22; anchors.bottomMargin: 18
            Item { Layout.fillWidth: true }
            PillButton { text: "Close"; onClicked: root.close() }
        }
    }

    contentItem: ColumnLayout {
        id: inner
        spacing: 10
    }
}
