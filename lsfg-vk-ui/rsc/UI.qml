// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import "dialogs"
import "widgets"

ApplicationWindow {
    id: root
    width: 920
    height: 640
    minimumWidth: 800
    minimumHeight: 520
    visible: true
    title: "lsfg-vk"

    // ── Theme: derived live from the GTK palette via SystemPalette ─────────────
    SystemPalette { id: sys; colorGroup: SystemPalette.Active }
    // force opaque surfaces: some GTK palettes carry an alpha on window/base, which
    // would otherwise make the whole window translucent.
    readonly property color cBg:       Qt.rgba(sys.window.r, sys.window.g, sys.window.b, 1)
    readonly property color cPanel:    Qt.darker(cBg, 1.10)
    readonly property color cCard:     Qt.lighter(cBg, 1.22)
    readonly property color cText:     Qt.rgba(sys.windowText.r, sys.windowText.g, sys.windowText.b, 1)
    readonly property color cSubtext:  Qt.rgba(sys.windowText.r, sys.windowText.g, sys.windowText.b, 0.55)
    readonly property color cBorder:   Qt.rgba(sys.windowText.r, sys.windowText.g, sys.windowText.b, 0.11)
    readonly property color cAccent:   Qt.rgba(sys.highlight.r, sys.highlight.g, sys.highlight.b, 1)
    readonly property color cOnAccent: sys.highlightedText
    readonly property int   rad: 12
    color: cBg

    readonly property bool hasProfile: backend.available
    readonly property bool adaptive: backend.target_fps > 0

    // ── Profile / activation dialogs (reused) ─────────────────────────────────
    CenteredDialog {
        id: create_dialog
        name: "Create New Profile"
        onConfirm: backend.createProfile(create_name.text)
        TextField {
            Layout.fillWidth: true
            id: create_name
            placeholderText: "Choose a profile name"
            focus: true
        }
    }
    CenteredDialog {
        id: rename_dialog
        name: "Rename Profile"
        onConfirm: backend.renameProfile(rename_name.text)
        TextField {
            Layout.fillWidth: true
            id: rename_name
            placeholderText: "Choose a profile name"
            focus: true
        }
    }
    CenteredDialog {
        id: delete_dialog
        name: "Confirm Deletion"
        onConfirm: backend.deleteProfile()
        Label {
            Layout.fillWidth: true
            text: "Delete the selected profile? This cannot be undone."
            color: cText
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }
    }
    LargeDialog {
        id: active_in_dialog
        onConfirm: {}
        List {
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: backend.active_in
            selected: backend.active_in_index
            onSelect: (index) => {
                backend.active_in_index = index
                var idx = backend.active_in.index(index, 0);
                active_in_name.text = backend.active_in.data(idx);
            }
        }
        RowLayout {
            spacing: 8
            TextField {
                Layout.fillWidth: true
                id: active_in_name
                placeholderText: "linux binary / exe / process name"
                focus: true
            }
            Button { icon.name: "list-add"; onClicked: backend.addActiveIn(active_in_name.text) }
            Button { icon.name: "list-remove"; onClicked: backend.removeActiveIn() }
        }
    }

    GlobalSheet { id: global_sheet }

    // ── Small reusable section card ───────────────────────────────────────────
    component Card: Rectangle {
        default property alias content: inner.data
        property string heading: ""
        Layout.fillWidth: true
        implicitHeight: inner.implicitHeight + 32
        radius: rad
        color: cCard
        border.color: cBorder
        border.width: 1
        ColumnLayout {
            id: inner
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12
            Label {
                visible: heading.length > 0
                text: heading
                color: cAccent
                font.pixelSize: 12
                font.bold: true
                font.capitalization: Font.AllUppercase
            }
        }
    }

    // A labelled settings row: title + description on the left, control slot on the right.
    component Row2: RowLayout {
        property string title: ""
        property string subtitle: ""
        default property alias control: slot.data
        Layout.fillWidth: true
        spacing: 16
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Label { text: title; color: cText; font.pixelSize: 14; font.bold: true }
            Label {
                visible: subtitle.length > 0
                text: subtitle; color: cSubtext; font.pixelSize: 12
                Layout.fillWidth: true; wrapMode: Text.WordWrap
            }
        }
        Item { id: slot; implicitWidth: childrenRect.width; implicitHeight: childrenRect.height
               Layout.alignment: Qt.AlignVCenter }
    }

    // ── Main layout ───────────────────────────────────────────────────────────
    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Sidebar -------------------------------------------------------------
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 252
            color: cPanel

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12

                RowLayout {
                    spacing: 10
                    Image {
                        source: "qrc:/rsc/gay.pancake.lsfg-vk-ui.png"
                        sourceSize.width: 26; sourceSize.height: 26
                    }
                    Label { text: "lsfg-vk"; color: cText; font.pixelSize: 18; font.bold: true }
                    Item { Layout.fillWidth: true }
                }

                Label {
                    text: "PROFILES"; color: cSubtext; font.pixelSize: 11; font.bold: true
                    Layout.topMargin: 6
                }

                ListView {
                    id: profileList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 4
                    model: backend.profiles
                    currentIndex: backend.profile_index
                    delegate: ItemDelegate {
                        width: ListView.view.width
                        height: 38
                        onClicked: backend.profile_index = index
                        background: Rectangle {
                            radius: 8
                            color: index === backend.profile_index ? cAccent
                                 : (hovered ? cCard : "transparent")
                        }
                        contentItem: Label {
                            text: model.display
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: 12
                            elide: Text.ElideRight
                            color: index === backend.profile_index ? cOnAccent : cText
                            font.pixelSize: 14
                            font.bold: index === backend.profile_index
                        }
                    }
                }

                Button {
                    Layout.fillWidth: true
                    text: "  New Profile"
                    icon.name: "list-add"
                    onClicked: { create_name.text = ""; create_dialog.open() }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: cBorder }

                ItemDelegate {
                    Layout.fillWidth: true
                    height: 38
                    onClicked: global_sheet.open()
                    background: Rectangle { radius: 8; color: parent.hovered ? cCard : "transparent" }
                    contentItem: RowLayout {
                        spacing: 10
                        Label { text: "⚙"; color: cSubtext; font.pixelSize: 16; leftPadding: 6 }
                        Label { text: "Global Settings"; color: cText; font.pixelSize: 14 }
                        Item { Layout.fillWidth: true }
                    }
                }
            }
        }

        // Detail --------------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: cBg

            // Empty state
            ColumnLayout {
                anchors.centerIn: parent
                visible: !hasProfile
                spacing: 8
                Label {
                    text: "No profile selected"
                    color: cSubtext; font.pixelSize: 16
                    Layout.alignment: Qt.AlignHCenter
                }
                Label {
                    text: "Create or pick a profile to configure frame generation."
                    color: cSubtext; font.pixelSize: 13
                    Layout.alignment: Qt.AlignHCenter
                }
            }

            ScrollView {
                anchors.fill: parent
                visible: hasProfile
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                ColumnLayout {
                    width: root.width - 252 - 48
                    x: 24
                    y: 24
                    spacing: 16

                    // Header: profile name + actions
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Label {
                                text: hasProfile && backend.profile_index >= 0
                                      ? backend.profiles.data(backend.profiles.index(backend.profile_index, 0))
                                      : ""
                                color: cText; font.pixelSize: 22; font.bold: true
                                elide: Text.ElideRight; Layout.fillWidth: true
                            }
                            Label { text: "Profile settings"; color: cSubtext; font.pixelSize: 12 }
                        }
                        Button {
                            text: "Rename"
                            onClicked: {
                                rename_name.text = backend.profiles.data(backend.profiles.index(backend.profile_index, 0))
                                rename_dialog.open()
                            }
                        }
                        Button { text: "Delete"; onClicked: delete_dialog.open() }
                    }

                    // Frame Generation -----------------------------------------
                    Card {
                        heading: "Frame Generation"

                        // Fixed / Adaptive mode
                        Segmented {
                            Layout.fillWidth: true
                            model: ["Fixed", "Adaptive"]
                            currentIndex: root.adaptive ? 1 : 0
                            onActivated: (index) => backend.target_fps =
                                (index === 1 ? (backend.target_fps > 0 ? backend.target_fps : 120) : 0)
                        }

                        // Fixed multiplier
                        Row2 {
                            visible: !root.adaptive
                            title: "Multiplier"
                            subtitle: "Generated frames per real frame. Fractional values allowed."
                            RowLayout {
                                spacing: 12
                                Slider {
                                    id: multSlider
                                    implicitWidth: 220
                                    from: 1.0; to: 4.0; stepSize: 0.05
                                    value: backend.multiplier
                                    onMoved: backend.multiplier = value
                                }
                                Label {
                                    text: multSlider.value.toFixed(2) + "×"
                                    color: cAccent; font.pixelSize: 15; font.bold: true
                                    horizontalAlignment: Text.AlignRight
                                    Layout.preferredWidth: 52
                                }
                            }
                        }

                        // Adaptive target + cap
                        Row2 {
                            visible: root.adaptive
                            title: "Target FPS"
                            subtitle: "Generation adapts each frame to hold this output rate."
                            RowLayout {
                                spacing: 12
                                Slider {
                                    id: targetSlider
                                    implicitWidth: 220
                                    from: 30; to: 360; stepSize: 1
                                    value: backend.target_fps
                                    onMoved: backend.target_fps = value
                                }
                                Label {
                                    text: Math.round(targetSlider.value) + " fps"
                                    color: cAccent; font.pixelSize: 15; font.bold: true
                                    horizontalAlignment: Text.AlignRight
                                    Layout.preferredWidth: 60
                                }
                            }
                        }
                        Row2 {
                            visible: root.adaptive
                            title: "Max Multiplier"
                            subtitle: "Upper bound on generation when the base rate is low."
                            RowLayout {
                                spacing: 12
                                Slider {
                                    id: capSlider
                                    implicitWidth: 220
                                    from: 1.0; to: 4.0; stepSize: 0.5
                                    value: backend.multiplier
                                    onMoved: backend.multiplier = value
                                }
                                Label {
                                    text: capSlider.value.toFixed(1) + "×"
                                    color: cAccent; font.pixelSize: 15; font.bold: true
                                    horizontalAlignment: Text.AlignRight
                                    Layout.preferredWidth: 52
                                }
                            }
                        }
                    }

                    // Quality --------------------------------------------------
                    Card {
                        heading: "Quality"
                        Row2 {
                            title: "Flow Scale"
                            subtitle: "Lower motion-estimation resolution for more performance."
                            RowLayout {
                                spacing: 12
                                Slider {
                                    id: flowSlider
                                    implicitWidth: 220
                                    from: 0.25; to: 1.0; stepSize: 0.05
                                    value: backend.flow_scale
                                    onMoved: backend.flow_scale = value
                                }
                                Label {
                                    text: Math.round(flowSlider.value * 100) + "%"
                                    color: cAccent; font.pixelSize: 15; font.bold: true
                                    Layout.preferredWidth: 48; horizontalAlignment: Text.AlignRight
                                }
                            }
                        }
                        Row2 {
                            title: "Performance Mode"
                            subtitle: "Use a significantly lighter generation model."
                            Switch {
                                checked: backend.performance_mode
                                onToggled: backend.performance_mode = checked
                            }
                        }
                    }

                    // Presentation ---------------------------------------------
                    Card {
                        heading: "Presentation"
                        Row2 {
                            title: "Pacing Mode"
                            subtitle: "How generated frames are spaced for presentation."
                            Segmented {
                                implicitWidth: 180
                                model: ["None", "CPU"]
                                currentIndex: backend.pacing_mode
                                onActivated: (index) => backend.pacing_mode = index
                            }
                        }
                        Row2 {
                            title: "GPU"
                            subtitle: "Which device runs frame generation."
                            ComboBox {
                                implicitWidth: 220
                                model: backend.gpus
                                currentIndex: backend.gpu
                                onActivated: (index) => backend.gpu = index
                            }
                        }
                    }

                    // Activation -----------------------------------------------
                    Card {
                        heading: "Activation"
                        Row2 {
                            title: "Active In"
                            subtitle: "Applications this profile applies to (exe / process name)."
                            Button { text: "Edit…"; onClicked: active_in_dialog.open() }
                        }
                    }

                    Item { Layout.preferredHeight: 8 }
                }
            }
        }
    }
}
