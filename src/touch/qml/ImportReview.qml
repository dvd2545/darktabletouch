import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

TouchDialog {
    id: dialog
    UiMetrics { id: reviewMetrics; pixelDensity: dialog.parent.Screen.pixelDensity; pixelRatio: dialog.parent.Screen.devicePixelRatio }
    required property var photoController
    property var chosen: ({})
    property bool onlyNew: true
    property bool rawOnly: false
    readonly property var candidates: photoController.importCandidates.filter(function(item) { return (!dialog.onlyNew || !item.existing) && (!dialog.rawOnly || item.raw) })
    readonly property int selectedCount: Object.keys(chosen).filter(function(key) { return chosen[key] }).length
    modal: true
    focus: true
    title: qsTr("add to library")
    anchors.centerIn: parent
    width: Math.min(800, parent.width - 32)
    height: Math.min(650, parent.height - 32)
    padding: Math.round(16 * reviewMetrics.paddingScale)
    function selectAll(value) {
        const next = {}
        for(const item of candidates) if(!item.existing) next[item.path] = value
        chosen = next
    }
    function toggle(path) { const next = Object.assign({}, chosen); next[path] = !next[path]; chosen = next }
    onCandidatesChanged: {
        const next = {}
        for(const item of candidates) if(!item.existing && chosen[item.path]) next[item.path] = true
        chosen = next
    }
    onOpened: selectAll(true)
    contentItem: ColumnLayout {
        spacing: 10
        Label {
            Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#aebbc1"
            text: qsTr("choose images to add at their current location; existing catalog entries are skipped")
        }
        RowLayout {
            TouchButton { text: qsTr("select all new"); onClicked: dialog.selectAll(true) }
            TouchButton { text: qsTr("select none"); onClicked: dialog.selectAll(false) }
            Label { Layout.fillWidth: true; horizontalAlignment: Text.AlignRight; text: qsTr("%1 selected").arg(dialog.selectedCount); color: "#e6eef0" }
        }
        ListView {
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 4
            model: dialog.candidates
            delegate: TouchButton {
                required property var modelData
                required property int index
                width: ListView.view.width; implicitHeight: reviewMetrics.touchHeight
                padding: Math.round(8 * reviewMetrics.paddingScale)
                text: modelData.filename
                Accessible.name: modelData.filename
                Accessible.description: modelData.path + (modelData.existing ? " - " + qsTr("already in library") : "")
                ToolTip.visible: hovered || activeFocus; ToolTip.text: modelData.path
                contentItem: RowLayout {
                    spacing: reviewMetrics.gap
                    Icon { name: modelData.existing || dialog.chosen[modelData.path] ? "check" : "add"; color: dialog.chosen[modelData.path] ? "#07191b" : reviewMetrics.muted; Layout.preferredWidth: 20; Layout.preferredHeight: 20 }
                    ColumnLayout {
                        spacing: 2; Layout.fillWidth: true; Layout.minimumWidth: 0
                        Label { text: modelData.filename; textFormat: Text.PlainText; font.pixelSize: reviewMetrics.bodySize; color: dialog.chosen[modelData.path] ? "#07191b" : reviewMetrics.text; Layout.fillWidth: true; elide: Text.ElideMiddle }
                        Label { text: modelData.path; textFormat: Text.PlainText; font.pixelSize: reviewMetrics.captionSize; color: dialog.chosen[modelData.path] ? "#173c40" : reviewMetrics.muted; Layout.fillWidth: true; elide: Text.ElideMiddle }
                    }
                    Label { text: qsTr("already in library"); visible: modelData.existing; color: reviewMetrics.muted; font.pixelSize: reviewMetrics.captionSize }
                }
                checked: !!dialog.chosen[modelData.path]; enabled: !modelData.existing
                onClicked: dialog.toggle(modelData.path)
                onActiveFocusChanged: if(activeFocus) { ListView.view.currentIndex = index; ListView.view.positionViewAtIndex(index, ListView.Contain) }
                Keys.onPressed: function(event) {
                    const direction = event.key === Qt.Key_Tab || event.key === Qt.Key_Down ? 1 : event.key === Qt.Key_Backtab || event.key === Qt.Key_Up ? -1 : 0
                    let next = index + direction
                    while(direction && next >= 0 && next < dialog.candidates.length && dialog.candidates[next].existing) next += direction
                    if(direction && next >= 0 && next < dialog.candidates.length) {
                        event.accepted = true
                        const view = ListView.view
                        view.currentIndex = next
                        view.positionViewAtIndex(next, ListView.Contain)
                        Qt.callLater(function() { const item = view.itemAtIndex(next); if(item) item.forceActiveFocus() })
                    }
                }
            }
            ScrollBar.vertical: ScrollBar { }
            Label { visible: !dialog.candidates.length; anchors.centerIn: parent; text: qsTr("no images match the import options"); color: "#aebbc1" }
        }
        RowLayout {
            Item { Layout.fillWidth: true }
            TouchButton {
                objectName: "confirmImport"
                text: dialog.selectedCount === 1 ? qsTr("add 1 image") : qsTr("add %1 images").arg(dialog.selectedCount); checked: true
                enabled: dialog.selectedCount > 0 && !dialog.photoController.busy
                onClicked: {
                    const urls = []
                    for(const item of dialog.candidates)
                        if(dialog.chosen[item.path]) urls.push(item.path)
                    dialog.photoController.importSelectedPaths(urls)
                    dialog.close()
                }
            }
        }
    }
}
