import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    required property var photoController
    property int page: 0
    property var draft: ({collection: [], filtering: [], sort: []})
    property alias focusColumn: ruleColumn
    readonly property var focusViewport: scroll.contentItem
    readonly property var properties: photoController.library.properties || []
    readonly property var sortNames: [qsTr("filename"), qsTr("capture time"), qsTr("import time"),
        qsTr("modification time"), qsTr("export time"), qsTr("print time"), qsTr("rating"),
        qsTr("image ID"), qsTr("color label"), qsTr("group"), qsTr("path"), qsTr("custom order"),
        qsTr("title"), qsTr("description"), qsTr("aspect ratio"), qsTr("shuffle")]
    modal: true
    focus: true
    title: qsTr("collections and filters")
    width: Math.min(860, parent.width - 32)
    height: Math.min(680, parent.height - 32)
    anchors.centerIn: parent
    padding: 16
    background: Rectangle { color: "#121a1e"; radius: 16; border.color: "#334249" }

    function propertyIndex(item) {
        for(let i = 0; i < properties.length; ++i) if(properties[i].item === item) return i
        return -1
    }
    function change(group, index, key, value) {
        const next = JSON.parse(JSON.stringify(draft))
        next[group][index][key] = value
        draft = next
    }
    function remove(group, index) {
        const next = JSON.parse(JSON.stringify(draft))
        next[group].splice(index, 1)
        draft = next
    }
    function add(group) {
        const next = JSON.parse(JSON.stringify(draft))
        if(group === "sort") next.sort.push({item: 0, order: 0})
        else next[group].push({item: 2, mode: 0, off: 0, top: 0, string: ""})
        draft = next
    }
    function chooseFolder(folder) {
        const next = JSON.parse(JSON.stringify(draft))
        next.collection = [{item: 0, mode: 0, off: 0, top: 0, string: folder}]
        draft = next
        page = 1
    }
    onOpened: {
        const state = photoController.library
        draft = JSON.parse(JSON.stringify({collection: state.collection || [], filtering: state.filtering || [], sort: state.sort || []}))
        if(draft.collection.length === 0) draft.collection = [{item: 0, mode: 0, off: 0, top: 0, string: "%"}]
    }
    contentItem: ColumnLayout {
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            Repeater {
                model: [qsTr("folders"), qsTr("collection"), qsTr("filters"), qsTr("sort")]
                TouchButton { required property int index; required property string modelData; text: modelData; Layout.fillWidth: true; checked: dialog.page === index; onClicked: dialog.page = index }
            }
        }
        Label {
            Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#aebbc1"
            text: dialog.page === 0 ? qsTr("choose an imported folder, then apply; filters remain independent")
                : dialog.page === 1 ? qsTr("combine collection rules using Classic's matching syntax; % matches any text")
                : dialog.page === 2 ? qsTr("refine the collection; disable a filter to keep it without applying it")
                : qsTr("sort priorities run from top to bottom")
        }
        ScrollView {
            id: scroll
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true
            contentWidth: availableWidth
            ColumnLayout {
                id: ruleColumn
                width: scroll.availableWidth; spacing: 10
                TouchButton {
                    visible: dialog.page === 0; Layout.fillWidth: true; text: qsTr("all imported photos")
                    onClicked: dialog.chooseFolder("%")
                }
                Repeater {
                    model: dialog.page === 0 ? (dialog.photoController.library.folders || []) : []
                    TouchButton {
                        required property var modelData
                        Layout.fillWidth: true; text: modelData.folder + "  (" + modelData.images + ")"
                        implicitHeight: Math.max(64, contentItem.implicitHeight + 28)
                        onClicked: dialog.chooseFolder(modelData.folder)
                    }
                }
                Label { visible: dialog.page === 0 && !(dialog.photoController.library.folders || []).length; text: qsTr("no imported folders yet"); color: "#aebbc1" }
                Repeater {
                    model: dialog.page === 1 ? dialog.draft.collection : dialog.page === 2 ? dialog.draft.filtering : dialog.page === 3 ? dialog.draft.sort : []
                    Rectangle {
                        id: card
                        required property var modelData
                        required property int index
                        readonly property string group: dialog.page === 1 ? "collection" : dialog.page === 2 ? "filtering" : "sort"
                        readonly property var suggestions: dialog.propertyIndex(modelData.item) >= 0 ? (dialog.properties[dialog.propertyIndex(modelData.item)].values || []) : []
                        Layout.fillWidth: true; implicitHeight: controls.implicitHeight + 20
                        radius: 12; color: "#1b252a"
                        ColumnLayout {
                            id: controls
                            anchors.fill: parent; anchors.margins: 10
                            RowLayout {
                                Layout.fillWidth: true
                                ComboBox {
                                    visible: card.group !== "sort"; implicitHeight: 64; Layout.preferredWidth: 120
                                    model: [qsTr("AND"), qsTr("OR"), qsTr("AND NOT")]
                                    currentIndex: card.modelData.mode || 0
                                    onActivated: dialog.change(card.group, card.index, "mode", currentIndex)
                                    delegate: ItemDelegate { required property string modelData; width: parent.width; height: 64; text: modelData }
                                }
                                ComboBox {
                                    Layout.fillWidth: true; implicitHeight: 64
                                    model: card.group === "sort" ? dialog.sortNames : dialog.properties
                                    textRole: card.group === "sort" ? "" : "label"
                                    currentIndex: card.group === "sort" ? card.modelData.item : dialog.propertyIndex(card.modelData.item)
                                    onActivated: dialog.change(card.group, card.index, "item", card.group === "sort" ? currentIndex : dialog.properties[currentIndex].item)
                                    delegate: ItemDelegate {
                                        required property var modelData
                                        width: parent.width; height: 64; text: card.group === "sort" ? modelData : modelData.label
                                    }
                                }
                                TouchButton {
                                    text: qsTr("remove"); enabled: card.group !== "collection" || dialog.draft.collection.length > 1
                                    onClicked: dialog.remove(card.group, card.index)
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                TextField {
                                    visible: card.group !== "sort"; Layout.fillWidth: true; implicitHeight: 64
                                    text: card.modelData.string || ""; selectByMouse: true
                                    placeholderText: qsTr("value, % wildcard, or comparison such as >=3")
                                    Accessible.name: qsTr("rule value")
                                    onTextEdited: {
                                        // keep the draft object stable while typing so focus stays in the field
                                        dialog.draft[card.group][card.index].string = text
                                    }
                                }
                                ComboBox {
                                    visible: card.group !== "sort" && card.suggestions.length > 0
                                    implicitHeight: 64; Layout.preferredWidth: 150
                                    model: card.suggestions; displayText: qsTr("choose value")
                                    onActivated: dialog.change(card.group, card.index, "string", card.suggestions[currentIndex])
                                    delegate: ItemDelegate { required property string modelData; width: parent.width; height: 64; text: modelData }
                                }
                                TouchButton {
                                    visible: card.group === "filtering"; text: checked ? qsTr("enabled") : qsTr("disabled")
                                    checked: !card.modelData.off
                                    onClicked: dialog.change(card.group, card.index, "off", checked ? 1 : 0)
                                }
                                TouchButton {
                                    visible: card.group === "sort"; Layout.fillWidth: true
                                    text: card.modelData.order ? qsTr("descending") : qsTr("ascending")
                                    onClicked: dialog.change(card.group, card.index, "order", card.modelData.order ? 0 : 1)
                                }
                            }
                        }
                    }
                }
                TouchButton {
                    visible: dialog.page > 0; Layout.fillWidth: true
                    text: dialog.page === 3 ? qsTr("add sort priority") : qsTr("add rule")
                    enabled: (dialog.page === 1 ? dialog.draft.collection.length : dialog.page === 2 ? dialog.draft.filtering.length : dialog.draft.sort.length) < (dialog.page === 3 ? dialog.sortNames.length : 10)
                    onClicked: dialog.add(dialog.page === 1 ? "collection" : dialog.page === 2 ? "filtering" : "sort")
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            TouchButton { text: qsTr("reset all"); onClicked: dialog.draft = {collection: [{item: 0, mode: 0, off: 0, top: 0, string: "%"}], filtering: [], sort: [{item: 0, order: 0}]} }
            Item { Layout.fillWidth: true }
            TouchButton { text: qsTr("cancel"); onClicked: dialog.close() }
            TouchButton { text: qsTr("apply"); checked: true; enabled: dialog.photoController.ready && !dialog.photoController.busy; onClicked: { dialog.photoController.libraryCommand("collection", dialog.draft); dialog.close() } }
        }
    }
}
