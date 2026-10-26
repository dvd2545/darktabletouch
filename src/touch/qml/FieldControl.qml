import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    UiMetrics { id: metrics; pixelDensity: root.Screen.pixelDensity; pixelRatio: root.Screen.devicePixelRatio }
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
    implicitHeight: Math.max(metrics.touchHeight, fieldLoader.item ? fieldLoader.item.implicitHeight : 0)

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
            objectName: "parameter-" + root.field.name
            text: root.label
            Accessible.name: root.label
            checked: Boolean(root.field.value)
            implicitHeight: metrics.touchHeight
            font.pixelSize: metrics.bodySize
            focusPolicy: Qt.StrongFocus
            enabled: root.field.editable !== false
            onToggled: if(enabled) controller.setParameter(root.field.name, checked, true, root.field.operation || "")
        }
    }

    Component {
        id: enumControl
        RowLayout {
            spacing: metrics.gap
            Label { text: root.label; color: metrics.muted; font.pixelSize: metrics.captionSize; Layout.preferredWidth: 70; wrapMode: Text.Wrap }
            TouchComboBox {
                objectName: "parameter-" + root.field.name
                Accessible.name: root.label
                Layout.fillWidth: true
                Layout.preferredHeight: metrics.touchHeight
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
                ToolTip.visible: hovered || activeFocus
                ToolTip.text: root.field.hint || ""
                Accessible.description: root.field.hint || ""
                font.pixelSize: metrics.bodySize
                focusPolicy: Qt.StrongFocus
                enabled: root.field.editable !== false
                delegate: ChoiceDelegate {
                    required property int index
                    text: root.field.values[index].name
                    height: metrics.touchHeight
                    width: parent.width
                }
                onActivated: function(index) { if(enabled) controller.setParameter(root.field.name, root.field.values[index].value, true, root.field.operation || "") }
            }
        }
    }

    Component {
        id: numberControl
        ColumnLayout {
            spacing: 0
            RowLayout {
                Layout.fillWidth: true; spacing: 4
                TouchButton {
                    objectName: "field-options-" + root.field.name
                    text: root.label + (root.field.unit ? " (" + root.field.unit + ")" : "")
                    iconName: "more"; flat: true; padding: 4
                    Layout.fillWidth: true; Layout.minimumWidth: 0
                    font.pixelSize: metrics.captionSize
                    Accessible.description: qsTr("value options and reset")
                    onClicked: fieldActions.open()
                }
                TextField {
                    id: numeric
                    objectName: "numeric-" + root.field.name
                    Accessible.name: root.label
                    Accessible.description: validation.visible ? validation.text : root.field.unit || ""
                    enabled: root.field.editable !== false
                    property bool edited: false
                    property bool validationRequested: false
                    readonly property real parsedValue: {
                        try { return Number.fromLocaleString(Qt.locale("en_US"), text) }
                        catch(error) { return NaN }
                    }
                    readonly property string validationError: {
                        if(!text.trim().length) return qsTr("enter a number")
                        if(!isFinite(parsedValue)) return qsTr("enter a valid number using a decimal point")
                        if(root.integerField && Math.floor(parsedValue) !== parsedValue) return qsTr("enter a whole number")
                        if(root.field.min !== undefined && parsedValue < root.field.min)
                            return qsTr("enter a value of at least %1").arg(root.field.min)
                        if(root.field.max !== undefined && parsedValue > root.field.max)
                            return qsTr("enter a value of at most %1").arg(root.field.max)
                        return acceptableInput ? "" : qsTr("enter a valid number using a decimal point")
                    }
                    function engineText() {
                        const value = Number(root.field.value || 0)
                        return numeric.activeFocus || root.integerField ? String(value) : String(Number(value.toPrecision(6)))
                    }
                    function finishEdit() {
                        if(!edited || !enabled) return
                        validationRequested = true
                        if(validationError.length) return
                        controller.setParameter(root.field.name, parsedValue, true, root.field.operation || "")
                        edited = false
                        validationRequested = false
                    }
                    function discardEdit() {
                        edited = false
                        validationRequested = false
                        text = engineText()
                        forceActiveFocus()
                        selectAll()
                    }
                    onActiveFocusChanged: {
                        if(activeFocus) {
                            if(!edited) text = engineText()
                            controller.rememberParameter(root.field.name, root.field.operation || "", root.increment)
                        }
                        else finishEdit()
                    }
                    onTextEdited: edited = true
                    Layout.preferredWidth: Math.min(132, root.width * 0.44)
                    Layout.preferredHeight: metrics.touchHeight
                    font.pixelSize: 14
                    padding: 6
                    horizontalAlignment: TextInput.AlignHCenter
                    selectByMouse: true
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    color: "#e6eef0"
                    background: Rectangle {
                        radius: metrics.radius; color: numeric.activeFocus ? "#253035" : "transparent"
                        border.width: numeric.activeFocus ? 2 : 0
                        border.color: numeric.edited && numeric.validationError.length ? "#ffa99e" : numeric.activeFocus ? "#00dbe5" : "#35434a"
                    }
                    validator: DoubleValidator {
                        bottom: root.field.min === undefined ? -1.7976931348623157e308 : root.field.min
                        top: root.field.max === undefined ? 1.7976931348623157e308 : root.field.max
                        locale: "en_US"
                        notation: DoubleValidator.ScientificNotation
                    }
                    Binding {
                        target: numeric; property: "text"; when: !numeric.activeFocus && !numeric.edited
                        value: numeric.engineText()
                        restoreMode: Binding.RestoreNone
                    }
                    onEditingFinished: finishEdit()
                    Keys.onReturnPressed: function(event) { finishEdit(); event.accepted = true }
                    Keys.onEnterPressed: function(event) { finishEdit(); event.accepted = true }
                    Keys.onEscapePressed: function(event) { discardEdit(); event.accepted = true }
                }
            }
                Slider {
                    id: slider
                    objectName: "parameter-" + root.field.name
                    Accessible.name: root.label
                    Accessible.description: root.field.unit || ""
                    Layout.fillWidth: true
                    Layout.preferredHeight: metrics.touchHeight
                    visible: root.hasSlider
                    enabled: root.field.editable !== false
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
                        if(!enabled) return
                        lastMoved = value
                        controller.setParameter(root.field.name, value, !pressed, root.field.operation || "")
                    }
                    onPressedChanged: {
                        if(pressed) lastMoved = value
                        else if(enabled) controller.setParameter(root.field.name, lastMoved, true, root.field.operation || "")
                    }
                }
            Label {
                id: validation
                objectName: "validation-" + root.field.name
                visible: numeric.edited && numeric.validationRequested && !!numeric.validationError
                text: numeric.validationError
                textFormat: Text.PlainText
                color: "#ffa99e"
                font.pixelSize: 13
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            TouchButton {
                objectName: "discard-" + root.field.name
                text: qsTr("discard invalid edit")
                Accessible.name: qsTr("discard invalid edit for %1").arg(root.label)
                visible: validation.visible
                Layout.fillWidth: true
                Layout.preferredHeight: metrics.touchHeight
                onClicked: numeric.discardEdit()
            }
            TouchPopup {
                id: fieldActions
                parent: Overlay.overlay; anchors.centerIn: parent
                width: Math.min(400, parent.width - 32); padding: metrics.inset; modal: true; focus: true
                background: Rectangle { color: metrics.floating; radius: metrics.radius }
                onOpened: contentItem.forceActiveFocus()
                contentItem: ColumnLayout {
                    objectName: "fieldActionsContent"
                    Label { text: root.label; color: metrics.text; font.pixelSize: metrics.titleSize; Layout.fillWidth: true; wrapMode: Text.Wrap }
                    Label { text: String(root.field.value); color: metrics.muted; Layout.fillWidth: true; wrapMode: Text.Wrap }
                    TouchButton {
                        objectName: "reset-" + root.field.name
                        text: qsTr("reset")
                        Accessible.name: qsTr("reset %1 to default").arg(root.label)
                        Layout.preferredWidth: 64
                        Layout.preferredHeight: metrics.touchHeight
                        padding: 6
                        enabled: root.field.editable !== false
                            && typeof root.field.default === "number" && isFinite(root.field.default)
                            && Number(root.field.value) !== root.field.default
                        onClicked: {
                            fieldActions.close()
                            if(!enabled) return
                            numeric.edited = false
                            numeric.validationRequested = false
                            controller.setParameter(root.field.name, root.field.default, true, root.field.operation || "")
                            numeric.text = numeric.engineText()
                        }
                    }
                }
            }
        }
    }
}
