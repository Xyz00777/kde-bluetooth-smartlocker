import QtQml
import org.kde.smartlocker 1.0

QtObject {
    property SmartLockerClient smartLockerClient: SmartLockerClient {}
    Component.onCompleted: Qt.quit()
}
