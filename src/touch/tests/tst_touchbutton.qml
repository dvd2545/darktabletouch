import QtQuick
import QtQuick.Controls
import QtTest
import "../qml"

TestCase {
    id: testCase
    name: "TouchButton"
    width: 400
    height: 240
    visible: true
    when: windowShown

    Component {
        id: buttonComponent
        TouchButton { text: "quick actions"; iconName: "menu" }
    }

    function iconItem(button) {
        for(const child of button.contentItem.children)
            if(child.name !== undefined) return child
        fail("button icon is unavailable")
    }

    function test_iconCentered_data() {
        return [
            { tag: "default", width: 64, height: 64, checked: false, enabled: true },
            { tag: "wide", width: 128, height: 64, checked: false, enabled: true },
            { tag: "tall", width: 64, height: 96, checked: false, enabled: true },
            { tag: "checked", width: 64, height: 64, checked: true, enabled: true },
            { tag: "disabled", width: 64, height: 64, checked: false, enabled: false }
        ]
    }

    function test_iconCentered(data) {
        const button = createTemporaryObject(buttonComponent, testCase, {
            showLabel: false, width: data.width, height: data.height, checked: data.checked, enabled: data.enabled
        })
        verify(button)
        verify(waitForPolish(button))
        const icon = iconItem(button)
        compare(icon.width, 24)
        compare(icon.height, 24)
        const center = icon.mapToItem(button, icon.width / 2, icon.height / 2)
        fuzzyCompare(center.x, button.width / 2, 0.5, "icon horizontal center")
        fuzzyCompare(center.y, button.height / 2, 0.5, "icon vertical center")
    }

    function test_defaultLabel() {
        const button = createTemporaryObject(buttonComponent, testCase)
        verify(button)
        verify(button.showLabel)
        verify(waitForPolish(button))
        const label = button.contentItem.children[1]
        verify(label.visible)
        compare(label.text, button.text)
        verify(label.width >= label.implicitWidth)
        button.stacked = true
        button.width = 100
        button.text = "more"
        verify(waitForPolish(button))
        const icon = iconItem(button)
        tryVerify(function() { return icon.mapToItem(button, 0, icon.height).y <= label.mapToItem(button, 0, 0).y })
        verify(label.width > 0)
    }

    function test_labelToggle() {
        const button = createTemporaryObject(buttonComponent, testCase, { width: 240, showLabel: true })
        verify(button)
        verify(waitForPolish(button))
        const icon = iconItem(button)
        let label = null
        for(const child of button.contentItem.children)
            if(child.text !== undefined) label = child
        verify(label)
        verify(label.visible)
        verify(label.width >= label.implicitWidth)
        verify(icon.mapToItem(button, icon.width, 0).x <= label.mapToItem(button, 0, 0).x)
        button.showLabel = false
        verify(waitForPolish(button))
        verify(!label.visible)
        tryVerify(function() {
            return Math.abs(icon.mapToItem(button, icon.width / 2, icon.height / 2).x - button.width / 2) <= 0.5
        }, 1000, "icon recenters after hiding label")
        const center = icon.mapToItem(button, icon.width / 2, icon.height / 2)
        fuzzyCompare(center.x, button.width / 2, 0.5, "icon recenters after hiding label")
        fuzzyCompare(center.y, button.height / 2, 0.5)
        button.iconName = ""
        verify(waitForPolish(button))
        verify(label.visible)
        verify(!icon.visible)
        compare(label.text, button.text)
    }
}
