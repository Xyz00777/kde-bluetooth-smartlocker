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
        property int snoozeSeconds: 30

        Component.onCompleted: {
            snoozeSeconds = client.snoozeSeconds()
        }
        onSettingsChanged: {
            snoozeSeconds = client.snoozeSeconds()
        }
    }

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8

        Label {
            text: "Bluetooth SmartLocker"
            font.bold: true
            color: "#eff0f1"
            Layout.fillWidth: true
        }

        Label {
            text: "State: " + client.state
            color: "#eff0f1"
            Layout.fillWidth: true
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

        Repeater {
            model: client.devices
            delegate: ColumnLayout {
                required property string modelData
                Layout.fillWidth: true
                spacing: 4

                Label {
                    text: modelData
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

                    Label {
                        text: "RSSI"
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
                        onValueModified: client.setDeviceRssiThreshold(modelData, rssiBox.value)
                    }
                }

                Connections {
                    target: client
                    function onSettingsChanged() {
                        devSwitch.checked = client.deviceEnabled(modelData)
                        const threshold = client.deviceRssiThreshold(modelData)
                        if (rssiBox.value !== threshold) {
                            rssiBox.value = threshold
                        }
                    }
                }
            }
        }

        Button {
            text: "Snooze " + client.snoozeSeconds + " seconds"
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
