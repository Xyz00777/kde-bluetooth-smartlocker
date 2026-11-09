pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.plasma.components as PlasmaComponents3
import org.kde.kirigami as Kirigami

PlasmaComponents3.Frame {
    id: root

    required property string path
    required property var daemonClient

    property string deviceName: ""
    property bool watchEnabled: true
    property bool bleEnabled: true
    property bool connectionEnabled: true
    property int connectionThresholdValue: -70
    property bool bleSignalAvailable: false
    property int bleSignalDbm: 0
    property bool connectionSignalAvailable: false
    property int connectionSignalDbm: 0
    property bool syncInProgress: false

    readonly property bool daemonAvailable: root.daemonClient.state !== "unavailable"
    readonly property string displayName: root.deviceName.length > 0 ? root.deviceName : "Unknown device"

    Layout.fillWidth: true
    padding: Kirigami.Units.largeSpacing

    function syncFromClient() {
        root.deviceName = root.daemonClient.deviceName(root.path);

        const enabled = root.daemonClient.deviceEnabled(root.path);
        root.watchEnabled = enabled;
        if (watchSwitch.checked !== enabled) {
            root.syncInProgress = true;
            watchSwitch.checked = enabled;
            root.syncInProgress = false;
        }

        const ble = root.daemonClient.deviceBleEnabled(root.path);
        if (bleSwitch.checked !== ble) {
            root.syncInProgress = true;
            bleSwitch.checked = ble;
            root.syncInProgress = false;
        }
        root.bleEnabled = ble;

        const connection = root.daemonClient.deviceConnectionEnabled(root.path);
        if (connectionSwitch.checked !== connection) {
            root.syncInProgress = true;
            connectionSwitch.checked = connection;
            root.syncInProgress = false;
        }
        root.connectionEnabled = connection;

        root.bleSignalAvailable = root.daemonClient.deviceBleRssiAvailable(root.path);
        root.bleSignalDbm = root.daemonClient.deviceBleRssiDbm(root.path);
        root.connectionSignalAvailable = root.daemonClient.deviceConnectionRssiAvailable(root.path);
        root.connectionSignalDbm = root.daemonClient.deviceConnectionRssiDbm(root.path);

        const threshold = root.daemonClient.deviceRssiThreshold(root.path);
        if (!thresholdSlider.pressed
                && !thresholdCommitTimer.running
                && thresholdSlider.value !== threshold) {
            thresholdSlider.value = threshold;
        }

        const connectionThreshold = root.daemonClient.deviceConnectionRssiThreshold(root.path);
        root.connectionThresholdValue = connectionThreshold;
        if (!connectionThresholdSlider.pressed
                && !connectionThresholdCommitTimer.running
                && connectionThresholdSlider.value !== connectionThreshold) {
            connectionThresholdSlider.value = connectionThreshold;
        }
    }

    Component.onCompleted: root.syncFromClient()

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                PlasmaComponents3.Label {
                    Layout.fillWidth: true
                    text: root.displayName
                    font.bold: true
                    elide: Text.ElideRight
                    Accessible.name: "Bluetooth device " + root.displayName
                }

                PlasmaComponents3.Label {
                    Layout.fillWidth: true
                    text: root.path
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                    elide: Text.ElideMiddle
                    Accessible.name: "Bluetooth address " + root.path
                }
            }

            PlasmaComponents3.Switch {
                id: watchSwitch

                text: "Watch"
                enabled: root.daemonAvailable
                Accessible.name: "Watch " + root.displayName
                Accessible.description: "Include this device in the presence policy."

                onToggled: {
                    if (root.syncInProgress) {
                        return;
                    }
                    root.watchEnabled = checked;
                    root.daemonClient.setDeviceEnabled(root.path, checked);
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing

            PlasmaComponents3.Switch {
                id: bleSwitch
                text: "BLE scan"
                enabled: root.daemonAvailable && root.watchEnabled
                Accessible.name: "BLE scanning for " + root.displayName
                Accessible.description: "Use Bluetooth Low Energy signal strength to detect this device while it is advertising."
                onToggled: {
                    if (root.syncInProgress) return;
                    root.bleEnabled = checked;
                    root.daemonClient.setDeviceBleEnabled(root.path, checked);
                }
            }

            PlasmaComponents3.Switch {
                id: connectionSwitch
                text: "Connection scan"
                enabled: root.daemonAvailable && root.watchEnabled
                Accessible.name: "Connection scanning for " + root.displayName
                Accessible.description: "Use an active Bluetooth connection to detect this device."
                onToggled: {
                    if (root.syncInProgress) return;
                    root.connectionEnabled = checked;
                    root.daemonClient.setDeviceConnectionEnabled(root.path, checked);
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing
            opacity: root.watchEnabled && root.bleEnabled ? 1 : 0.65

            PlasmaComponents3.Label {
                text: "BLE range"
                Accessible.ignored: true
            }

            PlasmaComponents3.Label {
                Layout.minimumWidth: Kirigami.Units.gridUnit * 5
                horizontalAlignment: Text.AlignRight
                text: root.bleSignalAvailable ? root.bleSignalDbm + " dBm" : "—"
                color: root.bleSignalAvailable
                    ? (root.bleSignalDbm >= Math.round(thresholdSlider.value)
                        ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.neutralTextColor)
                    : Kirigami.Theme.disabledTextColor
                Accessible.name: root.bleSignalAvailable ? "Latest BLE signal " + root.bleSignalDbm + " dBm" : "No recent BLE signal"
            }

            Item {
                id: sliderTrack

                Layout.fillWidth: true
                Layout.minimumWidth: Kirigami.Units.gridUnit * 4
                implicitHeight: thresholdSlider.implicitHeight

                PlasmaComponents3.Slider {
                    id: thresholdSlider

                    anchors.fill: parent
                    from: -100
                    to: 0
                    stepSize: 1
                    snapMode: QQC2.Slider.SnapAlways
                    enabled: root.daemonAvailable && root.watchEnabled && root.bleEnabled
                    Accessible.name: "BLE signal threshold for " + root.displayName
                    Accessible.description: "The colored signal marker shows the latest fresh RSSI; the handle is the configured threshold."

                    onMoved: thresholdCommitTimer.restart()
                }

                Rectangle {
                    id: signalMarker

                    width: Kirigami.Units.smallSpacing / 2
                    height: Kirigami.Units.gridUnit * 0.75
                    x: thresholdSlider.leftPadding
                        + Math.max(0, Math.min(1, (root.bleSignalDbm - thresholdSlider.from)
                            / (thresholdSlider.to - thresholdSlider.from))) * thresholdSlider.availableWidth - width / 2
                    y: (parent.height - height) / 2
                    radius: width / 2
                    color: Kirigami.Theme.highlightColor
                    visible: root.bleSignalAvailable && root.watchEnabled && root.bleEnabled
                        && Math.abs(root.bleSignalDbm - Math.round(thresholdSlider.value)) > 1
                    Accessible.ignored: true
                    z: 2

                    Behavior on x {
                        NumberAnimation {
                            duration: 150
                            easing.type: Easing.OutCubic
                        }
                    }
                }
            }

            PlasmaComponents3.Label {
                Layout.minimumWidth: Kirigami.Units.gridUnit * 3
                horizontalAlignment: Text.AlignRight
                text: Math.round(thresholdSlider.value) + " dBm"
                Accessible.ignored: true
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing
            opacity: root.watchEnabled && root.connectionEnabled ? 1 : 0.65

            PlasmaComponents3.Label {
                text: "Connection"
                Accessible.ignored: true
            }

            PlasmaComponents3.Label {
                Layout.minimumWidth: Kirigami.Units.gridUnit * 5
                horizontalAlignment: Text.AlignRight
                text: root.connectionSignalAvailable ? root.connectionSignalDbm + " dBm" : "—"
                color: root.connectionSignalAvailable
                    ? (root.connectionSignalDbm >= Math.round(connectionThresholdSlider.value)
                        ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.neutralTextColor)
                    : Kirigami.Theme.disabledTextColor
                Accessible.name: root.connectionSignalAvailable
                    ? "Latest connection signal " + root.connectionSignalDbm + " dBm" : "No recent connection signal"
            }

            Item {
                id: connectionSliderTrack

                Layout.fillWidth: true
                Layout.minimumWidth: Kirigami.Units.gridUnit * 4
                implicitHeight: connectionThresholdSlider.implicitHeight

                PlasmaComponents3.Slider {
                    id: connectionThresholdSlider

                    anchors.fill: parent
                    from: -100
                    to: 0
                    stepSize: 1
                    snapMode: QQC2.Slider.SnapAlways
                    value: root.connectionThresholdValue
                    enabled: root.daemonAvailable && root.watchEnabled && root.connectionEnabled
                    Accessible.name: "Connection signal threshold for " + root.displayName
                    Accessible.description: "The active connection must meet this signal strength to count as present."

                    onMoved: connectionThresholdCommitTimer.restart()
                }

                Rectangle {
                    width: Kirigami.Units.smallSpacing / 2
                    height: Kirigami.Units.gridUnit * 0.75
                    x: connectionThresholdSlider.leftPadding
                        + Math.max(0, Math.min(1, (root.connectionSignalDbm - connectionThresholdSlider.from)
                            / (connectionThresholdSlider.to - connectionThresholdSlider.from))) * connectionThresholdSlider.availableWidth - width / 2
                    y: (parent.height - height) / 2
                    radius: width / 2
                    color: Kirigami.Theme.highlightColor
                    visible: root.connectionSignalAvailable && root.watchEnabled && root.connectionEnabled
                        && Math.abs(root.connectionSignalDbm - Math.round(connectionThresholdSlider.value)) > 1
                    Accessible.ignored: true
                    z: 2

                    Behavior on x {
                        NumberAnimation {
                            duration: 150
                            easing.type: Easing.OutCubic
                        }
                    }
                }
            }

            PlasmaComponents3.Label {
                Layout.minimumWidth: Kirigami.Units.gridUnit * 3
                horizontalAlignment: Text.AlignRight
                text: Math.round(connectionThresholdSlider.value) + " dBm"
                Accessible.ignored: true
            }
        }

        PlasmaComponents3.Label {
            Layout.fillWidth: true
            text: "Colored markers show each source's latest fresh signal; slider handles are the saved thresholds."
            font: Kirigami.Theme.smallFont
            color: Kirigami.Theme.disabledTextColor
            wrapMode: Text.Wrap
            opacity: root.watchEnabled ? 1 : 0.65
            Accessible.ignored: true
        }
    }

    Timer {
        id: thresholdCommitTimer
        interval: 300
        repeat: false
        onTriggered: root.daemonClient.setDeviceRssiThreshold(root.path, Math.round(thresholdSlider.value))
    }

    Timer {
        id: connectionThresholdCommitTimer
        interval: 300
        repeat: false
        onTriggered: root.daemonClient.setDeviceConnectionRssiThreshold(root.path, Math.round(connectionThresholdSlider.value))
    }

    Connections {
        target: root.daemonClient

        function onSettingsChanged() {
            root.syncFromClient();
        }
    }
}
