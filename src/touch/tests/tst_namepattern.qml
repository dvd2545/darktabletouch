import QtQuick
import QtQuick.Controls
import QtTest
import "../qml"

TestCase {
    id: tests
    name: "NamePattern"
    width: 600; height: 400; visible: true; when: windowShown
    Component { id: component; NamePatternEditor { width: 560 } }
    function test_duplicateRemovalPreservesText() {
        const editor = createTemporaryObject(component, tests)
        editor.text = "{name}_trip_{name}_{sequence}_{invalid}"
        compare(editor.tags(editor.text).length, 3)
        verify(waitForPolish(editor))
        mouseClick(findChild(editor, "removeNameTag-1"))
        compare(editor.text, "{name}_trip__{sequence}_{invalid}")
        compare(editor.tags(editor.text).length, 2)
    }
    function test_insertAtCursor() {
        const editor = createTemporaryObject(component, tests)
        editor.text = "trip_"
        findChild(editor, "namePatternInput").cursorPosition = 5
        editor.insertTag("{id}")
        compare(editor.text, "trip_{id}")
        compare(editor.tags(editor.text)[0].text, "{id}")
    }
}
