// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

Switch {
    id: control

    SystemPalette { id: sys; colorGroup: SystemPalette.Active }
    readonly property color accent: Qt.rgba(sys.highlight.r, sys.highlight.g, sys.highlight.b, 1)
    readonly property color base: Qt.rgba(sys.window.r, sys.window.g, sys.window.b, 1)
    readonly property color thumb: Qt.rgba(sys.windowText.r, sys.windowText.g, sys.windowText.b, 1)

    indicator: Rectangle {
        implicitWidth: 46
        implicitHeight: 26
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        radius: height / 2
        color: control.checked ? control.accent : Qt.darker(control.base, 1.25)
        border.color: Qt.rgba(sys.windowText.r, sys.windowText.g, sys.windowText.b, 0.12)
        border.width: 1
        Behavior on color { ColorAnimation { duration: 130 } }

        Rectangle {
            width: 18; height: 18; radius: 9
            y: 4
            x: control.checked ? parent.width - width - 4 : 4
            color: control.thumb
            Behavior on x { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
        }
    }

    contentItem: Item {}
}
