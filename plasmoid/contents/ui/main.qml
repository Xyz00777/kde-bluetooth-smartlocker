import QtQuick
import QtQuick.Controls
import org.kde.plasma.plasmoid
import org.kde.smartlocker

PlasmoidItem {
    id: root
    property var devicePaths: []
    SmartLockerClient {
        id: client
        Component.onCompleted: root.devicePaths = devices()
        onStateChanged: root.devicePaths = devices()
    }
    compactRepresentation: Label { text: client.state }
    fullRepresentation: Column {
        spacing: 8
        Label { text: "Bluetooth SmartLocker"; font.bold: true }
        Label { text: "State: " + client.state }
        Switch { text: "Monitoring enabled"; checked: client.state !== "disabled"; onToggled: client.setEnabled(checked) }
        Label {
            visible: root.devicePaths.length === 0
            text: "No devices configured. Add a BlueZ device path to enable locking."
            wrapMode: Text.Wrap
        }
        Repeater {
            model: root.devicePaths
            delegate: DevicePolicyRow { path: modelData; daemonClient: client }
        }
        Button { text: "Snooze 30 seconds"; onClicked: client.snooze(30) }
        Button { text: "Refresh"; onClicked: client.refresh() }
    }
}
