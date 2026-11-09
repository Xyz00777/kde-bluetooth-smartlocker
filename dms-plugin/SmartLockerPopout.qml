pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import qs.Common
import qs.Widgets
import qs.Modules.Plugins

PopoutComponent {
    id: root

    required property var bridge
    property color stateAccent: Theme.surfaceVariantText
    property string stateLabel: "Starting"
    property string stateDescription: "Waiting for the first Bluetooth reading."
    property string stateIcon: "bluetooth"
    property bool showThresholds: true

    property bool monitoringChecked: false
    property bool monitoringPending: false
    property bool monitoringDesired: false

    readonly property int deviceCount: Array.isArray(root.bridge.devices) ? root.bridge.devices.length : 0

    headerText: "Bluetooth SmartLocker"
    detailsText: root.bridge.available ? ((root.stateLabel || "Starting") + " · " + root.deviceCount + (root.deviceCount === 1 ? " device" : " devices")) : "Daemon offline"
    showCloseButton: true

    headerActions: Component {
        DankActionButton {
            iconName: "refresh"
            tooltipText: "Refresh status"
            Accessible.name: tooltipText
            onClicked: root.bridge.refresh()
        }
    }

    function syncMonitoring() {
        if (!root.bridge.available) {
            root.monitoringPending = false;
            root.monitoringChecked = false;
            return;
        }
        const actual = root.bridge.stateName !== "disabled";
        if (root.monitoringPending) {
            if (actual === root.monitoringDesired) {
                root.monitoringPending = false;
                root.monitoringChecked = actual;
            }
            return;
        }
        root.monitoringChecked = actual;
    }

    Component.onCompleted: syncMonitoring()

    Connections {
        target: root.bridge

        function onChanged() {
            root.syncMonitoring();
        }
    }

    Timer {
        id: monitoringPendingTimer
        interval: 2000
        repeat: false
        onTriggered: {
            root.monitoringPending = false;
            root.syncMonitoring();
        }
    }

    Item {
        id: body
        width: parent.width
        implicitHeight: bodyColumn.implicitHeight
        height: implicitHeight

        Column {
            id: bodyColumn
            width: parent.width
            spacing: Theme.spacingS

            StyledRect {
                id: errorBanner
                width: parent.width
                visible: (root.bridge.lastError || "").length > 0
                implicitHeight: errorRow.implicitHeight + Theme.spacingM * 2
                height: implicitHeight
                radius: Theme.cornerRadius
                color: Theme.withAlpha(Theme.error, 0.14)

                RowLayout {
                    id: errorRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: Theme.spacingM
                    anchors.rightMargin: Theme.spacingM
                    spacing: Theme.spacingS

                    DankIcon {
                        name: "error"
                        size: Theme.iconSize - 4
                        color: Theme.error
                        Layout.alignment: Qt.AlignTop
                    }

                    StyledText {
                        Layout.fillWidth: true
                        text: root.bridge.lastError
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.surfaceText
                        wrapMode: Text.WordWrap
                    }

                    DankActionButton {
                        iconName: "close"
                        buttonSize: 28
                        iconSize: 16
                        tooltipText: "Dismiss error"
                        Accessible.name: tooltipText
                        Layout.alignment: Qt.AlignVCenter
                        onClicked: root.bridge.clearError()
                    }
                }
            }

            StyledRect {
                width: parent.width
                implicitHeight: heroRow.implicitHeight + Theme.spacingM * 2
                height: implicitHeight
                radius: Theme.cornerRadius
                color: Theme.surfaceContainerHigh

                RowLayout {
                    id: heroRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: Theme.spacingM
                    anchors.rightMargin: Theme.spacingM
                    spacing: Theme.spacingM

                    StyledRect {
                        Layout.preferredWidth: 44
                        Layout.preferredHeight: 44
                        Layout.alignment: Qt.AlignTop
                        radius: 22
                        color: Theme.withAlpha(root.stateAccent, 0.16)

                        DankIcon {
                            anchors.centerIn: parent
                            name: root.stateIcon || "bluetooth"
                            size: Theme.iconSize
                            color: root.stateAccent
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingXXS

                        StyledText {
                            Layout.fillWidth: true
                            text: root.stateLabel
                            font.pixelSize: Theme.fontSizeLarge
                            font.weight: Font.Bold
                            color: Theme.surfaceText
                            wrapMode: Text.NoWrap
                            elide: Text.ElideRight
                        }

                        StyledText {
                            Layout.fillWidth: true
                            text: root.stateDescription
                            font.pixelSize: Theme.fontSizeSmall
                            color: Theme.surfaceVariantText
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingXS

                            DankIcon {
                                name: "lock"
                                size: 12
                                color: Theme.primary
                            }

                            StyledText {
                                Layout.fillWidth: true
                                text: "Lock-only · presence never unlocks"
                                font.pixelSize: Theme.fontSizeSmall
                                color: Theme.surfaceVariantText
                                wrapMode: Text.NoWrap
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }

            StyledRect {
                width: parent.width
                implicitHeight: monitoringColumn.implicitHeight + Theme.spacingM * 2
                height: implicitHeight
                radius: Theme.cornerRadius
                color: Theme.surfaceContainerHigh

                Column {
                    id: monitoringColumn
                    width: parent.width - Theme.spacingM * 2
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: Theme.spacingM
                    spacing: Theme.spacingS

                    DankToggle {
                        id: monitoringToggle
                        width: parent.width
                        text: "Monitoring"
                        description: "Lock the session when every watched device is away"
                        checked: root.monitoringChecked
                        enabled: root.bridge.available
                        onToggled: isChecked => {
                            root.monitoringChecked = isChecked;
                            root.monitoringDesired = isChecked;
                            root.monitoringPending = true;
                            root.bridge.setEnabled(isChecked);
                            monitoringPendingTimer.restart();
                        }
                        Accessible.name: "Toggle Bluetooth presence monitoring"
                        Accessible.description: "When enabled, the session locks after every watched device has been away for the configured delay"
                    }

                    DankButton {
                        width: parent.width
                        iconName: "snooze"
                        text: root.bridge.stateName === "snoozed" ? "Snoozed" : ("Snooze " + root.bridge.snoozeSeconds + " s")
                        visible: root.bridge.snoozeSeconds > 0
                        enabled: root.bridge.available && root.bridge.monitoring && root.bridge.stateName !== "snoozed"
                        onClicked: root.bridge.snooze(root.bridge.snoozeSeconds)
                    }
                }
            }

            RowLayout {
                width: parent.width
                spacing: Theme.spacingS

                StyledText {
                    text: "Watched devices"
                    font.pixelSize: Theme.fontSizeMedium
                    font.weight: Font.Bold
                    color: Theme.surfaceText
                }

                Item {
                    Layout.fillWidth: true
                }

                StyledRect {
                    implicitWidth: deviceCountText.implicitWidth + Theme.spacingS * 2
                    implicitHeight: deviceCountText.implicitHeight + Theme.spacingXS
                    radius: height / 2
                    color: Theme.surfaceContainerHighest

                    StyledText {
                        id: deviceCountText
                        anchors.centerIn: parent
                        text: String(root.deviceCount)
                        font.pixelSize: Theme.fontSizeSmall
                        font.weight: Font.Medium
                        color: Theme.surfaceVariantText
                    }
                }
            }

            StyledRect {
                width: parent.width
                visible: root.bridge.available && root.deviceCount === 0
                implicitHeight: emptyColumn.implicitHeight + Theme.spacingL * 2
                height: implicitHeight
                radius: Theme.cornerRadius
                color: Theme.surfaceContainerHigh

                Column {
                    id: emptyColumn
                    width: parent.width - Theme.spacingL * 2
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacingS

                    DankIcon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        name: "bluetooth_searching"
                        size: Theme.iconSize
                        color: Theme.primary
                    }

                    StyledText {
                        width: parent.width
                        text: "No paired or trusted Bluetooth devices"
                        font.pixelSize: Theme.fontSizeMedium
                        font.weight: Font.Medium
                        color: Theme.surfaceText
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                    }

                    StyledText {
                        width: parent.width
                        text: "Pair or trust a device in Bluetooth settings. With no configured devices, every paired or trusted device is watched."
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.surfaceVariantText
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                    }
                }
            }

            DankFlickable {
                id: deviceFlickable
                width: parent.width
                height: root.deviceCount === 0 ? 0 : Math.min(deviceColumn.implicitHeight, 360)
                visible: root.deviceCount > 0
                contentWidth: width
                contentHeight: deviceColumn.implicitHeight
                clip: true

                Column {
                    id: deviceColumn
                    width: deviceFlickable.width
                    spacing: Theme.spacingS

                    Repeater {
                        model: root.bridge.devices || []

                        delegate: SmartLockerDeviceRow {
                            required property string modelData
                            address: modelData
                            bridge: root.bridge
                            showThreshold: root.showThresholds
                        }
                    }
                }
            }

            StyledText {
                width: parent.width
                text: "Away delay and minimum present devices are configured on the daemon; per-device scan modes and both signal thresholds are controlled above."
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.surfaceVariantText
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }
        }
    }
}
