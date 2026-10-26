import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

TouchDialog {
    id: dialog
    UiMetrics { id: exportMetrics; pixelDensity: dialog.parent.Screen.pixelDensity; pixelRatio: dialog.parent.Screen.devicePixelRatio }
    required property var photoController
    objectName: "batchExportDialog"
    modal: true; focus: true; title: qsTr("export images")
    width: Math.min(760, parent.width - 32); height: Math.min(700, parent.height - 32)
    anchors.centerIn: parent
    property url folder
    property string scope: "selected"
    property alias focusColumn: exportColumn
    readonly property var focusViewport: exportScroll.contentItem
    readonly property int targetCount: scope === "current" ? (photoController.imageId > 0 ? 1 : 0)
        : photoController.images.filter(function(image) {
            return dialog.scope === "collection"
                || (dialog.scope === "selected" && (dialog.photoController.library.selected || []).indexOf(image.imgid) >= 0)
        }).length
    readonly property bool validPattern: pattern.valid
    readonly property string validationMessage: {
        if(photoController.busy) return qsTr("wait for the current operation to finish before exporting")
        if(targetCount === 0) return scope === "selected" ? qsTr("select images or choose another export scope")
            : scope === "current" ? qsTr("open a image or choose another export scope") : qsTr("the visible collection has no images to export")
        if(storage.currentIndex === 0 && !folder.toString().trim()) return qsTr("choose a destination folder before exporting")
        if(!maxWidth.acceptableInput || !maxHeight.acceptableInput) return qsTr("enter dimensions from 0 to 100000 in advanced options")
        if(!validPattern) return qsTr("enter a valid filename pattern in advanced options; use only {name}, {sequence}, or {id} tokens and no filename separators")
        return ""
    }
    FolderDialog { id: destination; title: qsTr("export destination"); onAccepted: dialog.folder = selectedFolder }
    contentItem: ColumnLayout {
        ScrollView {
            id: exportScroll
            Layout.fillWidth: true; Layout.fillHeight: true; contentWidth: availableWidth; clip: true
            ColumnLayout {
                id: exportColumn
                width: exportScroll.availableWidth; spacing: 10
                TouchComboBox {
                    id: scopeControl
                    objectName: "exportScope"
                    Accessible.name: qsTr("images to export")
                    Layout.fillWidth: true; implicitHeight: 64
                    model: [qsTr("selected images"), qsTr("visible collection"), qsTr("current image")]
                    currentIndex: dialog.scope === "current" ? 2 : dialog.scope === "collection" ? 1 : 0
                    onActivated: dialog.scope = ["selected", "collection", "current"][currentIndex]
                }
                Label {
                    text: dialog.targetCount === 1 ? qsTr("1 image to export") : qsTr("%1 images to export").arg(dialog.targetCount)
                    color: "#aebbc1"; Layout.fillWidth: true; wrapMode: Text.Wrap
                }
                Label { text: qsTr("destination"); color: "#00dbe5" }
                Label {
                    visible: storage.currentIndex !== 0
                    text: qsTr("exported subfolder beside each source")
                    color: "#e6eef0"; Layout.fillWidth: true; wrapMode: Text.Wrap
                }
                RowLayout {
                    visible: storage.currentIndex === 0; Layout.fillWidth: true
                    Label { text: dialog.folder.toString() || qsTr("choose a destination"); textFormat: Text.PlainText; Layout.fillWidth: true; elide: Text.ElideMiddle; color: "#e6eef0" }
                    TouchButton { text: qsTr("choose folder"); iconName: "folder"; showLabel: true; Layout.minimumWidth: 160; onClicked: destination.open() }
                }
                Label { text: qsTr("format options"); color: "#00dbe5" }
                RowLayout {
                    TouchComboBox { id: format; objectName: "exportFormat"; Accessible.name: qsTr("export format"); Layout.fillWidth: true; implicitHeight: 64; model: ["JPEG", "PNG", "TIFF"] }
                    TouchComboBox { id: bits; objectName: "exportBitDepth"; Accessible.name: qsTr("export bit depth"); enabled: format.currentIndex !== 0; Layout.fillWidth: true; implicitHeight: 64; model: [qsTr("8 bit"), qsTr("16 bit")] }
                }
                RowLayout {
                    visible: format.currentIndex === 0
                    Layout.fillWidth: true
                    Label { text: qsTr("quality"); color: "#e6eef0" }
                    Slider { objectName: "exportQualitySlider"; Accessible.name: qsTr("JPEG quality"); Layout.fillWidth: true; implicitHeight: exportMetrics.touchHeight; palette.highlight: "#00dbe5"; from: 1; to: 100; stepSize: 1; value: quality.value; onMoved: quality.value = Math.round(value) }
                    SpinBox { id: quality; objectName: "exportQuality"; from: 1; to: 100; value: 95; editable: true; implicitHeight: 64 }
                }
                Label { text: qsTr("name modifier"); color: "#00dbe5" }
                NamePatternEditor { id: pattern; objectName: "exportPattern"; Layout.fillWidth: true; modifiers: dialog.photoController.library.name_modifiers || [] }
                TouchButton {
                    id: advanced
                    objectName: "exportAdvanced"
                    text: checked ? qsTr("hide advanced options") : qsTr("show advanced options")
                    checkable: true; Layout.fillWidth: true
                }
                ColumnLayout {
                    visible: advanced.checked
                    Layout.fillWidth: true; spacing: 10
                    Label { text: qsTr("storage"); color: "#00dbe5" }
                    TouchComboBox { id: storage; objectName: "exportStorage"; Accessible.name: qsTr("export storage"); Layout.fillWidth: true; implicitHeight: 64; model: [qsTr("one destination folder"), qsTr("exported subfolder beside each source")] }
                    Label { text: qsTr("existing files"); color: "#00dbe5" }
                    TouchComboBox { id: collision; Accessible.name: qsTr("existing file handling"); Layout.fillWidth: true; implicitHeight: 64; model: [qsTr("use a unique filename if it exists"), qsTr("skip existing files"), qsTr("stop if a file exists")] }
                    Label { text: qsTr("resize and output profile"); color: "#00dbe5" }
                    Label { text: qsTr("maximum dimensions in pixels; 0 keeps the original size"); color: "#aebbc1"; Layout.fillWidth: true; wrapMode: Text.Wrap }
                    RowLayout {
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { objectName: "exportWidthLabel"; text: qsTr("width (pixels)"); color: "#aebbc1" }
                            TextField { id: maxWidth; objectName: "exportMaxWidth"; Layout.fillWidth: true; implicitHeight: 64; text: "0"; Accessible.name: qsTr("maximum width in pixels"); validator: IntValidator { bottom: 0; top: 100000 } }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { objectName: "exportHeightLabel"; text: qsTr("height (pixels)"); color: "#aebbc1" }
                            TextField { id: maxHeight; objectName: "exportMaxHeight"; Layout.fillWidth: true; implicitHeight: 64; text: "0"; Accessible.name: qsTr("maximum height in pixels"); validator: IntValidator { bottom: 0; top: 100000 } }
                        }
                    }
                    TouchSwitch { id: upscale; text: qsTr("allow upscaling"); implicitHeight: 64 }
                    TouchSwitch { id: highQuality; text: qsTr("high quality resampling"); checked: true; implicitHeight: 64 }
                    TouchComboBox { id: profile; Accessible.name: qsTr("export output profile"); Layout.fillWidth: true; implicitHeight: 64; model: [qsTr("sRGB output profile"), qsTr("image output profile")] }
                }
            }
        }
        Label {
            objectName: "exportValidation"
            text: dialog.validationMessage
            visible: text.length > 0
            color: "#ffd08a"; Layout.fillWidth: true; wrapMode: Text.Wrap
            Accessible.name: text
        }
        RowLayout {
            Layout.fillWidth: true
            TouchButton { text: qsTr("cancel"); Layout.minimumWidth: 96; onClicked: dialog.close() }
            Item { Layout.fillWidth: true }
            TouchButton {
                objectName: "exportStartButton"
                text: qsTr("start export"); iconName: "export"; showLabel: true; checked: true
                Layout.minimumWidth: 176
                enabled: dialog.validationMessage.length === 0
                onClicked: {
                    if(dialog.validationMessage.length > 0) return
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
