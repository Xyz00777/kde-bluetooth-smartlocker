import QtQuick
import qs.Common
import qs.Modules.Plugins
import qs.Widgets

PluginSettings {
    id: root
    pluginId: "bluetoothSmartLocker"

    StyledText {
        width: parent.width
        text: "Bluetooth SmartLocker"
        font.pixelSize: Theme.fontSizeLarge
        font.weight: Font.Bold
        color: Theme.surfaceText
    }

    StyledText {
        width: parent.width
        text: "Watches configured Bluetooth devices and locks the session only after every watched device has been away for the daemon's away delay. Bluetooth presence never unlocks the session."
        font.pixelSize: Theme.fontSizeSmall
        color: Theme.surfaceVariantText
        wrapMode: Text.WordWrap
    }

    ToggleSetting {
        settingKey: "showLabel"
        label: "Show status in the bar"
        description: "Show the short state label next to the Bluetooth icon."
        defaultValue: true
    }

    ToggleSetting {
        settingKey: "showThresholds"
        label: "Show RSSI threshold controls"
        description: "Show the per-device signal threshold slider in the popout."
        defaultValue: true
    }

    SliderSetting {
        settingKey: "refreshIntervalSec"
        label: "Refresh interval"
        description: "How often the widget polls the daemon for state changes."
        defaultValue: 5
        minimum: 2
        maximum: 30
        unit: "s"
    }

    StyledText {
        width: parent.width
        text: "This widget is lock-only: Bluetooth presence never unlocks the session. Away delay, minimum present devices and RSSI policy are configured on the daemon (NixOS: services.kdeBluetoothSmartlocker)."
        font.pixelSize: Theme.fontSizeSmall
        color: Theme.surfaceVariantText
        wrapMode: Text.WordWrap
    }

    StyledText {
        width: parent.width
        text: "Enable this plugin in DMS Settings → Plugins, then add it to a DankBar."
        font.pixelSize: Theme.fontSizeSmall
        color: Theme.surfaceVariantText
        wrapMode: Text.WordWrap
    }
}
