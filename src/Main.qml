import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 520
    height: 760
    minimumWidth: 440
    minimumHeight: 560
    visible: true
    title: "Oma Spend"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    readonly property color panel: "#17171a"
    readonly property color muted: "#8a8a93"
    readonly property color danger: "#ff6b6b"
    readonly property color good: "#7bd88f"
    property bool helpVisible: false
    property bool fundMode: false

    // Reading translator.language makes each binding that formats money
    // re-render when the Omarchy language changes.
    function money(cents) {
        translator.language
        return backend.money(cents)
    }

    function startEntry(asFund) {
        fundMode = asFund
        amountField.forceActiveFocus()
        amountField.selectAll()
    }

    function submit() {
        const ok = fundMode ? backend.addFund(amountField.text, noteField.text)
                            : backend.addExpense(amountField.text, noteField.text)
        if (ok) {
            amountField.clear()
            noteField.clear()
            amountField.forceActiveFocus()
        }
    }

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; onActivated: win.startEntry(false) }
    Shortcut { sequence: "E"; context: Qt.ApplicationShortcut; onActivated: win.startEntry(false) }
    Shortcut { sequence: "F"; context: Qt.ApplicationShortcut; onActivated: win.startEntry(true) }
    Shortcut { sequences: [StandardKey.Undo]; context: Qt.ApplicationShortcut; onActivated: backend.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Shift+Z"]; context: Qt.ApplicationShortcut; onActivated: backend.redo() }
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (win.helpVisible) { win.helpVisible = false; return }
            amountField.clear()
            noteField.clear()
            list.forceActiveFocus()
        }
    }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 16

            // The month and where we are in it.
            ColumnLayout {
                spacing: 2
                Label {
                    text: backend.monthTitle
                    font.pixelSize: 24
                    font.bold: true
                }
                Label {
                    text: qsTr("Day %1 of %2").arg(backend.dayOfMonth).arg(backend.daysInMonth)
                          + "  ·  " + qsTr("%n days left", "", backend.daysLeft)
                    color: win.muted
                }
            }

            // What can be spent per day for the rest of the month.
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: hero.implicitHeight + 40
                radius: theme.radius
                color: win.panel

                ColumnLayout {
                    id: hero
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 4

                    Label {
                        visible: backend.fund === 0
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTr("Add this month's fund to see what you can spend each day.")
                        font.pixelSize: 16
                    }
                    Label {
                        visible: backend.fund === 0
                        text: qsTr("Press F to add a fund.")
                        color: win.muted
                    }

                    Label {
                        visible: backend.fund !== 0
                        text: qsTr("You can spend")
                        color: win.muted
                    }
                    Label {
                        visible: backend.fund !== 0
                        text: win.money(Math.max(0, backend.dailyAllowance))
                        font.pixelSize: 44
                        font.bold: true
                        color: backend.dailyAllowance > 0 ? theme.accent : win.danger
                    }
                    Label {
                        visible: backend.fund !== 0
                        text: qsTr("per day for the rest of the month")
                        color: win.muted
                    }
                    Label {
                        visible: backend.fund !== 0
                        Layout.topMargin: 10
                        text: backend.leftToday >= 0
                              ? qsTr("Left today: %1").arg(win.money(backend.leftToday))
                              : qsTr("Over today by %1").arg(win.money(-backend.leftToday))
                        color: backend.leftToday >= 0 ? win.good : win.danger
                        font.pixelSize: 16
                    }
                    Label {
                        visible: backend.fund !== 0 && backend.daysLeft > 1 && backend.spentToday > 0
                        text: qsTr("From tomorrow: %1 per day").arg(win.money(Math.max(0, backend.tomorrowAllowance)))
                        color: win.muted
                    }
                    Label {
                        visible: backend.fund !== 0 && backend.remaining < 0
                        text: qsTr("Over this month's fund by %1").arg(win.money(-backend.remaining))
                        color: win.danger
                    }
                }
            }

            // Fund, spent and left for the month.
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Repeater {
                    model: [
                        { label: qsTr("Fund"), cents: backend.fund },
                        { label: qsTr("Spent"), cents: backend.spent },
                        { label: qsTr("Left"), cents: backend.remaining }
                    ]
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        implicitHeight: 64
                        radius: theme.radius
                        color: win.panel
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 2
                            Label { text: modelData.label; color: win.muted; font.pixelSize: 12 }
                            Label {
                                Layout.fillWidth: true
                                text: win.money(modelData.cents)
                                font.pixelSize: 17
                                font.bold: true
                                elide: Text.ElideRight
                                color: modelData.cents < 0 ? win.danger : Material.foreground
                            }
                        }
                    }
                }
            }

            // Adding an expense or a fund.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                RowLayout {
                    spacing: 8
                    Button {
                        text: qsTr("Expense")
                        checkable: true
                        checked: !win.fundMode
                        flat: true
                        onClicked: win.startEntry(false)
                    }
                    Button {
                        text: qsTr("Fund")
                        checkable: true
                        checked: win.fundMode
                        flat: true
                        onClicked: win.startEntry(true)
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    TextField {
                        id: amountField
                        Layout.preferredWidth: 130
                        placeholderText: qsTr("Amount")
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        onAccepted: win.submit()
                    }
                    TextField {
                        id: noteField
                        Layout.fillWidth: true
                        placeholderText: win.fundMode ? qsTr("Note, like Salary") : qsTr("Note, like Lunch")
                        onAccepted: win.submit()
                    }
                    Button {
                        text: win.fundMode ? qsTr("Add fund") : qsTr("Add expense")
                        highlighted: true
                        onClicked: win.submit()
                    }
                }

                Label {
                    visible: backend.error !== ""
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: backend.error
                    color: win.danger
                }
            }

            // This month's entries, newest first.
            ListView {
                id: list
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 2
                model: backend.entries
                ScrollBar.vertical: ScrollBar {}

                Label {
                    anchors.centerIn: parent
                    visible: list.count === 0
                    text: qsTr("Nothing recorded this month yet.")
                    color: win.muted
                }

                delegate: Item {
                    id: row
                    required property var modelData
                    width: ListView.view.width
                    height: 44

                    HoverHandler { id: hover }

                    Rectangle {
                        anchors.fill: parent
                        radius: Math.min(4, theme.radius)
                        color: hover.hovered ? win.panel : "transparent"
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 4
                        spacing: 12

                        Label {
                            Layout.preferredWidth: 56
                            text: row.modelData.today ? qsTr("Today") : row.modelData.day
                            color: win.muted
                        }
                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: row.modelData.note !== "" ? row.modelData.note
                                  : row.modelData.kind === "fund" ? qsTr("Fund") : qsTr("Expense")
                        }
                        Label {
                            text: (row.modelData.kind === "fund" ? "+" : "−") + win.money(row.modelData.cents)
                            color: row.modelData.kind === "fund" ? win.good : Material.foreground
                            font.bold: true
                        }
                        ToolButton {
                            text: "✕"
                            opacity: hover.hovered ? 1 : 0
                            onClicked: backend.removeEntry(row.modelData.id)
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Remove (Ctrl+Z brings it back)")
                        }
                    }
                }
            }

            Label {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Press ? for shortcuts")
                color: win.muted
                font.pixelSize: 12
            }
        }

        // Every shortcut, shown with ?.
        Rectangle {
            anchors.fill: parent
            visible: win.helpVisible
            color: "#d90e0e10"

            MouseArea { anchors.fill: parent; onClicked: win.helpVisible = false }

            Rectangle {
                anchors.centerIn: parent
                width: Math.min(parent.width - 48, 380)
                height: helpGrid.implicitHeight + 48
                radius: theme.radius
                color: win.panel

                ColumnLayout {
                    id: helpGrid
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 10

                    Repeater {
                        model: [
                            ["E / Space", qsTr("Add an expense")],
                            ["F", qsTr("Add to this month's fund")],
                            ["Enter", qsTr("Save the entry")],
                            ["Esc", qsTr("Clear the entry")],
                            ["Ctrl+Z", qsTr("Undo")],
                            ["Ctrl+Shift+Z", qsTr("Redo")],
                            ["?", qsTr("Show or hide shortcuts")],
                            ["Q", qsTr("Quit")]
                        ]
                        delegate: RowLayout {
                            required property var modelData
                            spacing: 16
                            Label { Layout.preferredWidth: 110; text: modelData[0]; color: theme.accent; font.bold: true }
                            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: modelData[1] }
                        }
                    }
                }
            }
        }
    }
}
