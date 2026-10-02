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
    implicitHeight: layout.implicitHeight + 32
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
            }
            Label {
                text: client.state.length > 0 ? (client.state[0].toUpperCase() + client.state.slice(1)) : ""
                font.bold: true
                color: client.state === "monitoring" ? "#2ecc71" : (client.state === "away" ? "#f39c12" : (client.state === "locked" ? "#e74c3c" : "#bdc3c7"))
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
            color: "#bdc3c7"
            wrapMode: Text.Wrap
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
                    }

                    Item { Layout.fillWidth: true }

                    Label {
                        text: "Threshold:"
                        color: "#bdc3c7"
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
