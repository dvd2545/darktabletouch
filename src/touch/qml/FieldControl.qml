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
    property real increment: integerField ? 1 : field.name === "black" ? 0.0001 : 0.01
    implicitHeight: booleanField ? 72 : enumField ? 104 : 96

    Loader {
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
            onToggled: controller.setParameter(root.field.name, checked, true)
        }
    }

    Component {
        id: enumControl
        ColumnLayout {
            spacing: 8
            Label { text: root.label; color: "#aeb4be"; font.pixelSize: 14 }
            ComboBox {
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
                delegate: ItemDelegate {
                    required property int index
                    text: root.field.values[index].name
                    height: 64
                    width: parent.width
                }
                onActivated: function(index) { controller.setParameter(root.field.name, root.field.values[index].value, true) }
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
                    text: root.label + (root.field.name === "exposure" ? " (EV)" : "")
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
                    Layout.fillWidth: true
                    Layout.preferredHeight: 64
                    visible: root.hasSlider
                    from: Math.min(root.field.soft_min === undefined ? (root.field.min || 0) : root.field.soft_min, Number(root.field.value || 0))
                    to: Math.max(root.field.soft_max === undefined ? (root.field.max === undefined ? 1 : root.field.max) : root.field.soft_max, Number(root.field.value || 0))
                    stepSize: root.increment
                    focusPolicy: Qt.StrongFocus
                    property real lastMoved: value
                    Binding { target: slider; property: "value"; when: !slider.pressed; value: Number(root.field.value || 0); restoreMode: Binding.RestoreNone }
                    onMoved: {
                        lastMoved = value
                        controller.setParameter(root.field.name, value, !pressed)
                    }
                    onPressedChanged: {
                        if(pressed) lastMoved = value
                        else controller.setParameter(root.field.name, lastMoved, true)
                    }
                }
                TextField {
                    id: numeric
                    Layout.preferredWidth: root.hasSlider ? 112 : 220
                    Layout.preferredHeight: 64
                    font.pixelSize: 16
                    horizontalAlignment: TextInput.AlignHCenter
                    selectByMouse: true
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    validator: DoubleValidator {
                        bottom: root.field.min === undefined ? -1.7976931348623157e308 : root.field.min
                        top: root.field.max === undefined ? 1.7976931348623157e308 : root.field.max
                        locale: "en_US"
                        notation: DoubleValidator.ScientificNotation
                    }
                    Binding {
                        target: numeric; property: "text"; when: !numeric.activeFocus
                        value: Number(root.field.value || 0).toLocaleString(Qt.locale("en_US"), "g", 6)
                        restoreMode: Binding.RestoreNone
                    }
                    onEditingFinished: {
                        if(acceptableInput) controller.setParameter(root.field.name,
                            Number.fromLocaleString(Qt.locale("en_US"), text), true)
                    }
                }
                TouchButton {
                    text: qsTr("reset")
                    implicitWidth: 64
                    onClicked: controller.setParameter(root.field.name, root.field.default, true)
                }
            }
        }
    }
}
