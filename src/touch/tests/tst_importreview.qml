import QtQuick
import QtQuick.Controls
import QtTest
import "../qml"

TestCase {
    id: tests
    name: "ImportOptions"
    width: 960; height: 540; visible: true; when: windowShown
    QtObject {
        id: mock
        property bool busy: false
        property var importCandidates: [
            {path: "C:/new.CR3", filename: "new.CR3", existing: false, raw: true},
            {path: "C:/old.CR3", filename: "old.CR3", existing: true, raw: true},
            {path: "C:/new.jpg", filename: "new.jpg", existing: false, raw: false}
        ]
        function importSelectedPaths(paths) {}
    }
    Component { id: component; ImportReview { photoController: mock } }
    function test_newAndRawFilters() {
        const dialog = createTemporaryObject(component, tests)
        compare(dialog.candidates.length, 2)
        dialog.selectAll(true)
        compare(dialog.selectedCount, 2)
        dialog.rawOnly = true
        compare(dialog.candidates.length, 1)
        compare(dialog.selectedCount, 1)
        verify(dialog.chosen["C:/new.CR3"])
        verify(!dialog.chosen["C:/new.jpg"])
        dialog.onlyNew = false
        compare(dialog.candidates.length, 2)
        dialog.selectAll(true)
        compare(dialog.selectedCount, 1)
        verify(!dialog.chosen["C:/old.CR3"])
        dialog.selectAll(false)
        compare(dialog.selectedCount, 0)
    }
}
