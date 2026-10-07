// qmllint reports `unqualified` warnings here on purpose. The Repeater delegate is a real
// component boundary that legitimately reads the outer `client` id, so declaring
// `pragma ComponentBehavior: Bound` would cut the warnings to 21 but introduce an
// incompatible-type error and break the delegate. Removing them properly requires passing
// the client through the model or exposing it as a singleton, which is disproportionate for
// an example. The warnings are advisory and do not fail the build gate.
import Quickshell
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import org.kde.smartlocker 1.0

FloatingWindow {
    id: root

    title: "Bluetooth SmartLocker"
    visible: true
    width: 320
    height: Math.min(480, layout.implicitHeight + 32)
    color: "#232629"

    SmartLockerClient {
        id: client
    }

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: "Bluetooth SmartLocker"
                font.bold: true
                font.pointSize: 11
                color: "#eff0f1"
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
            color: "#bdc3c7"
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Accessible.name: "Device guidance"
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            Layout.maximumHeight: 280
            contentWidth: availableWidth
            clip: true
            visible: client.devices.length > 0
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            ColumnLayout {
                width: parent.width
                spacing: 8

                Repeater {
                    model: client.devices
                    delegate: ColumnLayout {
                        required property string modelData
                        property string deviceName: client.deviceName(modelData)
                        Layout.fillWidth: true
                        spacing: 4

                        Label {
                            text: deviceName.length > 0 ? (deviceName + " (" + modelData + ")") : modelData
                            color: "#eff0f1"
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                            Accessible.name: "Bluetooth device " + (deviceName.length > 0 ? deviceName : modelData)
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Switch {
                                id: devSwitch
                                text: "Watch device"
                                enabled: client.state !== "unavailable"
                                checked: client.deviceEnabled(modelData)
                                onToggled: client.setDeviceEnabled(modelData, devSwitch.checked)
                                Accessible.name: "Watch " + (deviceName.length > 0 ? deviceName : modelData)
                            }

                            Item { Layout.fillWidth: true }

                            Label {
                                text: "Threshold:"
                                color: "#bdc3c7"
                                Accessible.name: "Signal threshold label"
                            }

                            SpinBox {
                                id: rssiBox
                                from: -100
                                to: 0
                                stepSize: 1
                                enabled: client.state !== "unavailable"
                                value: client.deviceRssiThreshold(modelData)
                                editable: true
                                textFromValue: function(value) { return value + " dBm"; }
                                valueFromText: function(text) { return parseInt(text); }
                                onValueModified: client.setDeviceRssiThreshold(modelData, rssiBox.value)
                                Accessible.name: "RSSI threshold in dBm for " + (deviceName.length > 0 ? deviceName : modelData)
                                Accessible.description: "Signal strength threshold used to determine whether this device is present."
                            }
                        }

                        Connections {
                            target: client
                            function onSettingsChanged() {
                                deviceName = client.deviceName(modelData)
                                devSwitch.checked = client.deviceEnabled(modelData)
                                const threshold = client.deviceRssiThreshold(modelData)
                                if (rssiBox.value !== threshold) {
                                    rssiBox.value = threshold
                                }
                            }
                        }
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
        case "disabled": return "#bdc3c7"
        case "starting": return "#bdc3c7"
        default: return "#f39c12"
        }
    }
}
