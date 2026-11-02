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

        const threshold = root.daemonClient.deviceRssiThreshold(root.path);
        if (!thresholdSlider.pressed
                && !thresholdCommitTimer.running
                && thresholdSlider.value !== threshold) {
            thresholdSlider.value = threshold;
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
            spacing: Kirigami.Units.smallSpacing
            opacity: root.watchEnabled ? 1 : 0.65

            PlasmaComponents3.Label {
                text: "Signal threshold"
                Accessible.ignored: true
            }

            PlasmaComponents3.Slider {
                id: thresholdSlider

                Layout.fillWidth: true
                Layout.minimumWidth: Kirigami.Units.gridUnit * 4
                from: -100
                to: 0
                stepSize: 1
                snapMode: QQC2.Slider.SnapAlways
                enabled: root.daemonAvailable
                Accessible.name: "Signal threshold for " + root.displayName
                Accessible.description: "Lower is stricter; -100 dBm effectively always present."

                onMoved: thresholdCommitTimer.restart()
            }

            PlasmaComponents3.Label {
                Layout.minimumWidth: Kirigami.Units.gridUnit * 3
                horizontalAlignment: Text.AlignRight
                text: Math.round(thresholdSlider.value) + " dBm"
                Accessible.ignored: true
            }
        }

        PlasmaComponents3.Label {
            Layout.fillWidth: true
            text: "Lower is stricter."
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

    Connections {
        target: root.daemonClient

        function onSettingsChanged() {
            root.syncFromClient();
        }
    }
}
