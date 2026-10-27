import QtQuick
import QtQuick.Controls
import QtTest
import "../qml"

TestCase {
    id: tests
    name: "ChoicePopup"
    width: 960; height: 540
    visible: true; when: windowShown
    property QtObject gamepad: QtObject { property int choicesOpen: 0 }
    TouchComboBox { id: choice; width: 260; model: ["folder", "filename", "camera"] }
    function cleanup() { choice.popup.close(); tryCompare(choice.popup, "visible", false) }
    function test_choicesRemainUsableInModalPopup() {
        choice.popup.open()
        tryCompare(choice.popup, "opened", true)
        verify(choice.popup.modal)
        const list = findChild(choice.popup, "choicePopupList")
        tryVerify(function() { return choice.popup.height >= 3 * choice.height })
        compare(list.count, 3)
        mouseClick(list.itemAtIndex(1))
        tryCompare(choice, "currentIndex", 1)
        tryCompare(choice.popup, "visible", false)
    }
}
