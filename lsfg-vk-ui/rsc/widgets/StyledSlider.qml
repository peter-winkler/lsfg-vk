// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

Slider {
    id: control

    SystemPalette { id: sys; colorGroup: SystemPalette.Active }
    readonly property color accent: Qt.rgba(sys.highlight.r, sys.highlight.g, sys.highlight.b, 1)
    readonly property color ring: Qt.rgba(sys.window.r, sys.window.g, sys.window.b, 1)
    readonly property color groove: Qt.rgba(sys.windowText.r, sys.windowText.g, sys.windowText.b, 0.16)

    implicitWidth: 220
    implicitHeight: 22

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: 5
        radius: 3
        color: control.groove
        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: 3
            color: control.accent
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: 18
        height: 18
        radius: 9
        color: control.pressed ? control.accent : Qt.lighter(control.accent, 1.22)
        border.color: control.ring
        border.width: 3
    }
}
