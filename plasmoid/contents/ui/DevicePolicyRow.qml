import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root

    required property string path
    required property var daemonClient

    property string deviceName: root.daemonClient.deviceName(root.path)

    spacing: 4

    Label {
        Layout.fillWidth: true
        text: root.deviceName.length > 0 ? (root.deviceName + " (" + root.path + ")") : root.path
        elide: Text.ElideMiddle
        Accessible.name: "Bluetooth device"
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        Switch {
            id: enabledSwitch
            text: "Watch device"
            enabled: root.daemonClient.state !== "unavailable"
            checked: root.daemonClient.deviceEnabled(root.path)
            Accessible.name: "Watch " + (root.deviceName.length > 0 ? root.deviceName : root.path)
            onToggled: root.daemonClient.setDeviceEnabled(root.path, enabledSwitch.checked)
        }

        Item { Layout.fillWidth: true }

        Label {
            text: "Threshold:"
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
            textFromValue: function(value) { return value + " dBm"; }
            valueFromText: function(text) { return parseInt(text); }
            Accessible.name: "RSSI threshold in dBm for " + (root.deviceName.length > 0 ? root.deviceName : root.path)
            onValueModified: root.daemonClient.setDeviceRssiThreshold(root.path, thresholdSpinBox.value)
        }
    }

    Connections {
        target: root.daemonClient
        function onSettingsChanged() {
            root.deviceName = root.daemonClient.deviceName(root.path)
            enabledSwitch.checked = root.daemonClient.deviceEnabled(root.path)
            const threshold = root.daemonClient.deviceRssiThreshold(root.path)
            if (thresholdSpinBox.value !== threshold) {
                thresholdSpinBox.value = threshold
            }
        }
    }
}
