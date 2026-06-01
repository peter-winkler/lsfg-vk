// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

ComboBox {
    id: control

    SystemPalette { id: sys; colorGroup: SystemPalette.Active }
    readonly property color base: Qt.rgba(sys.window.r, sys.window.g, sys.window.b, 1)
    readonly property color accent: Qt.rgba(sys.highlight.r, sys.highlight.g, sys.highlight.b, 1)
    readonly property color txt: Qt.rgba(sys.windowText.r, sys.windowText.g, sys.windowText.b, 1)
    readonly property color line: Qt.rgba(sys.windowText.r, sys.windowText.g, sys.windowText.b, 0.14)

    implicitHeight: 34

    background: Rectangle {
        radius: 8
        color: control.down ? Qt.lighter(control.base, 1.34) : Qt.lighter(control.base, 1.20)
        border.color: control.popup.visible ? control.accent : control.line
        border.width: 1
    }

    contentItem: Text {
        leftPadding: 12
        rightPadding: 28
        text: control.displayText
        font: control.font
        color: control.txt
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Text {
        x: control.width - width - 11
        y: control.height / 2 - height / 2
        text: "▾"
        color: Qt.rgba(sys.windowText.r, sys.windowText.g, sys.windowText.b, 0.6)
        font.pixelSize: 12
    }

    delegate: ItemDelegate {
        width: ListView.view.width
        height: 32
        highlighted: control.highlightedIndex === index
        contentItem: Text {
            text: modelData
            color: control.txt
            leftPadding: 8
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 6
            color: highlighted ? Qt.rgba(control.accent.r, control.accent.g, control.accent.b, 0.28) : "transparent"
        }
    }

    popup: Popup {
        y: control.height + 4
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 260)
        padding: 4

        background: Rectangle {
            radius: 8
            color: Qt.lighter(control.base, 1.22)
            border.color: Qt.rgba(sys.windowText.r, sys.windowText.g, sys.windowText.b, 0.16)
            border.width: 1
        }
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            spacing: 2
            ScrollIndicator.vertical: ScrollIndicator {}
        }
    }
}
