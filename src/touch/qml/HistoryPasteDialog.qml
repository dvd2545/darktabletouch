import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

TouchDialog {
    id: dialog
    required property var photoController
    objectName: "historyPasteDialog"
    property int imageId: 0
    property string scope: "current"
    property var choices: []
    property alias focusColumn: optionsColumn
    readonly property var focusViewport: optionsScroll.contentItem
    readonly property int selectedCount: choices.filter(function(step) { return step.selected }).length
    readonly property int destinationCount: photoController.images.filter(function(image) {
        if(image.imgid === photoController.copiedHistory.source) return false
        if(scope === "current") return image.imgid === (imageId || photoController.imageId)
        return scope === "collection" || (photoController.library.selected || []).indexOf(image.imgid) >= 0
    }).length
    title: qsTr("paste selected history")
    modal: true; focus: true; anchors.centerIn: parent
    width: Math.min(760, parent.width - 32); height: Math.min(700, parent.height - 32)
    function selectSteps(mode) {
        choices = choices.map(function(step) {
            const copy = Object.assign({}, step)
            copy.selected = mode === "all" || (mode === "safe" && step.safe && step.latest)
            return copy
        })
    }
    function updateStep(index, name, value) {
        const next = choices.slice()
        next[index] = Object.assign({}, next[index])
        next[index][name] = value
        choices = next
    }
    function settings() {
        return {scope: scope, steps: choices.filter(function(step) { return step.selected }).map(function(step) {
            return {index: step.index, auto_init: step.auto_init}
        }), merge: mode.currentIndex === 0, copy_order: copyOrder.checked}
    }
    onOpened: {
        choices = (photoController.copiedHistory.steps || []).filter(function(step) { return step.selectable }).map(function(step) {
            return Object.assign({}, step, {selected: step.safe && step.latest, auto_init: false})
        })
    }
    contentItem: ColumnLayout {
        spacing: 10
        ScrollView {
            id: optionsScroll
            Layout.fillWidth: true; Layout.fillHeight: true; contentWidth: availableWidth; clip: true
            ColumnLayout {
                id: optionsColumn
                width: optionsScroll.availableWidth; spacing: 10
        Label {
            text: qsTr("source: %1").arg(dialog.photoController.copiedHistory.filename || "")
            textFormat: Text.PlainText; color: "#e6eef0"; Layout.fillWidth: true; elide: Text.ElideMiddle
        }
        TouchComboBox {
            objectName: "historyScopeControl"
            Layout.fillWidth: true; implicitHeight: 64
            model: [qsTr("current photo"), qsTr("selected photos"), qsTr("visible collection")]
            currentIndex: ["current", "selected", "collection"].indexOf(dialog.scope)
            onActivated: dialog.scope = ["current", "selected", "collection"][currentIndex]
        }
        Label { text: qsTr("%1 destination photos; the copied source is excluded").arg(dialog.destinationCount); color: "#aebbc1"; Layout.fillWidth: true; wrapMode: Text.Wrap }
        RowLayout {
            Layout.fillWidth: true
            TouchButton { text: qsTr("safe defaults"); Layout.fillWidth: true; onClicked: dialog.selectSteps("safe") }
            TouchButton { text: qsTr("all"); Layout.fillWidth: true; onClicked: dialog.selectSteps("all") }
            TouchButton { text: qsTr("none"); Layout.fillWidth: true; onClicked: dialog.selectSteps("none") }
        }
        Label { text: qsTr("%1 module states selected; each instance shows its latest state. target defaults reinitialize the checked module for each destination").arg(dialog.selectedCount); wrapMode: Text.Wrap; color: "#aebbc1"; Layout.fillWidth: true }
        ListView {
            id: stepList
            objectName: "pasteHistorySteps"
            Layout.fillWidth: true; Layout.preferredHeight: 240; clip: true; spacing: 6
            model: dialog.choices
            delegate: RowLayout {
                required property var modelData
                required property int index
                width: stepList.width; height: Math.max(64, stepCheck.implicitHeight)
                CheckBox {
                    id: stepCheck
                    Layout.fillWidth: true; implicitHeight: 64
                    text: modelData.index + "  " + modelData.label + (modelData.enabled ? "" : qsTr(" (disabled)")) + (modelData.safe ? "" : qsTr(" (advanced)"))
                    checked: modelData.selected
                    onToggled: dialog.updateStep(index, "selected", checked)
                    contentItem: Label { text: stepCheck.text; textFormat: Text.PlainText; leftPadding: stepCheck.indicator.width + stepCheck.spacing; color: "#e6eef0"; verticalAlignment: Text.AlignVCenter; wrapMode: Text.Wrap }
                }
                CheckBox {
                    text: qsTr("target defaults"); implicitHeight: 64
                    enabled: modelData.selected && modelData.index > 1
                    checked: modelData.auto_init
                    onToggled: dialog.updateStep(index, "auto_init", checked)
                }
            }
            ScrollBar.vertical: ScrollBar { }
        }
        TouchComboBox {
            id: mode
            Layout.fillWidth: true; implicitHeight: 64
            model: [qsTr("merge into destination history"), qsTr("replace destination history")]
        }
        CheckBox { id: copyOrder; text: qsTr("copy processing order"); implicitHeight: 56 }
        Label { text: qsTr("the copied source is unchanged; undo restores destination adjustments in this session"); wrapMode: Text.Wrap; color: "#aebbc1"; Layout.fillWidth: true }
            }
        }
        TouchButton {
            objectName: "pasteHistoryButton"
            text: qsTr("paste %1 selected steps").arg(dialog.selectedCount); iconName: "paste"; showLabel: true
            Layout.fillWidth: true; checked: true
            enabled: dialog.selectedCount > 0 && dialog.destinationCount > 0 && !dialog.photoController.busy
            onClicked: { dialog.photoController.historyCommand("paste", dialog.settings(), dialog.imageId); dialog.close() }
        }
    }
}
