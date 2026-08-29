import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.plasma.plasmoid
import org.kde.smartlocker

PlasmoidItem {
    id: root
    property var devicePaths: []
    SmartLockerClient {
        id: client
        Component.onCompleted: root.devicePaths = client.devices()
        onStateChanged: root.devicePaths = client.devices()
    }
    compactRepresentation: Label { text: client.state }
    fullRepresentation: ColumnLayout {
        spacing: 8
        Label { text: "Bluetooth SmartLocker"; font.bold: true; Layout.fillWidth: true }
        Label { text: "State: " + client.state; Layout.fillWidth: true }
        Switch {
            text: "Monitoring enabled"
            enabled: client.state !== "unavailable"
            checked: client.state !== "disabled" && client.state !== "unavailable"
            onToggled: client.setEnabled(checked)
            Layout.fillWidth: true
        }
        Label {
            visible: root.devicePaths.length === 0
            text: "No devices configured. Add a BlueZ device path to enable locking."
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Repeater {
            model: root.devicePaths
            delegate: DevicePolicyRow {
                path: modelData
                daemonClient: client
                Layout.fillWidth: true
            }
        }
        Button {
            text: "Snooze 30 seconds"
            enabled: client.state !== "unavailable" && client.state !== "disabled"
            onClicked: client.snooze(30)
            Layout.fillWidth: true
        }
        Button {
            text: "Refresh"
            onClicked: client.refresh()
            Layout.fillWidth: true
        }
    }
}
