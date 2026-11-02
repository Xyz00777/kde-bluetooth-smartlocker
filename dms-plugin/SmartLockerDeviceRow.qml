pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import qs.Common
import qs.Widgets

StyledRect {
    id: root

    required property string address
    required property var bridge
    property bool showThreshold: true

    property bool deviceChecked: false
    property bool devicePending: false
    property bool deviceDesired: false
    property int thresholdValue: -70
    property int lastCommittedThreshold: -70
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
        if (!root.ready || thresholdSlider.isDragging || commitTimer.running)
            return;
        const value = root.bridge.deviceRssiThreshold(root.address);
        root.thresholdValue = value;
        root.lastCommittedThreshold = value;
        // A user drag replaces the declarative value binding. Push later
        // daemon-side changes explicitly so the slider stays truthful.
        if (thresholdSlider.value !== value)
            thresholdSlider.value = value;
    }

    function syncFromBridge() {
        if (!root.bridge)
            return;
        root.displayName = root.bridge.deviceName(root.address) || "Unknown device";
        root.syncChecked();
        root.syncThreshold();
    }

    function commitThreshold(value) {
        if (value === root.lastCommittedThreshold)
            return;
        root.lastCommittedThreshold = value;
        root.bridge.setDeviceRssiThreshold(root.address, value);
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
            visible: root.showThreshold && root.deviceChecked

            RowLayout {
                width: parent.width
                spacing: Theme.spacingS

                StyledText {
                    text: "Signal threshold"
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.surfaceVariantText
                }

                Item {
                    Layout.fillWidth: true
                }

                StyledText {
                    text: root.thresholdValue + " dBm"
                    font.pixelSize: Theme.fontSizeSmall
                    font.weight: Font.Medium
                    color: Theme.primary
                }
            }

            DankSlider {
                id: thresholdSlider
                width: parent.width
                minimum: -100
                maximum: 0
                step: 1
                unit: " dBm"
                value: root.thresholdValue
                enabled: root.bridge.available && root.deviceChecked
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
                Accessible.name: "Signal strength threshold"
                Accessible.description: "Lower values require the device to be closer before it counts as present"
            }
        }
    }
}
