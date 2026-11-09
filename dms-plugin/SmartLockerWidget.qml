pragma ComponentBehavior: Bound

import QtQuick
import qs.Common
import qs.Services
import qs.Widgets
import qs.Modules.Plugins

PluginComponent {
    id: root

    layerNamespacePlugin: "bluetooth-smartlocker"
    popoutWidth: 420

    SmartLockerBridge {
        id: bridge
        refreshIntervalSec: Math.max(2, root.pluginData.refreshIntervalSec || 2)
    }

    readonly property int deviceCount: Array.isArray(bridge.devices) ? bridge.devices.length : 0
    readonly property var bridgeClient: bridge
    readonly property string stateName: bridge.stateName || "starting"
    readonly property color stateAccent: accentFor(stateName)
    readonly property string stateLabel: labelFor(stateName)
    readonly property string stateShortLabel: shortLabelFor(stateName)
    readonly property string stateDescription: descriptionFor(stateName)
    readonly property string stateIcon: iconFor(stateName)
    readonly property bool alertState: stateName === "away" || stateName === "snoozed" || stateName === "error" || stateName === "locked"
    readonly property bool showLabel: pluginData.showLabel !== false

    property string lastErrorShown: ""

    function accentFor(state) {
        switch (state) {
        case "monitoring":
            return Theme.success;
        case "away":
        case "snoozed":
            return Theme.warning;
        case "locked":
            return Theme.primary;
        case "error":
            return Theme.error;
        default:
            return Theme.surfaceVariantText;
        }
    }

    function labelFor(state) {
        switch (state) {
        case "monitoring":
            return "Monitoring";
        case "away":
            return "Away";
        case "snoozed":
            return "Snoozed";
        case "locked":
            return "Locked";
        case "disabled":
            return "Disabled";
        case "error":
            return "Error";
        case "starting":
            return "Starting";
        case "unavailable":
            return "Daemon offline";
        default:
            return "Starting";
        }
    }

    function shortLabelFor(state) {
        switch (state) {
        case "monitoring":
            return "On";
        case "away":
            return "Away";
        case "snoozed":
            return "Paused";
        case "locked":
            return "Locked";
        case "disabled":
            return "Off";
        case "error":
            return "Error";
        case "starting":
            return "…";
        case "unavailable":
            return "Offline";
        default:
            return "…";
        }
    }

    function descriptionFor(state) {
        switch (state) {
        case "monitoring":
            return "Watching " + deviceCount + (deviceCount === 1 ? " device." : " devices.") + " Locks after all leave.";
        case "away":
            return "All watched devices are away. Locking after the away delay.";
        case "snoozed":
            return "Monitoring paused. It resumes automatically.";
        case "locked":
            return "Session locked. Bluetooth presence never unlocks.";
        case "disabled":
            return "Monitoring is off. No locks will be triggered.";
        case "starting":
            return "Waiting for the first Bluetooth reading.";
        case "error":
            return "The daemon reported an error. Check the service logs.";
        case "unavailable":
            return "The SmartLocker daemon is not running.";
        default:
            return "Waiting for the first Bluetooth reading.";
        }
    }

    function iconFor(state) {
        switch (state) {
        case "monitoring":
            return "bluetooth_connected";
        case "away":
            return "bluetooth_disabled";
        case "snoozed":
            return "snooze";
        case "locked":
            return "lock";
        case "disabled":
            return "bluetooth_disabled";
        case "error":
            return "error";
        case "starting":
            return "hourglass_top";
        case "unavailable":
            return "link_off";
        default:
            return "bluetooth";
        }
    }

    function checkErrorToast() {
        const errorText = (bridge.lastError || "").toString();
        if (!errorText) {
            lastErrorShown = "";
            return;
        }
        if (errorText === lastErrorShown)
            return;
        lastErrorShown = errorText;
        ToastService.showError("Bluetooth SmartLocker", errorText);
    }

    Component.onCompleted: {
        bridge.refresh();
        checkErrorToast();
    }

    Connections {
        target: bridge

        function onChanged() {
            root.checkErrorToast();
        }

        function onLastErrorChanged() {
            root.checkErrorToast();
        }
    }

    horizontalBarPill: Component {
        StyledRect {
            Accessible.role: Accessible.Button
            Accessible.name: "Bluetooth SmartLocker, " + root.stateLabel
            Accessible.description: root.stateDescription
            Accessible.onPressAction: root.triggerPopout()

            implicitWidth: pillRow.implicitWidth + Theme.spacingS * 2
            implicitHeight: Math.max(root.iconSize - 6, pillRow.implicitHeight) + Theme.spacingXS * 2
            width: implicitWidth
            height: implicitHeight
            radius: height / 2
            color: root.alertState ? Theme.withAlpha(root.stateAccent, 0.14) : "transparent"

            Row {
                id: pillRow
                anchors.centerIn: parent
                spacing: Theme.spacingXS

                Item {
                    width: root.iconSize - 6
                    height: root.iconSize - 6
                    anchors.verticalCenter: parent.verticalCenter

                    DankIcon {
                        anchors.centerIn: parent
                        name: "bluetooth"
                        size: root.iconSize - 6
                        color: root.stateAccent
                    }

                    StyledRect {
                        width: 6
                        height: 6
                        radius: 3
                        color: root.stateAccent
                        border.width: 1
                        border.color: Theme.surface
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.rightMargin: -1
                        anchors.bottomMargin: -1
                    }
                }

                StyledText {
                    visible: root.showLabel
                    text: root.stateShortLabel
                    font.pixelSize: Theme.fontSizeSmall
                    font.weight: Font.Medium
                    color: root.stateAccent
                    wrapMode: Text.NoWrap
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }

    verticalBarPill: Component {
        StyledRect {
            Accessible.role: Accessible.Button
            Accessible.name: "Bluetooth SmartLocker, " + root.stateLabel
            Accessible.description: root.stateDescription
            Accessible.onPressAction: root.triggerPopout()

            implicitWidth: root.widgetThickness
            implicitHeight: pillColumn.implicitHeight + Theme.spacingXS * 2
            width: implicitWidth
            height: implicitHeight
            radius: height / 2
            color: root.alertState ? Theme.withAlpha(root.stateAccent, 0.14) : "transparent"

            Column {
                id: pillColumn
                width: parent.width
                anchors.centerIn: parent
                spacing: 1

                Item {
                    width: root.iconSize - 6
                    height: root.iconSize - 6
                    anchors.horizontalCenter: parent.horizontalCenter

                    DankIcon {
                        anchors.centerIn: parent
                        name: "bluetooth"
                        size: root.iconSize - 6
                        color: root.stateAccent
                    }

                    StyledRect {
                        width: 6
                        height: 6
                        radius: 3
                        color: root.stateAccent
                        border.width: 1
                        border.color: Theme.surface
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.rightMargin: -1
                        anchors.bottomMargin: -1
                    }
                }

                StyledText {
                    visible: root.showLabel
                    width: parent.width
                    text: root.stateShortLabel
                    font.pixelSize: Theme.fontSizeSmall
                    font.weight: Font.Medium
                    color: root.stateAccent
                    wrapMode: Text.NoWrap
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                }
            }
        }
    }

    pillRightClickAction: () => bridge.refresh()

    popoutContent: Component {
        SmartLockerPopout {
            bridge: root.bridgeClient
            stateAccent: root.stateAccent
            stateLabel: root.stateLabel
            stateDescription: root.stateDescription
            stateIcon: root.stateIcon
            showThresholds: root.pluginData.showThresholds !== false
        }
    }
}
