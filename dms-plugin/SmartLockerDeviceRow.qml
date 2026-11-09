pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import qs.Common
import qs.DankCommon.Common
import qs.Widgets

StyledRect {
    id: root

    required property string address
    required property var bridge
    property bool showThreshold: true

    property bool deviceChecked: false
    property bool devicePending: false
    property bool deviceDesired: false
    property bool bleEnabled: true
    property bool connectionEnabled: true
    property bool bleSignalAvailable: false
    property int bleSignalDbm: 0
    property bool connectionSignalAvailable: false
    property int connectionSignalDbm: 0
    property int thresholdValue: -70
    property int lastCommittedThreshold: -70
    property int connectionThresholdValue: -70
    property int lastCommittedConnectionThreshold: -70
    property bool ready: false

    property string displayName: "Unknown device"

    width: parent ? parent.width : 0
    implicitHeight: content.implicitHeight + Theme.spacingM * 2
    height: implicitHeight
    radius: Theme.cornerRadius
    color: Theme.surfaceContainerHigh

    Accessible.name: displayName
    Accessible.description: address + (deviceChecked ? " is being watched" : " is not being watched")

    function syncChecked() {
        const actual = root.bridge.deviceEnabled(root.address);
        if (root.devicePending) {
            if (actual === root.deviceDesired) {
                root.devicePending = false;
                root.deviceChecked = actual;
            }
            return;
        }
        root.deviceChecked = actual;
    }

    function syncThreshold() {
        if (!root.ready || thresholdSlider.isDragging || connectionThresholdSlider.isDragging
                || commitTimer.running || connectionCommitTimer.running)
            return;
        const value = root.bridge.deviceRssiThreshold(root.address);
        root.thresholdValue = value;
        root.lastCommittedThreshold = value;
        // A user drag replaces the declarative value binding. Push later
        // daemon-side changes explicitly so the slider stays truthful.
        if (thresholdSlider.value !== value)
            thresholdSlider.value = value;

        const connectionValue = root.bridge.deviceConnectionRssiThreshold(root.address);
        root.connectionThresholdValue = connectionValue;
        root.lastCommittedConnectionThreshold = connectionValue;
        if (connectionThresholdSlider.value !== connectionValue)
            connectionThresholdSlider.value = connectionValue;
    }

    function syncScanModes() {
        root.bleEnabled = root.bridge.deviceBleEnabled(root.address);
        root.connectionEnabled = root.bridge.deviceConnectionEnabled(root.address);
    }

    function syncFromBridge() {
        if (!root.bridge)
            return;
        root.displayName = root.bridge.deviceName(root.address) || "Unknown device";
        root.syncChecked();
        root.syncScanModes();
        root.bleSignalAvailable = root.bridge.deviceBleRssiAvailable(root.address);
        root.bleSignalDbm = root.bridge.deviceBleRssiDbm(root.address);
        root.connectionSignalAvailable = root.bridge.deviceConnectionRssiAvailable(root.address);
        root.connectionSignalDbm = root.bridge.deviceConnectionRssiDbm(root.address);
        root.syncThreshold();
    }

    function commitThreshold(value) {
        if (value === root.lastCommittedThreshold)
            return;
        root.lastCommittedThreshold = value;
        root.bridge.setDeviceRssiThreshold(root.address, value);
    }

    function commitConnectionThreshold(value) {
        if (value === root.lastCommittedConnectionThreshold)
            return;
        root.lastCommittedConnectionThreshold = value;
        root.bridge.setDeviceConnectionRssiThreshold(root.address, value);
    }

    Component.onCompleted: {
        root.ready = true;
        root.syncFromBridge();
    }

    onAddressChanged: {
        if (root.ready)
            root.syncFromBridge();
    }

    onBridgeChanged: {
        if (root.ready)
            root.syncFromBridge();
    }

    Connections {
        target: root.bridge

        function onChanged() {
            root.syncFromBridge();
        }
    }

    Timer {
        id: commitTimer
        interval: 400
        repeat: false
        onTriggered: root.commitThreshold(root.thresholdValue)
    }

    Timer {
        id: connectionCommitTimer
        interval: 400
        repeat: false
        onTriggered: root.commitConnectionThreshold(root.connectionThresholdValue)
    }

    Timer {
        id: devicePendingTimer
        interval: 2000
        repeat: false
        onTriggered: {
            root.devicePending = false;
            root.syncChecked();
        }
    }

    Column {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.spacingM
        spacing: Theme.spacingS
        opacity: root.deviceChecked ? 1 : 0.6

        RowLayout {
            width: parent.width
            spacing: Theme.spacingM

            StyledRect {
                Layout.preferredWidth: 36
                Layout.preferredHeight: 36
                Layout.alignment: Qt.AlignTop
                radius: 18
                color: Theme.withAlpha(Theme.primary, 0.12)

                DankIcon {
                    anchors.centerIn: parent
                    name: "devices_other"
                    size: 20
                    color: Theme.primary
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingXXS

                StyledText {
                    Layout.fillWidth: true
                    text: root.displayName
                    font.pixelSize: Theme.fontSizeMedium
                    font.weight: Font.Medium
                    color: Theme.surfaceText
                    wrapMode: Text.NoWrap
                    elide: Text.ElideRight
                }

                StyledText {
                    Layout.fillWidth: true
                    text: root.address
                    isMonospace: true
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.surfaceVariantText
                    wrapMode: Text.NoWrap
                    elide: Text.ElideMiddle
                }
            }

            Item {
                Layout.preferredWidth: deviceToggle.trackWidth
                Layout.preferredHeight: 36

                DankToggle {
                    id: deviceToggle
                    hideText: true
                    width: deviceToggle.trackWidth
                    height: deviceToggle.trackHeight
                    anchors.verticalCenter: parent.verticalCenter
                    checked: root.deviceChecked
                    enabled: root.bridge.available
                    onToggled: isChecked => {
                        root.deviceChecked = isChecked;
                        root.deviceDesired = isChecked;
                        root.devicePending = true;
                        root.bridge.setDeviceEnabled(root.address, isChecked);
                        devicePendingTimer.restart();
                    }
                    Accessible.name: "Watch " + root.displayName
                    Accessible.description: root.deviceChecked ? "Bluetooth presence for this device is watched" : "Bluetooth presence for this device is ignored"
                }
            }
        }

        Column {
            width: parent.width
            spacing: Theme.spacingXXS
            visible: root.deviceChecked

            Column {
                width: parent.width
                spacing: Theme.spacingXXS

                DankToggle {
                    width: parent.width
                    text: "BLE advertisements"
                    description: "Use signal strength to detect this device"
                    checked: root.bleEnabled
                    enabled: root.bridge.available
                    onToggled: isChecked => {
                        root.bleEnabled = isChecked;
                        root.bridge.setDeviceBleEnabled(root.address, isChecked);
                    }
                    Accessible.name: "BLE scanning for " + root.displayName
                }

                DankToggle {
                    width: parent.width
                    text: "Bluetooth connection"
                    description: "Count an active connection as present"
                    checked: root.connectionEnabled
                    enabled: root.bridge.available
                    onToggled: isChecked => {
                        root.connectionEnabled = isChecked;
                        root.bridge.setDeviceConnectionEnabled(root.address, isChecked);
                    }
                    Accessible.name: "Connection scanning for " + root.displayName
                }
            }

            Column {
                width: parent.width
                spacing: Theme.spacingXXS
                visible: root.showThreshold
                opacity: root.bleEnabled ? 1 : 0.6

                RowLayout {
                    width: parent.width
                    spacing: Theme.spacingS

                    StyledText {
                        text: "BLE range"
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.surfaceVariantText
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    StyledText {
                        text: root.bleSignalAvailable ? "RSSI " + root.bleSignalDbm + " dBm" : "No recent BLE RSSI"
                        font.pixelSize: Theme.fontSizeSmall
                        font.weight: Font.Medium
                        color: !root.bleSignalAvailable ? Theme.surfaceVariantText
                            : root.bleSignalDbm >= root.thresholdValue ? Theme.success : Theme.warning
                    }

                    StyledText {
                        text: "Target " + root.thresholdValue + " dBm"
                        font.pixelSize: Theme.fontSizeSmall
                        font.weight: Font.Medium
                        color: Theme.primary
                    }
                }

                Item {
                    id: sliderTrack
                    width: parent.width
                    height: thresholdSlider.height

                    DankSlider {
                        id: thresholdSlider
                        width: parent.width
                        minimum: -100
                        maximum: 0
                        step: 1
                        unit: " dBm"
                        value: root.thresholdValue
                        enabled: root.bridge.available && root.deviceChecked && root.bleEnabled
                        wheelEnabled: false
                        onSliderValueChanged: newValue => {
                            root.thresholdValue = newValue;
                            commitTimer.restart();
                        }
                        onSliderDragFinished: finalValue => {
                            root.thresholdValue = finalValue;
                            commitTimer.stop();
                            root.commitThreshold(finalValue);
                        }
                        Accessible.name: "BLE signal threshold"
                        Accessible.description: "BLE advertisements must meet this signal strength; connected-device signal uses the separate connection threshold"
                    }

                    Rectangle {
                        id: signalMarker
                        width: Theme.spacingXS
                        height: Theme.spacingL
                        y: (parent.height - height) / 2
                        x: {
                            const ratio = Math.max(0, Math.min(1, (root.bleSignalDbm - thresholdSlider.minimum)
                                / (thresholdSlider.maximum - thresholdSlider.minimum)));
                            return ratio * (parent.width - width - Theme.spacingXXS) + Theme.spacingXXS / 2;
                        }
                        radius: width / 2
                        color: Theme.primary
                        visible: root.bleSignalAvailable && root.bleEnabled
                            && Math.abs(root.bleSignalDbm - root.thresholdValue) > 1
                        Accessible.ignored: true

                        Behavior on x {
                            NumberAnimation {
                                duration: Style.mediumDuration
                                easing.type: Style.standardEasing
                            }
                        }
                    }
                }
            }

            Column {
                width: parent.width
                spacing: Theme.spacingXXS
                visible: root.showThreshold
                opacity: root.connectionEnabled ? 1 : 0.6

                RowLayout {
                    width: parent.width
                    spacing: Theme.spacingS

                    StyledText {
                        text: "Connection strength"
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.surfaceVariantText
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    StyledText {
                        text: root.connectionSignalAvailable ? "RSSI " + root.connectionSignalDbm + " dBm" : "No recent connection RSSI"
                        font.pixelSize: Theme.fontSizeSmall
                        font.weight: Font.Medium
                        color: !root.connectionSignalAvailable ? Theme.surfaceVariantText
                            : root.connectionSignalDbm >= root.connectionThresholdValue ? Theme.success : Theme.warning
                    }

                    StyledText {
                        text: "Target " + root.connectionThresholdValue + " dBm"
                        font.pixelSize: Theme.fontSizeSmall
                        font.weight: Font.Medium
                        color: Theme.primary
                    }
                }

                Item {
                    id: connectionSliderTrack
                    width: parent.width
                    height: connectionThresholdSlider.height

                    DankSlider {
                        id: connectionThresholdSlider
                        width: parent.width
                        minimum: -100
                        maximum: 0
                        step: 1
                        unit: " dBm"
                        value: root.connectionThresholdValue
                        enabled: root.bridge.available && root.deviceChecked && root.connectionEnabled
                        wheelEnabled: false
                        onSliderValueChanged: newValue => {
                            root.connectionThresholdValue = newValue;
                            connectionCommitTimer.restart();
                        }
                        onSliderDragFinished: finalValue => {
                            root.connectionThresholdValue = finalValue;
                            connectionCommitTimer.stop();
                            root.commitConnectionThreshold(finalValue);
                        }
                        Accessible.name: "Connection signal threshold"
                        Accessible.description: "The active connection must meet this signal strength to count as present"
                    }

                    Rectangle {
                        id: connectionSignalMarker
                        width: Theme.spacingXS
                        height: Theme.spacingL
                        y: (parent.height - height) / 2
                        x: {
                            const ratio = Math.max(0, Math.min(1, (root.connectionSignalDbm - connectionThresholdSlider.minimum)
                                / (connectionThresholdSlider.maximum - connectionThresholdSlider.minimum)));
                            return ratio * (parent.width - width - Theme.spacingXXS) + Theme.spacingXXS / 2;
                        }
                        radius: width / 2
                        color: Theme.primary
                        visible: root.connectionSignalAvailable && root.connectionEnabled
                            && Math.abs(root.connectionSignalDbm - root.connectionThresholdValue) > 1
                        Accessible.ignored: true

                        Behavior on x {
                            NumberAnimation {
                                duration: Style.mediumDuration
                                easing.type: Style.standardEasing
                            }
                        }
                    }
                }
            }
        }
    }
}
