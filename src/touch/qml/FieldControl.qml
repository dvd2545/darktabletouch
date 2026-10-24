import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var field
    property string label: (field.label || field.name || "").replace(/_/g, " ")
    property bool booleanField: field.type === "bool"
    property bool enumField: field.type === "enum"
    property bool integerField: ["int", "uint", "int8", "uint8", "short", "ushort"].indexOf(field.type) >= 0
    property bool numericField: integerField || field.type === "float" || field.type === "double"
    property bool hasSlider: !booleanField && !enumField
        && typeof field.min === "number" && typeof field.max === "number"
        && Math.abs(field.min) <= 100000 && Math.abs(field.max) <= 100000 && field.max > field.min
    property real increment: typeof field.step === "number" ? field.step : integerField ? 1 : field.name === "black" ? 0.0001 : 0.01
    implicitHeight: booleanField ? 72 : enumField ? Math.max(96, fieldLoader.item ? fieldLoader.item.implicitHeight : 96) : 88

    Loader {
        id: fieldLoader
        anchors.fill: parent
        sourceComponent: root.booleanField ? booleanControl : root.enumField ? enumControl : root.numericField ? numberControl : unavailableControl
    }

    Component {
        id: unavailableControl
        ColumnLayout {
            Label { text: root.label; color: "#aeb4be"; Layout.fillWidth: true; wrapMode: Text.Wrap }
            Label { text: qsTr("use classic for this control"); color: "#8993a1"; Layout.fillWidth: true; wrapMode: Text.Wrap }
        }
    }

    Component {
        id: booleanControl
        Switch {
            text: root.label
            checked: Boolean(root.field.value)
            font.pixelSize: 15
            focusPolicy: Qt.StrongFocus
            onToggled: controller.setParameter(root.field.name, checked, true, root.field.operation || "")
        }
    }

    Component {
        id: enumControl
        ColumnLayout {
            spacing: 8
            Label { text: root.label; color: "#aeb4be"; font.pixelSize: 14 }
            TouchComboBox {
                objectName: "parameter-" + root.field.name
                Layout.fillWidth: true
                Layout.preferredHeight: 64
                model: root.field.values || []
                textRole: "name"
                valueRole: "value"
                currentIndex: {
                    const choices = root.field.values || []
                    for(let i = 0; i < choices.length; i++)
                        if(Number(choices[i].value) === Number(root.field.value)) return i
                    return -1
                }
                displayText: currentIndex >= 0 ? root.field.values[currentIndex].name : qsTr("select")
                font.pixelSize: 15
                focusPolicy: Qt.StrongFocus
                enabled: root.field.editable !== false
                delegate: ItemDelegate {
                    required property int index
                    text: root.field.values[index].name
                    height: 64
                    width: parent.width
                }
                onActivated: function(index) { controller.setParameter(root.field.name, root.field.values[index].value, true, root.field.operation || "") }
            }
            Label {
                visible: !!root.field.hint
                text: root.field.hint || ""; color: "#9eb5bd"; font.pixelSize: 12
                Layout.fillWidth: true; wrapMode: Text.WordWrap
            }
        }
    }

    Component {
        id: numberControl
        ColumnLayout {
            spacing: 5
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: root.label + (root.field.unit ? " (" + root.field.unit + ")" : "")
                    color: "#aeb4be"
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    font.pixelSize: 14
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Slider {
                    id: slider
                    objectName: "parameter-" + root.field.name
                    Layout.fillWidth: true
                    Layout.preferredHeight: 64
                    visible: root.hasSlider
                    from: Math.min(root.field.soft_min === undefined ? (root.field.min || 0) : root.field.soft_min, Number(root.field.value || 0))
                    to: Math.max(root.field.soft_max === undefined ? (root.field.max === undefined ? 1 : root.field.max) : root.field.soft_max, Number(root.field.value || 0))
                    stepSize: root.increment
                    focusPolicy: Qt.StrongFocus
                    onActiveFocusChanged: if(activeFocus) controller.rememberParameter(root.field.name, root.field.operation || "", root.increment)
                    property real lastMoved: value
                    background: Rectangle {
                        x: slider.leftPadding; y: slider.topPadding + slider.availableHeight / 2 - height / 2
                        width: slider.availableWidth; height: 6; radius: 3; color: "#3b484d"
                        Rectangle { width: slider.visualPosition * parent.width; height: parent.height; radius: 3; color: "#00dbe5" }
                    }
                    handle: Rectangle {
                        x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
                        y: slider.topPadding + slider.availableHeight / 2 - height / 2
                        width: 24; height: 24; radius: 12
                        color: slider.pressed ? "#00dbe5" : "#d8e6ed"
                        border.width: slider.activeFocus ? 2 : 0; border.color: "#00dbe5"
                    }
                    Binding { target: slider; property: "value"; when: !slider.pressed; value: Number(root.field.value || 0); restoreMode: Binding.RestoreNone }
                    onMoved: {
                        lastMoved = value
                        controller.setParameter(root.field.name, value, !pressed, root.field.operation || "")
                    }
                    onPressedChanged: {
                        if(pressed) lastMoved = value
                        else controller.setParameter(root.field.name, lastMoved, true, root.field.operation || "")
                    }
                }
                TextField {
                    id: numeric
                    objectName: "numeric-" + root.field.name
                    property bool edited: false
                    onActiveFocusChanged: if(activeFocus) { edited = false; controller.rememberParameter(root.field.name, root.field.operation || "", root.increment) }
                    onTextEdited: edited = true
                    Layout.preferredWidth: root.hasSlider ? 100 : 220
                    Layout.preferredHeight: 64
                    font.pixelSize: 14
                    padding: 6
                    horizontalAlignment: TextInput.AlignHCenter
                    selectByMouse: true
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    color: "#e6eef0"
                    background: Rectangle {
                        radius: 8; color: "#253035"
                        border.width: numeric.activeFocus ? 2 : 1
                        border.color: numeric.edited && !numeric.acceptableInput ? "#ffa99e" : numeric.activeFocus ? "#00dbe5" : "#35434a"
                    }
                    validator: DoubleValidator {
                        bottom: root.field.min === undefined ? -1.7976931348623157e308 : root.field.min
                        top: root.field.max === undefined ? 1.7976931348623157e308 : root.field.max
                        locale: "en_US"
                        notation: DoubleValidator.ScientificNotation
                    }
                    Binding {
                        target: numeric; property: "text"; when: !numeric.activeFocus && !numeric.edited
                        value: Number(root.field.value || 0).toLocaleString(Qt.locale("en_US"), "g", 6)
                        restoreMode: Binding.RestoreNone
                    }
                    onEditingFinished: {
                        if(edited && acceptableInput) {
                            const next = Number.fromLocaleString(Qt.locale("en_US"), text)
                            controller.setParameter(root.field.name, next, true, root.field.operation || "")
                            edited = false
                        }
                    }
                }
            }
        }
    }
}
