import QtQuick
import QtQuick.Controls
import QtTest
import "../qml"

TestCase {
    id: tests
    name: "FieldControl"
    width: 400; height: 260
    visible: true; when: windowShown
    QtObject {
        id: mock
        property var edits: []
        property int commits: 0
        property int clicks: 0
        property int holds: 0
        function setParameter(name, value, commit, operation) { edits = edits.concat([value]); if(commit) ++commits }
        function rememberParameter(name, operation, step) {}
    }
    Component {
        id: sceneComponent
        Flickable {
            width: 360; height: 220; contentHeight: 700; clip: true
            boundsBehavior: Flickable.StopAtBounds
            FieldControl {
                y: 24; width: parent.width
                property var controller: mock
                field: ({name: "gain", label: "long parameter name without truncation", type: "float", value: 0.5, default: 0, min: 0, max: 1, step: 0.01})
            }
        }
    }
    Component {
        id: thumbnailSceneComponent
        Flickable {
            width: 360; height: 220; contentHeight: 700; clip: true
            ThumbnailButton {
                objectName: "photo"; y: 30; width: 300; height: 160
                onClicked: ++mock.clicks
                onPressAndHold: ++mock.holds
            }
        }
    }
    function test_thumbnailTouchScroll() {
        const scene = createTemporaryObject(thumbnailSceneComponent, tests)
        const photo = findChild(scene, "photo")
        const touch = touchEvent(scene)
        touch.press(0, photo, 100, 130).commit(); wait(40)
        touch.move(0, photo, 100, 90).commit(); wait(40)
        touch.move(0, photo, 100, 20).commit(); wait(40)
        touch.release(0, photo, 100, 20).commit(); wait(40)
        verify(scene.contentY > 0)
        compare(mock.clicks, 0)
        compare(mock.holds, 0)
    }
    function test_thumbnailHold() {
        const scene = createTemporaryObject(thumbnailSceneComponent, tests)
        const photo = findChild(scene, "photo")
        const touch = touchEvent(scene)
        touch.press(0, photo, 100, 80).commit()
        wait(750)
        touch.release(0, photo, 100, 80).commit(); wait(40)
        compare(mock.holds, 1)
        compare(mock.clicks, 0)
    }
    function init() { mock.edits = []; mock.commits = 0; mock.clicks = 0; mock.holds = 0 }
    function test_directReset() {
        const scene = createTemporaryObject(sceneComponent, tests)
        verify(waitForPolish(scene))
        const reset = findChild(scene, "reset-gain")
        verify(reset.visible)
        mouseClick(reset)
        compare(mock.edits.length, 1)
        compare(mock.edits[0], 0)
    }
    function test_horizontalTouchAdjustment() {
        const scene = createTemporaryObject(sceneComponent, tests)
        verify(waitForPolish(scene))
        const slider = findChild(scene, "parameter-gain")
        const touch = touchEvent(scene)
        touch.press(0, slider, slider.width / 2, slider.height / 2).commit(); wait(40)
        touch.move(0, slider, slider.width * 0.8, slider.height / 2).commit(); wait(40)
        touch.release(0, slider, slider.width * 0.8, slider.height / 2).commit(); wait(40)
        compare(mock.commits, 1, "one slider gesture creates one committed edit")
        verify(mock.edits[mock.edits.length - 1] > 0.7)
        compare(scene.contentY, 0)
    }
    function test_tapAdjustment() {
        const scene = createTemporaryObject(sceneComponent, tests)
        verify(waitForPolish(scene))
        const slider = findChild(scene, "parameter-gain")
        const touch = touchEvent(scene)
        touch.press(0, slider, slider.width * 0.8, slider.height / 2).commit(); wait(40)
        touch.release(0, slider, slider.width * 0.8, slider.height / 2).commit(); wait(40)
        compare(mock.commits, 1)
        verify(mock.edits[mock.edits.length - 1] > 0.7)
    }
    function test_verticalTouchScroll() {
        const scene = createTemporaryObject(sceneComponent, tests)
        verify(waitForPolish(scene))
        const slider = findChild(scene, "parameter-gain")
        const touch = touchEvent(scene)
        touch.press(0, slider, slider.width * 0.7, slider.height / 2).commit(); wait(40)
        touch.move(0, slider, slider.width * 0.7, slider.height / 2 - 30).commit(); wait(40)
        touch.move(0, slider, slider.width * 0.7, slider.height / 2 - 90).commit(); wait(40)
        touch.release(0, slider, slider.width * 0.7, slider.height / 2 - 90).commit(); wait(40)
        verify(scene.contentY > 0, "scroll=" + scene.contentY + " edits=" + JSON.stringify(mock.edits) + " slider=" + slider.width + "x" + slider.height)
        compare(mock.edits.length, 0, "scrolling over a slider must not edit its value")
    }
}
