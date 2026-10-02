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
    compactRepresentation: Label { text: client.state }
    fullRepresentation: ColumnLayout {
        spacing: 8
        Label { text: "Bluetooth SmartLocker"; font.bold: true; Layout.fillWidth: true }
        Label { text: "State: " + client.state; Layout.fillWidth: true }
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
            Layout.fillWidth: true
        }
        Repeater {
            model: client.devices
            delegate: DevicePolicyRow {
                required property string modelData
                path: modelData
                daemonClient: client
                Layout.fillWidth: true
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
