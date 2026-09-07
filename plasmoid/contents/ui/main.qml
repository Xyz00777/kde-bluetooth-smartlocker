import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.plasma.plasmoid
import org.kde.smartlocker

PlasmoidItem {
    id: root
    property var devicePaths: []
    property int snoozeSeconds: 30
    function updateDevicePaths() {
        const paths = client.devices()
        if (paths.length !== root.devicePaths.length) {
            root.devicePaths = paths
            return
        }
        for (let i = 0; i < paths.length; ++i) {
            if (paths[i] !== root.devicePaths[i]) {
                root.devicePaths = paths
                return
            }
        }
    }
    SmartLockerClient {
        id: client
        Component.onCompleted: {
            root.updateDevicePaths()
            root.snoozeSeconds = client.snoozeSeconds()
        }
        onSettingsChanged: {
            root.updateDevicePaths()
            root.snoozeSeconds = client.snoozeSeconds()
        }
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
            text: "Snooze " + root.snoozeSeconds + " seconds"
            enabled: client.state !== "unavailable" && client.state !== "disabled"
            onClicked: client.snooze(root.snoozeSeconds)
            Layout.fillWidth: true
        }
        Button {
            text: "Refresh"
            onClicked: client.refresh()
            Layout.fillWidth: true
        }
    }
}
