import QtQuick
import QtQuick.Layouts
import org.kde.plasma.plasmoid
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PC3

PlasmoidItem {
    id: root
    preferredRepresentation: fullRepresentation
    fullRepresentation: ColumnLayout {
        implicitWidth: 360
        implicitHeight: 300
        PC3.Label { text: "Taskbar Sync Companion"; font.bold: true }
        PC3.CheckBox {
            text: "Synchronize taskbars"
            checked: Plasmoid.syncing
            onToggled: Plasmoid.syncing = checked
        }
        PC3.Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: Plasmoid.discoveryError || (Plasmoid.taskbars.length + " stock Task Managers discovered")
        }
        Repeater {
            model: Plasmoid.taskbars
            PC3.Label {
                required property var modelData
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: "Screen " + modelData.screen + " · widget " + modelData.id + ": "
                    + (modelData.error || modelData.state) + " (" + modelData.sequence.length + " tasks)"
            }
        }
        PC3.Button { text: "Refresh discovery"; onClicked: Plasmoid.rescan() }
        Item { Layout.fillHeight: true }
    }
}
