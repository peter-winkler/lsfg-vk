// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

Item {
    id: control
    property var model: []
    property int currentIndex: 0
    signal activated(int index)

    SystemPalette { id: sys; colorGroup: SystemPalette.Active }
    readonly property int count: Math.max(1, control.model.length)

    implicitHeight: 36
    implicitWidth: 260

    Rectangle {
        anchors.fill: parent
        radius: 9
        color: Qt.darker(sys.window, 1.12)
        border.color: Qt.rgba(sys.windowText.r, sys.windowText.g, sys.windowText.b, 0.11)
        border.width: 1

        // sliding selection pill
        Rectangle {
            property real segW: control.width / control.count
            width: segW - 8
            height: parent.height - 8
            x: control.currentIndex * segW + 4
            y: 4
            radius: 6
            color: sys.highlight
            Behavior on x { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
        }

        Row {
            anchors.fill: parent
            Repeater {
                model: control.model
                delegate: Item {
                    width: control.width / control.count
                    height: control.height
                    Text {
                        anchors.centerIn: parent
                        text: modelData
                        font.pixelSize: 14
                        font.bold: index === control.currentIndex
                        color: index === control.currentIndex
                             ? sys.highlightedText
                             : Qt.rgba(sys.windowText.r, sys.windowText.g, sys.windowText.b, 0.82)
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: control.activated(index)
                    }
                }
            }
        }
    }
}
