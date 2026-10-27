import QtQuick
import QtQuick.Controls
import QtTest
import "../qml"

TestCase {
    id: tests
    name: "RatingFilter"
    width: 960; height: 540
    visible: true; when: windowShown
    QtObject {
        id: mock
        property bool ready: true
        property bool busy: false
        property var library: ({})
        function libraryCommand(action, settings) {
            compare(action, "collection")
            library = Object.assign({}, library, settings)
        }
    }
    Component { id: ratingComponent; RatingFilter { photoController: mock } }
    function init() {
        mock.library = {rating_property: 21, rating_range_property: 22,
            collection: [{item: 0, string: "trip", mode: 0}], filtering: [], sort: [{item: 0, order: 0}]}
    }
    function test_independentChoicesAndOtherFilters() {
        const control = createTemporaryObject(ratingComponent, tests)
        control.setAllowed(-1, false)
        verify(!control.allowed(-1)); verify(control.allowed(0)); verify(control.allowed(5))
        control.setAllowed(2, false)
        verify(!control.allowed(2)); verify(control.allowed(1)); verify(control.allowed(3))
        compare(mock.library.collection[0].string, "trip")
        for(let value = 0; value <= 5; value++) control.setAllowed(value, false)
        for(let value = -1; value <= 5; value++) verify(!control.allowed(value))
        control.setAllowed(-1, true)
        verify(control.allowed(-1)); verify(!control.allowed(0))
        mock.library = Object.assign({}, mock.library, {filtering: mock.library.filtering.concat([{item: 3, string: "Canon", mode: 0, off: 0}])})
        control.setAllowed(5, true)
        verify(mock.library.filtering.some(function(rule) { return rule.item === 3 && rule.string === "Canon" }))
        verify(control.allowed(-1)); verify(control.allowed(5)); verify(!control.allowed(4))
    }
    function test_existingRangeFilter() {
        const control = createTemporaryObject(ratingComponent, tests)
        mock.library = Object.assign({}, mock.library, {filtering: [{item: 22, string: "[2;4]", mode: 0, off: 0}]})
        verify(control.allowed(2)); verify(control.allowed(4)); verify(!control.allowed(-1)); verify(!control.allowed(5))
        control.setAllowed(5, true)
        verify(control.allowed(2)); verify(control.allowed(5)); verify(!control.allowed(0))
    }
}
