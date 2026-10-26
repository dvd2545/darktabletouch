import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    property alias text: input.text
    property var modifiers: []
    readonly property var allModifiers: [{tag: "{name}", description: qsTr("image filename")}, {tag: "{sequence}", description: qsTr("four-digit sequence")}, {tag: "{id}", description: qsTr("image ID")}].concat(modifiers)
    readonly property bool valid: validPattern(input.text)
    UiMetrics { id: metrics; pixelDensity: root.Screen.pixelDensity; pixelRatio: root.Screen.devicePixelRatio }
    function tags(value) {
        const result = [], expression = /\{(name|sequence|id)\}|\$\(([^()]*)\)/g
        let match
        while((match = expression.exec(value)) !== null) {
            if(!match[2] || modifiers.some(function(entry) { return entry.tag.split("[")[0] === match[2].split("[")[0] }))
                result.push({text: match[0], start: match.index, length: match[0].length})
        }
        return result
    }
    function validPattern(value) {
        let remaining = value
        const recognized = tags(value)
        for(let i = recognized.length - 1; i >= 0; --i) remaining = remaining.slice(0, recognized[i].start) + remaining.slice(recognized[i].start + recognized[i].length)
        return value.trim().length > 0 && !/[\u0000-\u001f<>:"/\\|?*{}]/.test(remaining) && remaining.indexOf("$(") < 0 && !/[. ]$/.test(value)
    }
    function removeTag(index) {
        const tag = tags(input.text)[index]
        if(tag) input.text = input.text.slice(0, tag.start) + input.text.slice(tag.start + tag.length)
    }
    function insertTag(tag) { input.insert(input.cursorPosition, tag); input.forceActiveFocus() }
    TextField {
        id: input
        objectName: "namePatternInput"
        Layout.fillWidth: true; implicitHeight: metrics.touchHeight
        text: "{name}_edited"; selectByMouse: true
        Accessible.name: qsTr("export name pattern")
    }
    Flow {
        Layout.fillWidth: true; spacing: 4
        Repeater {
            model: root.tags(input.text)
            delegate: Rectangle {
                required property var modelData
                required property int index
                height: metrics.touchHeight; width: Math.min(root.width, tagRow.implicitWidth + 12); radius: metrics.radius
                color: "#153439"; border.color: metrics.accent
                RowLayout {
                    id: tagRow
                    anchors.fill: parent; anchors.margins: 6; spacing: 0
                    Label { text: modelData.text; textFormat: Text.PlainText; color: metrics.text; Layout.fillWidth: true; elide: Text.ElideMiddle }
                    TouchButton { objectName: "removeNameTag-" + index; text: qsTr("remove %1").arg(modelData.text); iconName: "close"; showLabel: false; flat: true; onClicked: root.removeTag(index) }
                }
            }
        }
    }
    Flow {
        Layout.fillWidth: true; spacing: 4
        TouchButton { objectName: "addModifierButton"; text: qsTr("add modifier"); iconName: "add"; onClicked: { modifierSearch.text = ""; modifierBrowser.open() } }
    }
    Label {
        Layout.fillWidth: true; wrapMode: Text.Wrap; color: metrics.muted; textFormat: Text.PlainText
        text: input.text.indexOf("$(") >= 0 ? qsTr("modifiers use each image's metadata when exporting") : qsTr("preview: %1").arg(input.text.replace(/\{name\}/g, "image").replace(/\{sequence\}/g, "0001").replace(/\{id\}/g, "42"))
    }
    TouchDialog {
        id: modifierBrowser; objectName: "modifierBrowser"; parent: Overlay.overlay
        anchors.centerIn: parent; width: Math.min(640, parent.width - 32); height: Math.min(620, parent.height - 32)
        title: qsTr("add modifier"); modal: true; focus: true
        contentItem: ColumnLayout {
            TextField { id: modifierSearch; objectName: "modifierSearch"; Layout.fillWidth: true; implicitHeight: metrics.touchHeight; placeholderText: qsTr("search all modifiers"); Accessible.name: placeholderText }
            ListView {
                id: modifierList; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; boundsBehavior: Flickable.StopAtBounds
                model: root.allModifiers.filter(function(entry) { const query = modifierSearch.text.toLowerCase(); return (entry.tag + " " + entry.description).toLowerCase().includes(query) })
                delegate: TouchButton {
                    required property var modelData; required property int index
                    width: modifierList.width; text: modelData.tag + "\n" + modelData.description.replace(/^\$\([^)]*\)\s*-\s*/, ""); iconName: "add"; flat: true
                    onClicked: { root.insertTag(modelData.tag.startsWith("{") ? modelData.tag : "$(" + modelData.tag + ")"); modifierBrowser.close() }
                    onActiveFocusChanged: if(activeFocus) modifierList.positionViewAtIndex(index, ListView.Contain)
                }
                ScrollBar.vertical: ScrollBar {}
            }
        }
    }
}
