import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

TouchDialog {
    id: dialog
    required property var photoController
    objectName: "batchExportDialog"
    modal: true; focus: true; title: qsTr("export photos")
    width: Math.min(760, parent.width - 32); height: Math.min(700, parent.height - 32)
    anchors.centerIn: parent
    property url folder
    property string scope: "selected"
    background: Rectangle { color: "#121a1e"; radius: 16; border.color: "#334249" }
    FolderDialog { id: destination; title: qsTr("export destination"); onAccepted: dialog.folder = selectedFolder }
    contentItem: ColumnLayout {
        ScrollView {
            id: exportScroll
            Layout.fillWidth: true; Layout.fillHeight: true; contentWidth: availableWidth; clip: true
            ColumnLayout {
                width: exportScroll.availableWidth; spacing: 10
                Label { text: qsTr("photos"); color: "#00dbe5" }
                TouchComboBox {
                    id: scopeControl
                    Layout.fillWidth: true; implicitHeight: 64
                    model: [qsTr("selected photos"), qsTr("visible collection"), qsTr("current photo")]
                    currentIndex: dialog.scope === "current" ? 2 : dialog.scope === "collection" ? 1 : 0
                    onActivated: dialog.scope = ["selected", "collection", "current"][currentIndex]
                }
                Label { text: qsTr("storage"); color: "#00dbe5" }
                TouchComboBox { id: storage; Layout.fillWidth: true; implicitHeight: 64; model: [qsTr("one destination folder"), qsTr("exported subfolder beside each source")] }
                RowLayout {
                    visible: storage.currentIndex === 0; Layout.fillWidth: true
                    Label { text: dialog.folder.toString() || qsTr("choose a destination"); textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#e6eef0" }
                    TouchButton { text: qsTr("choose folder"); iconName: "folder"; onClicked: destination.open() }
                }
                TouchComboBox { id: collision; Layout.fillWidth: true; implicitHeight: 64; model: [qsTr("use a unique filename if it exists"), qsTr("skip existing files"), qsTr("stop if a file exists")] }
                Label { text: qsTr("name modifier"); color: "#00dbe5" }
                TextField { id: pattern; Layout.fillWidth: true; implicitHeight: 64; text: "{name}_edited"; selectByMouse: true; Accessible.name: qsTr("export name pattern") }
                Label { text: qsTr("tokens: {name}, {sequence}, {id}; original filenames remain unchanged"); color: "#aebbc1"; Layout.fillWidth: true; wrapMode: Text.Wrap }
                Label { text: qsTr("format options"); color: "#00dbe5" }
                RowLayout {
                    TouchComboBox { id: format; Layout.fillWidth: true; implicitHeight: 64; model: ["JPEG", "PNG", "TIFF"] }
                    TouchComboBox { id: bits; enabled: format.currentIndex !== 0; Layout.fillWidth: true; implicitHeight: 64; model: [qsTr("8 bit"), qsTr("16 bit")] }
                }
                RowLayout {
                    visible: format.currentIndex === 0
                    Label { text: qsTr("quality"); color: "#e6eef0" }
                    SpinBox { id: quality; from: 1; to: 100; value: 95; editable: true; implicitHeight: 64 }
                }
                Label { text: qsTr("global options"); color: "#00dbe5" }
                Label { text: qsTr("maximum dimensions in pixels; 0 keeps the original size"); color: "#aebbc1" }
                RowLayout {
                    TextField { id: maxWidth; Layout.fillWidth: true; implicitHeight: 64; text: "0"; placeholderText: qsTr("width"); Accessible.name: qsTr("maximum width"); validator: IntValidator { bottom: 0; top: 100000 } }
                    TextField { id: maxHeight; Layout.fillWidth: true; implicitHeight: 64; text: "0"; placeholderText: qsTr("height"); Accessible.name: qsTr("maximum height"); validator: IntValidator { bottom: 0; top: 100000 } }
                }
                Switch { id: upscale; text: qsTr("allow upscaling"); implicitHeight: 64 }
                Switch { id: highQuality; text: qsTr("high quality resampling"); checked: true; implicitHeight: 64 }
                TouchComboBox { id: profile; Layout.fillWidth: true; implicitHeight: 64; model: [qsTr("sRGB output profile"), qsTr("image output profile")] }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            TouchButton { text: qsTr("cancel"); iconName: "close"; onClicked: dialog.close() }
            Item { Layout.fillWidth: true }
            TouchButton {
                text: qsTr("start export"); iconName: "export"; checked: true
                enabled: !photoController.busy && maxWidth.acceptableInput && maxHeight.acceptableInput && !!pattern.text && (storage.currentIndex !== 0 || !!dialog.folder.toString())
                onClicked: {
                    photoController.exportBatch({scope: dialog.scope, folder: dialog.folder, storage: storage.currentIndex ? "source" : "folder",
                        collision: ["unique", "skip", "error"][collision.currentIndex], pattern: pattern.text,
                        format: ["jpeg", "png", "tiff"][format.currentIndex], bits: bits.currentIndex ? 16 : 8, quality: quality.value,
                        width: Number(maxWidth.text), height: Number(maxHeight.text), upscale: upscale.checked,
                        high_quality: highQuality.checked, srgb: profile.currentIndex === 0})
                    dialog.close()
                }
            }
        }
    }
}
