// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

Button {
    id: control
    property bool accented: false

    SystemPalette { id: sys; colorGroup: SystemPalette.Active }
    readonly property color accent: Qt.rgba(sys.highlight.r, sys.highlight.g, sys.highlight.b, 1)
    readonly property color base: Qt.rgba(sys.window.r, sys.window.g, sys.window.b, 1)

    leftPadding: 16; rightPadding: 16; topPadding: 8; bottomPadding: 8

    background: Rectangle {
        radius: 8
        color: control.accented
            ? (control.down ? Qt.darker(control.accent, 1.12)
               : control.hovered ? Qt.lighter(control.accent, 1.08) : control.accent)
            : (control.down ? Qt.lighter(control.base, 1.38)
               : control.hovered ? Qt.lighter(control.base, 1.30) : Qt.lighter(control.base, 1.20))
        border.color: control.accented
            ? "transparent"
            : Qt.rgba(sys.windowText.r, sys.windowText.g, sys.windowText.b, 0.14)
        border.width: 1
        Behavior on color { ColorAnimation { duration: 90 } }
    }

    contentItem: Text {
        text: control.text
        font.pixelSize: 13
        font.bold: control.accented
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: control.accented
            ? sys.highlightedText
            : Qt.rgba(sys.windowText.r, sys.windowText.g, sys.windowText.b, 0.92)
    }
}
