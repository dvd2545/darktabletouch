import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: window
    visible: true
    width: 1280
    height: 720
    minimumWidth: 960
    minimumHeight: 540
    title: qsTr("darktabletouch")
    color: "#141619"
    font.family: "Segoe UI"
    font.pixelSize: 15
    palette.window: "#141619"
    palette.windowText: "#e6e8eb"
    palette.base: "#202329"
    palette.text: "#e6e8eb"
    palette.button: "#292d34"
    palette.buttonText: "#e6e8eb"
    palette.highlight: "#a9d5bc"
    palette.highlightedText: "#101915"
    property real viewZoom: 1
    property int thumbnailRevision: 0
    property bool advanced: false

    function showHistory() { historyDialog.open() }
    function showQuickMenu() { historyDialog.close(); quickMenu.open() }

    onClosing: function(close) {
        if(!controller.canClose) { close.accepted = false; saveView(); controller.shutdown() }
    }
    Connections {
        target: controller
        function onClosingReady() { window.close() }
        function onThumbnailChanged(id) { window.thumbnailRevision++ }
        function onViewRestored(zoom, centerX, centerY) {
            window.viewZoom = Math.max(1, Math.min(128, zoom))
            Qt.callLater(function() {
                canvas.contentX = (canvas.contentWidth - photo.paintedWidth) / 2 + centerX * photo.paintedWidth - canvas.width / 2
                canvas.contentY = (canvas.contentHeight - photo.paintedHeight) / 2 + centerY * photo.paintedHeight - canvas.height / 2
                clampPan()
            })
        }
    }

    function saveView() {
        const x = photo.paintedWidth > 0 ? (canvas.contentX + canvas.width / 2 - (canvas.contentWidth - photo.paintedWidth) / 2) / photo.paintedWidth : 0.5
        const y = photo.paintedHeight > 0 ? (canvas.contentY + canvas.height / 2 - (canvas.contentHeight - photo.paintedHeight) / 2) / photo.paintedHeight : 0.5
        controller.saveView(viewZoom, Math.max(0, Math.min(1, x)), Math.max(0, Math.min(1, y)))
    }

    function clampPan() {
        canvas.contentX = Math.max(0, Math.min(canvas.contentX, canvas.contentWidth - canvas.width))
        canvas.contentY = Math.max(0, Math.min(canvas.contentY, canvas.contentHeight - canvas.height))
    }
    function zoomBy(amount) {
        viewZoom = Math.max(1, Math.min(8, viewZoom * amount))
        clampPan()
    }

    Connections {
        target: gamepad
        function onAction(name, pressed) {
            if(name === "compare") controller.before = pressed
            if(!pressed) return
            if(name === "previous") controller.navigateImage(-1)
            else if(name === "next") controller.navigateImage(1)
            else if(name === "library") controller.editorVisible = !controller.editorVisible
            else if(name === "search") toolSearch.open()
            else if(name === "menu") quickMenu.open()
        }
        function onPan(delta) {
            if(controller.editorVisible) { canvas.contentX += delta.x * 12; canvas.contentY += delta.y * 12; clampPan() }
        }
        function onZoom(delta) { if(controller.editorVisible) zoomBy(1 + delta * 0.025) }
    }

    Shortcut { sequences: [StandardKey.Undo]; onActivated: controller.undo() }
    Shortcut { sequences: [StandardKey.Redo]; onActivated: controller.redo() }
    Shortcut { sequence: "Escape"; onActivated: controller.editorVisible = false }

    header: Rectangle {
        height: 80
        color: "#1b1e23"
        RowLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 8
            Rectangle {
                Layout.preferredWidth: 48; Layout.preferredHeight: 48; radius: 14; color: "#a9d5bc"
                Text { anchors.centerIn: parent; text: qsTr("dt"); color: "#142019"; font.pixelSize: 25; font.weight: Font.DemiBold }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label { text: qsTr("darktabletouch"); font.pixelSize: 19; font.weight: Font.DemiBold; color: "#f0f1f3" }
                Label { text: qsTr("your photos, within reach"); font.pixelSize: 12; color: "#9da6b3" }
            }
            TouchButton { text: qsTr("library"); checked: !controller.editorVisible; onClicked: controller.editorVisible = false }
            TouchButton { text: qsTr("editor"); checked: controller.editorVisible; enabled: controller.imageId > 0; onClicked: controller.editorVisible = true }
            TouchButton { text: qsTr("undo"); visible: window.width >= 1150; enabled: controller.canUndo; onClicked: controller.undo() }
            TouchButton { text: qsTr("redo"); visible: window.width >= 1150; enabled: controller.canRedo; onClicked: controller.redo() }
            TouchButton { text: qsTr("history"); visible: window.width >= 1150; enabled: controller.imageId > 0; onClicked: showHistory() }
            TouchButton {
                text: qsTr("before")
                enabled: controller.imageId > 0
                onPressed: controller.before = true
                onReleased: controller.before = false
                onCanceled: controller.before = false
            }
            TouchButton { text: qsTr("export"); enabled: controller.imageId > 0; onClicked: exportDialog.open() }
            TouchButton { text: qsTr("menu"); onClicked: showQuickMenu() }
        }
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: controller.editorVisible ? 1 : 0
        ColumnLayout {
            spacing: 16
            RowLayout {
                Layout.fillWidth: true; Layout.margins: 16
                ColumnLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("photo library"); color: "#eceef0"; font.pixelSize: 28; font.weight: Font.DemiBold }
                    Label { text: controller.images.length === 1 ? qsTr("1 photo") : qsTr("%1 photos").arg(controller.images.length.toLocaleString(Qt.locale("en_US"), "f", 0)); color: "#9da6b3" }
                }
                TouchButton { text: qsTr("import folder"); enabled: controller.ready; onClicked: folderDialog.open() }
                TouchButton { text: qsTr("import photos"); enabled: controller.ready; onClicked: importDialog.open() }
            }
            GridView {
                id: library
                Layout.fillWidth: true; Layout.fillHeight: true; Layout.leftMargin: 16; Layout.rightMargin: 16
                clip: true
                cellWidth: Math.max(200, width / Math.max(1, Math.floor(width / 240)))
                cellHeight: cellWidth * 0.80
                model: controller.images
                cacheBuffer: cellHeight
                delegate: Item {
                    required property var modelData
                    width: library.cellWidth; height: library.cellHeight
                    TouchButton {
                        anchors.fill: parent; anchors.margins: 6; text: ""
                        onClicked: { viewZoom = 1; controller.selectImage(modelData.imgid) }
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 10; spacing: 8
                            Image {
                                Layout.fillWidth: true; Layout.fillHeight: true
                                source: "image://photos/thumb/" + modelData.imgid + "?revision=" + window.thumbnailRevision
                                fillMode: Image.PreserveAspectFit; asynchronous: true; cache: false
                            }
                            Label { text: modelData.filename; Layout.fillWidth: true; elide: Text.ElideMiddle; font.pixelSize: 14; color: "#e2e5e9" }
                        }
                    }
                    Component.onCompleted: controller.requestThumbnail(modelData.imgid)
                    Connections {
                        target: controller
                        function onStateChanged() { if(controller.ready) controller.requestThumbnail(modelData.imgid) }
                    }
                }
                Label {
                    anchors.centerIn: parent
                    visible: controller.ready && controller.images.length === 0
                    text: qsTr("a little space for your next favorite photo\nimport a folder or drop photos here")
                    horizontalAlignment: Text.AlignHCenter; lineHeight: 1.8; color: "#9da6b3"; font.pixelSize: 20
                }
                DropArea { anchors.fill: parent; onDropped: function(drop) { controller.importUrls(drop.urls) } }
                ScrollBar.vertical: ScrollBar { }
            }
        }
        RowLayout {
            spacing: 0
            Item {
                Layout.fillWidth: true; Layout.fillHeight: true
                Flickable {
                    id: canvas
                    anchors.fill: parent; anchors.margins: 16
                    clip: true; boundsBehavior: Flickable.StopAtBounds
                    contentWidth: width * window.viewZoom
                    contentHeight: height * window.viewZoom
                    Image {
                        id: photo
                        width: canvas.contentWidth; height: canvas.contentHeight
                        source: controller.preview
                        fillMode: Image.PreserveAspectFit; asynchronous: true; cache: false
                    }
                    PinchHandler {
                        target: null
                        property real startZoom: 1
                        onActiveChanged: if(active) startZoom = window.viewZoom
                        onActiveScaleChanged: window.viewZoom = Math.max(1, Math.min(8, startZoom * activeScale))
                    }
                    TapHandler { onDoubleTapped: { window.viewZoom = window.viewZoom > 1 ? 1 : 2; clampPan() } }
                    WheelHandler { onWheel: function(event) { zoomBy(event.angleDelta.y > 0 ? 1.1 : 1 / 1.1) } }
                }
                Row {
                    anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 24; spacing: 8
                    TouchButton { text: qsTr("previous"); onClicked: controller.navigateImage(-1) }
                    TouchButton { text: qsTr("fit"); onClicked: { window.viewZoom = 1; clampPan() } }
                    TouchButton { text: qsTr("next"); onClicked: controller.navigateImage(1) }
                }
            }
            Rectangle {
                Layout.preferredWidth: Math.max(300, Math.min(380, window.width * 0.29))
                Layout.fillHeight: true; color: "#1d2026"
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 16; spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: controller.toolLabel; Layout.fillWidth: true; elide: Text.ElideRight; color: "#f0f1f3"; font.pixelSize: 21; font.weight: Font.DemiBold }
                        TouchButton { text: qsTr("more"); checkable: true; checked: window.advanced; onClicked: window.advanced = !window.advanced }
                        TouchButton { text: qsTr("reset"); onClicked: controller.reset() }
                    }
                    Switch { text: qsTr("enabled"); checked: controller.moduleEnabled; implicitHeight: 64; onToggled: controller.setEnabled(checked) }
                    ListView {
                        id: controls
                        Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 12
                        model: controller.fields
                        delegate: FieldControl {
                            required property var model
                            field: model.field
                            width: controls.width
                            visible: controller.operation !== "exposure" || window.advanced
                                || ["exposure", "black", "mode"].indexOf(field.name) >= 0
                                || (Number(controller.values.mode) === 1 && field.name.startsWith("deflicker_"))
                            height: visible ? implicitHeight : 0
                        }
                        ScrollBar.vertical: ScrollBar { }
                    }
                }
            }
        }
    }

    footer: ColumnLayout {
        spacing: 0
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 88; visible: controller.editorVisible
            color: "#1b1e23"
            RowLayout {
                anchors.fill: parent; anchors.margins: 10; spacing: 8
                Repeater {
                    model: controller.tools
                    TouchButton {
                        required property var modelData
                        Layout.fillWidth: true; Layout.preferredWidth: 1
                        text: modelData.label
                        checked: controller.operation === modelData.operation
                        enabled: modelData.available && controller.imageId > 0
                        onClicked: controller.selectTool(modelData.operation)
                    }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 32; color: "#101215"
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16
                BusyIndicator { running: controller.busy; implicitWidth: 24; implicitHeight: 24; visible: running }
                Label { text: controller.message; Layout.fillWidth: true; elide: Text.ElideMiddle; color: "#a8b0bc"; font.pixelSize: 12 }
                Label { text: gamepad.connected ? qsTr("A select   B back   X before   Y tools   LB/RB photos") : qsTr("touch, keyboard, and mouse"); color: "#7f8b9c"; font.pixelSize: 12 }
            }
        }
    }

    FileDialog {
        id: importDialog
        title: qsTr("import photos")
        fileMode: FileDialog.OpenFiles
        options: FileDialog.DontUseNativeDialog
        onAccepted: controller.importUrls(selectedFiles)
    }
    FolderDialog {
        id: folderDialog
        title: qsTr("import a photo folder")
        options: FolderDialog.DontUseNativeDialog
        onAccepted: controller.importUrls([selectedFolder])
    }
    FileDialog {
        id: exportDialog
        title: qsTr("export photo")
        fileMode: FileDialog.SaveFile
        options: FileDialog.DontUseNativeDialog
        nameFilters: [qsTr("JPEG image (*.jpg)"), qsTr("PNG image (*.png)"), qsTr("TIFF image (*.tif)")]
        defaultSuffix: "jpg"
        onAccepted: {
            const path = selectedFile.toString().toLowerCase()
            controller.exportTo(selectedFile, path.endsWith(".png") ? "png" : path.endsWith(".tif") || path.endsWith(".tiff") ? "tiff" : "jpeg")
        }
    }
    Popup {
        id: quickMenu
        anchors.centerIn: parent
        width: 420; height: Math.min(600, window.height - 48); padding: 24; modal: true; focus: true
        background: Rectangle { color: "#24272d"; radius: 20; border.color: "#424951" }
        ColumnLayout {
            anchors.fill: parent; spacing: 12
            Label { text: qsTr("quick actions"); color: "#f0f1f3"; font.pixelSize: 24 }
            ScrollView {
                Layout.fillWidth: true; Layout.fillHeight: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width; spacing: 12
                    TouchButton { text: qsTr("import photos"); Layout.fillWidth: true; onClicked: { quickMenu.close(); importDialog.open() } }
                    TouchButton { text: qsTr("undo"); Layout.fillWidth: true; enabled: controller.canUndo; onClicked: { quickMenu.close(); controller.undo() } }
                    TouchButton { text: qsTr("redo"); Layout.fillWidth: true; enabled: controller.canRedo; onClicked: { quickMenu.close(); controller.redo() } }
                    TouchButton { text: qsTr("edit history"); Layout.fillWidth: true; enabled: controller.imageId > 0; onClicked: { quickMenu.close(); showHistory() } }
                    TouchButton {
                        text: qsTr("switch to classic"); Layout.fillWidth: true; visible: managedSession; enabled: controller.ready
                        onClicked: { quickMenu.close(); saveView(); controller.switchInterface("classic") }
                    }
                    TouchButton { text: qsTr("close menu"); Layout.fillWidth: true; onClicked: quickMenu.close() }
                }
            }
        }
    }
    Popup {
        id: historyDialog
        anchors.centerIn: parent
        width: Math.min(560, window.width - 48); height: Math.min(600, window.height - 48)
        padding: 20; modal: true; focus: true
        background: Rectangle { color: "#24272d"; radius: 20; border.color: "#424951" }
        onOpened: {
            historySteps.currentIndex = controller.historyEnd
            historySteps.positionViewAtIndex(controller.historyEnd, ListView.Contain)
            Qt.callLater(function() {
                const item = historySteps.itemAtIndex(historySteps.currentIndex)
                if(item) item.forceActiveFocus()
            })
        }
        ColumnLayout {
            anchors.fill: parent; spacing: 12
            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("edit history"); color: "#f0f1f3"; font.pixelSize: 24; Layout.fillWidth: true }
                TouchButton { text: qsTr("close"); onClicked: historyDialog.close() }
            }
            Label { text: qsTr("choose a step; undo returns to your previous edit"); wrapMode: Text.Wrap; Layout.fillWidth: true; color: "#aeb4be" }
            ListView {
                id: historySteps
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 8
                model: [{index: 0, label: qsTr("original"), enabled: true}].concat(controller.history)
                delegate: TouchButton {
                    required property var modelData
                    required property int index
                    width: historySteps.width
                    text: modelData.index + "  " + modelData.label + (modelData.enabled ? "" : qsTr(" (disabled)"))
                    checked: controller.historyEnd === modelData.index
                    opacity: modelData.index > controller.historyEnd ? 0.5 : 1
                    onClicked: { controller.jumpHistory(modelData.index); historyDialog.close() }
                    onActiveFocusChanged: if(activeFocus) { historySteps.currentIndex = index; historySteps.positionViewAtIndex(index, ListView.Contain) }
                    Keys.onPressed: function(event) {
                        const direction = event.key === Qt.Key_Tab || event.key === Qt.Key_Down ? 1 : event.key === Qt.Key_Backtab || event.key === Qt.Key_Up ? -1 : 0
                        const next = index + direction
                        if(direction && next >= 0 && next < historySteps.count) {
                            event.accepted = true
                            historySteps.currentIndex = next
                            historySteps.positionViewAtIndex(next, ListView.Contain)
                            Qt.callLater(function() { const item = historySteps.itemAtIndex(next); if(item) item.forceActiveFocus() })
                        }
                    }
                }
                ScrollBar.vertical: ScrollBar { }
            }
        }
    }
    Popup {
        id: toolSearch
        anchors.centerIn: parent
        width: 540; height: Math.min(560, window.height - 80); padding: 24; modal: true; focus: true
        background: Rectangle { color: "#24272d"; radius: 20; border.color: "#424951" }
        ColumnLayout {
            anchors.fill: parent; spacing: 12
            Label { text: qsTr("editing tools"); color: "#f0f1f3"; font.pixelSize: 24 }
            ListView {
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 8
                model: controller.tools
                delegate: TouchButton {
                    required property var modelData
                    text: modelData.label; width: ListView.view.width; enabled: controller.imageId > 0 && modelData.available
                    onClicked: { controller.selectTool(modelData.operation); toolSearch.close() }
                }
            }
        }
    }
}
