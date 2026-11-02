pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.plasma.plasmoid
import org.kde.plasma.components as PlasmaComponents3
import org.kde.plasma.extras as PlasmaExtras
import org.kde.plasma.core as PlasmaCore
import org.kde.kirigami as Kirigami
import org.kde.smartlocker

PlasmoidItem {
    id: root

    readonly property bool inPanel: [
        PlasmaCore.Types.TopEdge,
        PlasmaCore.Types.RightEdge,
        PlasmaCore.Types.BottomEdge,
        PlasmaCore.Types.LeftEdge
    ].includes(Plasmoid.location)

    Plasmoid.title: "Bluetooth SmartLocker"
    Plasmoid.icon: "bluetooth"
    Plasmoid.status: root.panelStatus

    toolTipMainText: "Bluetooth SmartLocker"
    toolTipSubText: root.toolTipText

    SmartLockerClient {
        id: client
    }

    // Typed as var so Connections can watch the native client without a
    // generated .qmltypes file for SmartLockerClient.
    readonly property var clientRef: client

    // Single source of truth for every state-dependent string and panel status.
    // Colors are a separate binding so theme changes re-evaluate them.
    readonly property var statusMap: ({
        "starting": {
            label: "Starting",
            subtitle: "Waiting for the first Bluetooth reading.",
            panelStatus: PlasmaCore.Types.PassiveStatus
        },
        "monitoring": {
            label: "Monitoring",
            subtitle: "Watching devices. Locks after all leave.",
            panelStatus: PlasmaCore.Types.PassiveStatus
        },
        "away": {
            label: "Away",
            subtitle: "All watched devices are away. Locking after the away delay.",
            panelStatus: PlasmaCore.Types.ActiveStatus
        },
        "snoozed": {
            label: "Snoozed",
            subtitle: "Monitoring paused. It resumes automatically.",
            panelStatus: PlasmaCore.Types.ActiveStatus
        },
        "locked": {
            label: "Locked",
            subtitle: "Session locked. Bluetooth presence never unlocks.",
            panelStatus: PlasmaCore.Types.PassiveStatus
        },
        "disabled": {
            label: "Disabled",
            subtitle: "Monitoring is off. No locks will be triggered.",
            panelStatus: PlasmaCore.Types.PassiveStatus
        },
        "error": {
            label: "Error",
            subtitle: "The daemon reported an error. Check the service logs.",
            panelStatus: PlasmaCore.Types.NeedsAttentionStatus
        },
        "unavailable": {
            label: "Unavailable",
            subtitle: "The daemon is not running.",
            panelStatus: PlasmaCore.Types.NeedsAttentionStatus
        }
    })

    readonly property var statusInfo: root.statusMap[client.state] || root.statusMap.unavailable
    readonly property string statusText: root.statusInfo.label
    readonly property color statusColor: {
        switch (client.state) {
        case "monitoring":
            return Kirigami.Theme.positiveTextColor;
        case "away":
        case "snoozed":
            return Kirigami.Theme.neutralTextColor;
        case "locked":
            return Kirigami.Theme.highlightColor;
        case "disabled":
        case "starting":
            return Kirigami.Theme.disabledTextColor;
        case "error":
        case "unavailable":
            return Kirigami.Theme.negativeTextColor;
        default:
            return Kirigami.Theme.negativeTextColor;
        }
    }
    readonly property int panelStatus: root.statusInfo.panelStatus

    readonly property string statusSubtitle: {
        if (client.state === "monitoring") {
            const count = client.devices.length;
            return "Watching " + count + (count === 1 ? " device" : " devices") + ". Locks after all leave.";
        }
        return root.statusInfo.subtitle;
    }

    readonly property bool daemonAvailable: client.state !== "unavailable"
    readonly property bool canSnooze: root.daemonAvailable
        && (client.state === "monitoring" || client.state === "away")
        && client.snoozeSeconds > 0

    readonly property string toolTipText: {
        let text = root.statusText;
        if (client.devices.length > 0) {
            text += " · " + client.devices.length
                + (client.devices.length === 1 ? " watched device" : " watched devices");
        }
        if (client.lastError.length > 0) {
            text += "\n" + client.lastError;
        }
        return text;
    }

    readonly property string compactDescription: {
        let text = client.devices.length === 0
            ? "No watched devices. "
            : client.devices.length + (client.devices.length === 1 ? " watched device. " : " watched devices. ");
        return text + root.statusSubtitle;
    }

    compactRepresentation: Item {
        id: compactRoot

        readonly property real compactSize: Kirigami.Units.iconSizes.smallMedium
            + Kirigami.Units.smallSpacing * 2

        implicitWidth: compactSize
        implicitHeight: compactSize
        Layout.minimumWidth: compactSize
        Layout.minimumHeight: compactSize
        Layout.preferredWidth: compactSize
        Layout.preferredHeight: compactSize

        Kirigami.Icon {
            id: compactIcon

            anchors.centerIn: parent
            implicitWidth: Kirigami.Units.iconSizes.smallMedium
            implicitHeight: Kirigami.Units.iconSizes.smallMedium
            source: "bluetooth"
            fallback: "network-bluetooth-symbolic"
            Accessible.ignored: true
        }

        Rectangle {
            id: stateDot

            readonly property real dotSize: Math.max(
                Kirigami.Units.smallSpacing,
                Math.round(Kirigami.Units.gridUnit * 0.35)
            )

            anchors.right: compactIcon.right
            anchors.bottom: compactIcon.bottom
            anchors.rightMargin: -Math.round(width / 2)
            anchors.bottomMargin: -Math.round(height / 2)
            width: dotSize
            height: dotSize
            radius: width / 2
            color: root.statusColor
            border.width: 1
            border.color: Kirigami.Theme.backgroundColor
            Accessible.ignored: true
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: 2
            radius: Kirigami.Units.smallSpacing
            color: "transparent"
            border.width: 1
            border.color: Kirigami.Theme.highlightColor
            visible: compactMouseArea.activeFocus
            Accessible.ignored: true
        }

        MouseArea {
            id: compactMouseArea

            anchors.fill: parent
            activeFocusOnTab: true
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor

            Accessible.role: Accessible.Button
            Accessible.name: "Bluetooth SmartLocker: " + root.statusText
            Accessible.description: root.compactDescription

            onClicked: root.expanded = !root.expanded

            Keys.onPressed: event => {
                if (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    root.expanded = !root.expanded;
                    event.accepted = true;
                }
            }
        }
    }

    fullRepresentation: PlasmaExtras.Representation {
        id: fullRepresentation

        Plasmoid.backgroundHints: root.inPanel
            ? PlasmaCore.Types.NoBackground
            : PlasmaCore.Types.DefaultBackground

        property bool syncingMonitoringSwitch: false

        function syncMonitoringSwitch() {
            const shouldBeEnabled = client.state !== "disabled" && client.state !== "unavailable";
            if (monitoringSwitch.checked !== shouldBeEnabled) {
                fullRepresentation.syncingMonitoringSwitch = true;
                monitoringSwitch.checked = shouldBeEnabled;
                fullRepresentation.syncingMonitoringSwitch = false;
            }
        }

        Component.onCompleted: fullRepresentation.syncMonitoringSwitch()

        Layout.minimumWidth: Kirigami.Units.gridUnit * 18
        Layout.preferredWidth: Kirigami.Units.gridUnit * 20

        leftPadding: Kirigami.Units.largeSpacing
        rightPadding: Kirigami.Units.largeSpacing
        topPadding: Kirigami.Units.largeSpacing
        bottomPadding: Kirigami.Units.largeSpacing

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.largeSpacing

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.largeSpacing

                Kirigami.Icon {
                    Layout.alignment: Qt.AlignTop
                    implicitWidth: Kirigami.Units.iconSizes.medium
                    implicitHeight: Kirigami.Units.iconSizes.medium
                    source: "bluetooth"
                    fallback: "network-bluetooth"
                    Accessible.ignored: true
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        PlasmaExtras.Heading {
                            level: 3
                            Layout.fillWidth: true
                            text: "Bluetooth SmartLocker"
                            elide: Text.ElideRight
                            Accessible.name: "Bluetooth SmartLocker"
                        }

                        PlasmaComponents3.Label {
                            text: root.statusText
                            color: root.statusColor
                            font.bold: true
                            Accessible.name: "Status: " + root.statusText
                        }
                    }

                    PlasmaExtras.DescriptiveLabel {
                        Layout.fillWidth: true
                        text: root.statusSubtitle
                        wrapMode: Text.Wrap
                        Accessible.name: root.statusSubtitle
                    }
                }
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: client.lastError.length > 0
                type: Kirigami.MessageType.Error
                text: client.lastError
                Accessible.name: "Daemon error: " + client.lastError

                actions: [
                    Kirigami.Action {
                        text: "Dismiss"
                        icon.name: "edit-clear"
                        onTriggered: client.clearError()
                    }
                ]
            }

            PlasmaComponents3.Switch {
                id: monitoringSwitch

                Layout.fillWidth: true
                text: "Monitoring enabled"
                enabled: root.daemonAvailable
                Accessible.name: "Monitoring enabled"
                Accessible.description: "Turn Bluetooth presence monitoring on or off. When off, no locks will be triggered."

                onToggled: {
                    if (fullRepresentation.syncingMonitoringSwitch) {
                        return;
                    }
                    client.setEnabled(checked);
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                PlasmaExtras.Heading {
                    level: 4
                    text: "Watched devices"
                    Accessible.name: "Watched devices"
                }

                Item {
                    Layout.fillWidth: true
                }

                Rectangle {
                    Layout.preferredWidth: deviceCountLabel.implicitWidth + Kirigami.Units.smallSpacing * 2
                    Layout.preferredHeight: deviceCountLabel.implicitHeight + Kirigami.Units.smallSpacing
                    radius: height / 2
                    color: Kirigami.Theme.alternateBackgroundColor

                    PlasmaComponents3.Label {
                        id: deviceCountLabel

                        anchors.centerIn: parent
                        text: String(client.devices.length)
                        font: Kirigami.Theme.smallFont
                        color: Kirigami.Theme.textColor
                        Accessible.name: client.devices.length
                            + (client.devices.length === 1 ? " watched device" : " watched devices")
                    }
                }
            }

            PlasmaExtras.PlaceholderMessage {
                Layout.fillWidth: true
                Layout.topMargin: Kirigami.Units.smallSpacing
                Layout.bottomMargin: Kirigami.Units.smallSpacing
                visible: client.devices.length === 0 && root.daemonAvailable
                iconName: "bluetooth"
                text: "No paired or trusted Bluetooth devices"
                explanation: "Pair or trust a device in System Settings → Bluetooth. With no configured devices, every paired or trusted device is watched."
            }

            PlasmaComponents3.ScrollView {
                id: deviceScroll

                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(devicesColumn.implicitHeight, 320)
                visible: client.devices.length > 0
                contentWidth: availableWidth
                clip: true
                QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff
                QQC2.ScrollBar.vertical.policy: QQC2.ScrollBar.AsNeeded

                ColumnLayout {
                    id: devicesColumn

                    width: deviceScroll.availableWidth
                    spacing: Kirigami.Units.smallSpacing

                    Repeater {
                        model: client.devices

                        delegate: DevicePolicyRow {
                            required property string modelData

                            path: modelData
                            daemonClient: client
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                PlasmaComponents3.Button {
                    visible: client.snoozeSeconds > 0
                    enabled: root.canSnooze
                    text: client.state === "snoozed" ? "Snoozed" : "Snooze " + client.snoozeSeconds + " s"
                    icon.name: "chronometer"
                    Accessible.name: client.state === "snoozed"
                        ? "Monitoring snoozed for " + client.snoozeSeconds + " seconds"
                        : "Snooze monitoring for " + client.snoozeSeconds + " seconds"
                    Accessible.description: "Pause automatic locking. Monitoring resumes automatically when the snooze ends."
                    onClicked: client.snooze(client.snoozeSeconds)
                }

                Item {
                    Layout.fillWidth: true
                }

                PlasmaComponents3.Button {
                    text: "Refresh"
                    icon.name: "view-refresh"
                    Accessible.name: "Refresh status and devices"
                    Accessible.description: "Ask the daemon for its current state and watched device list."
                    onClicked: client.refresh()
                }
            }

            PlasmaExtras.DescriptiveLabel {
                Layout.fillWidth: true
                text: "Lock-only · Bluetooth presence never unlocks."
                font: Kirigami.Theme.smallFont
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                Accessible.name: text
            }
        }

        Connections {
            target: root.clientRef

            function onStateChanged() {
                fullRepresentation.syncMonitoringSwitch();
            }

            function onSettingsChanged() {
                fullRepresentation.syncMonitoringSwitch();
            }

            function onErrorOccurred() {
                // A rejected mutation leaves the daemon state unchanged, so
                // resync the switch from daemon truth instead of leaving the
                // user's optimistic toggle in place.
                fullRepresentation.syncMonitoringSwitch();
            }
        }
    }
}
