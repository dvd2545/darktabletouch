import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    property alias text: input.text
    UiMetrics { id: metrics; pixelDensity: root.Screen.pixelDensity; pixelRatio: root.Screen.devicePixelRatio }
    function tags(value) {
        const result = [], expression = /\{(name|sequence|id)\}/g
        let match
        while((match = expression.exec(value)) !== null) result.push({text: match[0], start: match.index, length: match[0].length})
        return result
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
                height: metrics.touchHeight; width: tagRow.implicitWidth + 12; radius: metrics.radius
                color: "#153439"; border.color: metrics.accent
                RowLayout {
                    id: tagRow
                    anchors.centerIn: parent; spacing: 0
                    Label { text: modelData.text; textFormat: Text.PlainText; color: metrics.text }
                    TouchButton { objectName: "removeNameTag-" + index; text: qsTr("remove %1").arg(modelData.text); iconName: "close"; showLabel: false; flat: true; onClicked: root.removeTag(index) }
                }
            }
        }
    }
    Flow {
        Layout.fillWidth: true; spacing: 4
        Repeater {
            model: ["{name}", "{sequence}", "{id}"]
            delegate: TouchButton { required property string modelData; text: qsTr("add %1").arg(modelData); flat: true; onClicked: root.insertTag(modelData) }
        }
    }
    Label {
        Layout.fillWidth: true; wrapMode: Text.Wrap; color: metrics.muted; textFormat: Text.PlainText
        text: qsTr("preview: %1").arg(input.text.replace(/\{name\}/g, "image").replace(/\{sequence\}/g, "0001").replace(/\{id\}/g, "42"))
    }
}
