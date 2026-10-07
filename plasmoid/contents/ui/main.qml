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
        text: root.statusText
        font.bold: true
        Accessible.name: "Bluetooth SmartLocker status: " + root.statusText
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
                Accessible.name: "Bluetooth SmartLocker"
            }
            Label {
                text: root.statusText
                font.bold: true
                color: root.statusColor
                Accessible.name: "Monitoring status: " + root.statusText
            }
        }

        RowLayout {
            visible: client.lastError.length > 0
            Layout.fillWidth: true
            Label {
                text: client.lastError
                color: "#f39c12"
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                Accessible.name: "Daemon error: " + client.lastError
            }
            Button {
                text: "Dismiss"
                Accessible.name: "Dismiss daemon error"
                onClicked: client.clearError()
            }
        }

        Switch {
            id: enabledSwitch
            text: "Monitoring enabled"
            enabled: client.state !== "unavailable"
            checked: client.state !== "disabled" && client.state !== "unavailable"
            onToggled: client.setEnabled(enabledSwitch.checked)
            Layout.fillWidth: true
            Accessible.name: "Monitoring enabled"
            Accessible.description: "Turn Bluetooth presence monitoring on or off."
        }

        Label {
            visible: client.devices.length === 0
            text: "No paired or trusted Bluetooth devices were found."
            wrapMode: Text.Wrap
            color: "#95a5a6"
            Layout.fillWidth: true
            Accessible.name: "Device guidance"
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
                Accessible.name: "Snooze monitoring for " + client.snoozeSeconds + " seconds"
            }

            Button {
                text: "Refresh"
                onClicked: client.refresh()
                Layout.fillWidth: true
                Accessible.name: "Refresh SmartLocker status and devices"
            }
        }
    }

    property string statusText: {
        switch (client.state) {
        case "starting": return "Starting"
        case "monitoring": return "Monitoring"
        case "away": return "Away"
        case "snoozed": return "Snoozed"
        case "disabled": return "Disabled"
        case "locked": return "Locked"
        case "error": return "Error"
        default: return "Status unknown"
        }
    }
    property color statusColor: {
        switch (client.state) {
        case "monitoring": return "#2ecc71"
        case "away":
        case "snoozed":
        case "error": return "#f39c12"
        case "locked": return "#e74c3c"
        case "disabled": return "#95a5a6"
        case "starting": return "#95a5a6"
        default: return "#f39c12"
        }
    }
}
