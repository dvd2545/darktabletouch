import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: editor
    required property var dialog
    required property string group
    Repeater {
        model: dialog.draft[editor.group]
        Rectangle {
            id: card
            required property var modelData
            required property int index
            readonly property string group: editor.group
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
        Layout.fillWidth: true
        text: editor.group === "sort" ? qsTr("add sort priority") : qsTr("add rule")
        enabled: dialog.draft[editor.group].length < (editor.group === "sort" ? dialog.sortNames.length : 10)
        onClicked: dialog.add(editor.group)
    }
}
