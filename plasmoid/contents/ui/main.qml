import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.plasma.plasmoid
import org.kde.smartlocker

PlasmoidItem {
    id: root
    SmartLockerClient {
        id: client
    }
    compactRepresentation: Label {
        text: client.state.length > 0 ? (client.state[0].toUpperCase() + client.state.slice(1)) : ""
        font.bold: true
    }
    fullRepresentation: ColumnLayout {
        spacing: 10
        Layout.minimumWidth: 320

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: "Bluetooth SmartLocker"
                font.bold: true
                font.pointSize: 11
                Layout.fillWidth: true
            }
            Label {
                text: client.state.length > 0 ? (client.state[0].toUpperCase() + client.state.slice(1)) : ""
                font.bold: true
                color: client.state === "monitoring" ? "#2ecc71" : (client.state === "away" ? "#f39c12" : (client.state === "locked" ? "#e74c3c" : "#95a5a6"))
            }
        }

        Switch {
            id: enabledSwitch
            text: "Monitoring enabled"
            enabled: client.state !== "unavailable"
            checked: client.state !== "disabled" && client.state !== "unavailable"
            onToggled: client.setEnabled(enabledSwitch.checked)
            Layout.fillWidth: true
        }

        Label {
            visible: client.devices.length === 0
            text: "No paired or trusted Bluetooth devices were found."
            wrapMode: Text.Wrap
            color: "#95a5a6"
            Layout.fillWidth: true
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.maximumHeight: 280
            contentWidth: availableWidth
            clip: true
            visible: client.devices.length > 0
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            ColumnLayout {
                width: parent.width
                spacing: 6

                Repeater {
                    model: client.devices
                    delegate: DevicePolicyRow {
                        required property string modelData
                        path: modelData
                        daemonClient: client
                        Layout.fillWidth: true
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Button {
                text: "Snooze (" + client.snoozeSeconds + "s)"
                enabled: client.state !== "unavailable" && client.state !== "disabled" && client.snoozeSeconds > 0
                visible: client.snoozeSeconds > 0
                onClicked: client.snooze(client.snoozeSeconds)
                Layout.fillWidth: true
            }

            Button {
                text: "Refresh"
                onClicked: client.refresh()
                Layout.fillWidth: true
            }
        }
    }
}
