import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root

    required property string path
    required property var daemonClient

    spacing: 4

    Label {
        Layout.fillWidth: true
        text: root.path
        elide: Text.ElideMiddle
        Accessible.name: "Bluetooth device path"
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        Switch {
            id: enabledSwitch
            text: "Watch device"
            enabled: root.daemonClient.state !== "unavailable"
            checked: root.daemonClient.deviceEnabled(root.path)
            Accessible.name: "Watch Bluetooth device"
            onToggled: root.daemonClient.setDeviceEnabled(root.path, checked)
        }

        Label {
            text: "RSSI"
            Accessible.ignored: true
        }

        SpinBox {
            id: thresholdSpinBox
            from: -100
            to: 0
            stepSize: 1
            enabled: root.daemonClient.state !== "unavailable"
            value: root.daemonClient.deviceRssiThreshold(root.path)
            editable: true
            Accessible.name: "RSSI threshold in dBm"
            onValueModified: root.daemonClient.setDeviceRssiThreshold(root.path, value)
        }
    }
}
