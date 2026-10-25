import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

TouchDialog {
    id: dialog
    required property var photoController
    property var chosen: ({})
    readonly property int selectedCount: Object.keys(chosen).filter(function(key) { return chosen[key] }).length
    modal: true
    focus: true
    title: qsTr("add to library")
    anchors.centerIn: parent
    width: Math.min(800, parent.width - 32)
    height: Math.min(650, parent.height - 32)
    padding: 16
    background: Rectangle { color: "#121a1e"; radius: 16; border.color: "#334249" }
    function selectAll(value) {
        const next = {}
        for(const item of photoController.importCandidates) if(!item.existing) next[item.path] = value
        chosen = next
    }
    function toggle(path) { const next = Object.assign({}, chosen); next[path] = !next[path]; chosen = next }
    onOpened: selectAll(true)
    contentItem: ColumnLayout {
        spacing: 10
        Label {
            Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#aebbc1"
            text: qsTr("choose photos to add at their current location; existing catalog entries are skipped")
        }
        RowLayout {
            TouchButton { text: qsTr("select all new"); onClicked: dialog.selectAll(true) }
            TouchButton { text: qsTr("select none"); onClicked: dialog.selectAll(false) }
            Label { Layout.fillWidth: true; horizontalAlignment: Text.AlignRight; text: qsTr("%1 selected").arg(dialog.selectedCount); color: "#e6eef0" }
        }
        ListView {
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 8
            model: dialog.photoController.importCandidates
            delegate: TouchButton {
                required property var modelData
                required property int index
                width: ListView.view.width; implicitHeight: Math.max(84, contentItem.implicitHeight + 28)
                text: (modelData.existing ? qsTr("already in library") : dialog.chosen[modelData.path] ? "\u2713" : "\u25a1") + "  " + modelData.path
                checked: !!dialog.chosen[modelData.path]; enabled: !modelData.existing
                onClicked: dialog.toggle(modelData.path)
                onActiveFocusChanged: if(activeFocus) { ListView.view.currentIndex = index; ListView.view.positionViewAtIndex(index, ListView.Contain) }
                Keys.onPressed: function(event) {
                    const direction = event.key === Qt.Key_Tab || event.key === Qt.Key_Down ? 1 : event.key === Qt.Key_Backtab || event.key === Qt.Key_Up ? -1 : 0
                    let next = index + direction
                    while(direction && next >= 0 && next < dialog.photoController.importCandidates.length && dialog.photoController.importCandidates[next].existing) next += direction
                    if(direction && next >= 0 && next < dialog.photoController.importCandidates.length) {
                        event.accepted = true
                        const view = ListView.view
                        view.currentIndex = next
                        view.positionViewAtIndex(next, ListView.Contain)
                        Qt.callLater(function() { const item = view.itemAtIndex(next); if(item) item.forceActiveFocus() })
                    }
                }
            }
            ScrollBar.vertical: ScrollBar { }
            Label { visible: !dialog.photoController.importCandidates.length; anchors.centerIn: parent; text: qsTr("no supported photos found"); color: "#aebbc1" }
        }
        RowLayout {
            Item { Layout.fillWidth: true }
            TouchButton {
                objectName: "confirmImport"
                text: dialog.selectedCount === 1 ? qsTr("add 1 photo") : qsTr("add %1 photos").arg(dialog.selectedCount); checked: true
                enabled: dialog.selectedCount > 0 && !dialog.photoController.busy
                onClicked: {
                    const urls = []
                    for(const item of dialog.photoController.importCandidates)
                        if(dialog.chosen[item.path]) urls.push(item.path)
                    dialog.photoController.importSelectedPaths(urls)
                    dialog.close()
                }
            }
        }
    }
}
