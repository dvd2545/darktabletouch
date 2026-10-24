import QtQuick
import QtQuick.Controls

ComboBox {
    id: control
    indicator: Icon {
        name: "chevron-down"; width: 18; height: 18
        x: control.width - width - control.rightPadding; y: (control.height - height) / 2
    }
    delegate: ItemDelegate { required property int index; width: control.width; height: 64; text: control.textAt(index); highlighted: control.highlightedIndex === index }
    popup: TouchPopup {
        onOpened: gamepad.choicesOpen++
        onClosed: gamepad.choicesOpen = Math.max(0, gamepad.choicesOpen - 1)
        y: control.height; width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, 360)
        padding: 6
        contentItem: ListView {
            objectName: "choicePopupList"
            clip: true; implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Rectangle { color: "#1c272c"; radius: 10; border.color: "#334249" }
    }
}
