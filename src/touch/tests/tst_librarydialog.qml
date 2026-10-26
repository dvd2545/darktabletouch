import QtQuick
import QtQuick.Controls
import QtTest
import "../qml"

TestCase {
    id: tests
    name: "LibraryCollections"
    width: 960; height: 540
    visible: true; when: windowShown
    QtObject {
        id: mock
        property var library: ({folders: [
            {folder: "C:/Users/person/Pictures/trip", images: 2},
            {folder: "C:/Users/person/Pictures/trip/day one", images: 3},
            {folder: "D:/archive/trip", images: 4},
            {folder: "//server/photos/wedding", images: 5}
        ]})
        property var collectionValues: ({})
        property bool ready: true
        property bool busy: false
        function libraryCommand() {}
    }
    Component { id: dialogComponent; LibraryDialog { photoController: mock } }
    function createDialog() {
        const dialog = createTemporaryObject(dialogComponent, tests)
        dialog.browseProperty = 0
        dialog.draft = {collection: [], filtering: [], sort: []}
        dialog.syncFolders()
        return dialog
    }
    function test_importedRootsAndChildren() {
        const dialog = createDialog()
        const rows = dialog.tree()
        compare(rows.length, 4)
        compare(rows.filter(function(row) { return row.depth === 0 }).length, 3)
        verify(rows.every(function(row) { return ["trip", "day one", "wedding"].indexOf(row.label) >= 0 }))
        const parent = rows.find(function(row) { return row.key === "C:/Users/person/Pictures/trip" })
        compare(parent.count, 5); compare(parent.children, 1)
        compare(rows.find(function(row) { return row.label === "day one" }).depth, 1)
    }
    function test_distinctSelectionAndCollapse() {
        const dialog = createDialog()
        const key = "C:/Users/person/Pictures/trip"
        dialog.toggleFolder(key, true)
        compare(dialog.folderState(key), Qt.Checked)
        compare(dialog.folderState("D:/archive/trip"), Qt.Unchecked)
        compare(dialog.draft.collection.length, 1)
        compare(dialog.draft.collection[0].string, key + "*")
        dialog.toggleFolder(key + "/day one", false)
        compare(dialog.folderState(key), Qt.PartiallyChecked)
        compare(dialog.draft.collection[0].string, key)
        dialog.expanded = ({"C:/Users/person/Pictures/trip": false})
        compare(dialog.tree().length, 3)
        compare(dialog.tree(true).length, 4)
    }
    function test_searchUsesCollectionNames() {
        const dialog = createDialog()
        dialog.search = "person"
        compare(dialog.tree().length, 0)
        dialog.search = "day one"
        compare(dialog.tree().length, 2)
        compare(dialog.tree()[0].label, "trip")
        dialog.search = "wedding"
        compare(dialog.tree().length, 1)
        compare(dialog.tree()[0].label, "wedding")
        compare(dialog.tree(true).length, 4)
    }
}
