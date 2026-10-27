import QtQuick
import QtQuick.Controls
import QtTest
import "../qml"

TestCase {
    id: tests
    name: "ThumbnailInput"
    width: 400; height: 240
    visible: true; when: windowShown
    ThumbnailButton { id: thumbnail; width: 140; height: 100 }
    SignalSpy { id: clicks; target: thumbnail; signalName: "clicked" }
    SignalSpy { id: holds; target: thumbnail; signalName: "pressAndHold" }
    TouchPopup { id: popup; parent: Overlay.overlay; x: 180; y: 100; width: 180; height: 100 }
    function init() { thumbnail.inputBlocked = false; clicks.clear(); holds.clear() }
    function cleanup() { popup.close(); tryCompare(popup, "visible", false) }
    function test_touchTapAndHold() {
        const touch = touchEvent(thumbnail)
        touch.press(0, thumbnail, 50, 40).commit()
        wait(50)
        touch.release(0, thumbnail, 50, 40).commit()
        compare(clicks.count, 1); compare(holds.count, 0)
        clicks.clear()
        touch.press(0, thumbnail, 50, 40).commit()
        tryCompare(holds, "count", 1)
        touch.release(0, thumbnail, 50, 40).commit()
        compare(clicks.count, 0)
    }
    function test_dragDoesNotOpen() {
        const touch = touchEvent(thumbnail)
        touch.press(0, thumbnail, 50, 40).commit()
        touch.move(0, thumbnail, 110, 40).commit()
        wait(750)
        touch.release(0, thumbnail, 110, 40).commit()
        compare(clicks.count, 0); compare(holds.count, 0)
    }
    function test_rightClick() {
        mouseClick(thumbnail, 50, 40, Qt.RightButton)
        compare(clicks.count, 0); compare(holds.count, 1)
    }
    function test_modalBlocksAndDismisses() {
        thumbnail.inputBlocked = Qt.binding(function() { return popup.visible })
        popup.open(); tryCompare(popup, "opened", true)
        verify(popup.modal)
        mouseClick(thumbnail, 50, 40)
        tryCompare(popup, "visible", false)
        compare(clicks.count, 0); compare(holds.count, 0)
        mouseClick(thumbnail, 50, 40)
        compare(clicks.count, 1)
    }
}
