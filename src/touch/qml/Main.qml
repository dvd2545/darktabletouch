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
    color: "#090d0f"
    font.family: "Segoe UI"
    font.pixelSize: 15
    palette.window: "#090d0f"
    palette.windowText: "#e6e8eb"
    palette.base: "#202329"
    palette.text: "#e6e8eb"
    palette.button: "#292d34"
    palette.buttonText: "#e6e8eb"
    palette.highlight: "#00dbe5"
    palette.highlightedText: "#07191b"
    property real viewZoom: 1
    property int thumbnailRevision: 0
    property bool advanced: false
    property bool selectionMode: false
    property bool importRecursive: true
    readonly property var selectedImages: controller.library.selected || []
    property string expandedTool: "exposure"
    property bool informationVisible: false
    property bool statusVisible: true
    readonly property bool compact: height < 650
    property real requestedFilmstripHeight: 0
    property real requestedModuleWidth: 0
    readonly property real maximumFilmstripHeight: Math.min(300, height * 0.45)
    readonly property real maximumModuleWidth: Math.min(640, width - 400)

    function resizeFilmstrip(delta) {
        requestedFilmstripHeight = Math.max(112, Math.min(maximumFilmstripHeight, filmstripBar.height + delta))
        Qt.callLater(clampPan)
    }
    function resizeModules(delta) {
        requestedModuleWidth = Math.max(340, Math.min(maximumModuleWidth, toolPanel.width + delta))
        Qt.callLater(clampPan)
    }

    function showImageInformation() {
        informationVisible = true
        informationTimer.restart()
    }
    function currentImageIndex() {
        for(let i = 0; i < controller.images.length; ++i)
            if(controller.images[i].imgid === controller.imageId) return i
        return -1
    }
    function updateFilmstrip() {
        filmstrip.currentIndex = currentImageIndex()
        if(filmstrip.currentIndex >= 0)
            filmstrip.positionViewAtIndex(filmstrip.currentIndex, ListView.Contain)
    }
    function cameraDescription() {
        const camera = controller.imageInformation.camera || {}
        return [camera.model, camera.lens].filter(function(value) { return value && value.length }).join("  |  ")
    }
    function exposureDescription() {
        const exif = controller.imageInformation.exif || {}
        const locale = Qt.locale("en_US")
        const values = []
        if(exif.focal_length > 0) values.push(Number(exif.focal_length).toLocaleString(locale, "g", 4) + " mm")
        if(exif.aperture > 0) values.push("f/" + Number(exif.aperture).toLocaleString(locale, "g", 3))
        if(exif.exposure > 0) values.push((exif.exposure < 1 ? "1/" + Math.round(1 / exif.exposure).toLocaleString(locale, "f", 0) : Number(exif.exposure).toLocaleString(locale, "g", 3)) + " s")
        if(exif.iso > 0) values.push("ISO " + Number(exif.iso).toLocaleString(locale, "f", 0))
        return values.join("  |  ")
    }
    function revealItem(item, column, flickable) {
        if(!item) return
        let ancestor = item.parent
        while(ancestor && ancestor !== column) ancestor = ancestor.parent
        if(!ancestor) return
        const top = item.mapToItem(column, 0, 0).y
        if(top < flickable.contentY) flickable.contentY = top
        else if(top + item.height > flickable.contentY + flickable.height)
            flickable.contentY = Math.min(top + item.height - flickable.height, Math.max(0, flickable.contentHeight - flickable.height))
    }
    onActiveFocusItemChanged: {
        revealItem(activeFocusItem, toolColumn, toolScroll)
        if(quickMenu.opened) revealItem(activeFocusItem, quickColumn, quickScroll.contentItem)
        if(collectionDialog.opened) revealItem(activeFocusItem, collectionDialog.focusColumn, collectionDialog.focusViewport)
    }

    Timer { id: informationTimer; interval: 4000; onTriggered: window.informationVisible = false }
    Timer { id: statusTimer; interval: 3500; onTriggered: window.statusVisible = false }

    function showHistory() { historyDialog.open() }
    function showQuickMenu() { historyDialog.close(); toolActions.close(); toolSearch.close(); collectionDialog.close(); importReview.close(); importSource.close(); quickMenu.open() }

    onClosing: function(close) {
        if(!controller.canClose) { close.accepted = false; saveView(); controller.shutdown() }
    }
    Connections {
        target: controller
        function onClosingReady() { window.close() }
        function onThumbnailChanged(id) { window.thumbnailRevision++ }
        function onImageInformationChanged() { showImageInformation(); updateFilmstrip() }
        function onImagesChanged() { Qt.callLater(updateFilmstrip) }
        function onImportReviewChanged() { importReview.open() }
        function onModuleChanged() { window.expandedTool = controller.operation }
        function onStateChanged() { window.statusVisible = true; statusTimer.restart() }
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
            else if(name === "menu") showQuickMenu()
        }
        function onPan(delta) {
            if(controller.editorVisible) { canvas.contentX += delta.x * 12; canvas.contentY += delta.y * 12; clampPan() }
        }
        function onZoom(delta) { if(controller.editorVisible) zoomBy(1 + delta * 0.025) }
    }

    Shortcut { sequences: [StandardKey.Undo]; onActivated: controller.undo() }
    Shortcut { sequences: [StandardKey.Redo]; onActivated: controller.redo() }
    Shortcut {
        sequence: "Escape"
        enabled: !quickMenu.opened && !historyDialog.opened && !toolSearch.opened && !toolActions.opened
            && !importDialog.visible && !folderDialog.visible && !exportDialog.visible && !collectionDialog.opened && !importReview.opened && !importSource.opened
        onActivated: { if(controller.before) controller.before = false; else if(!controller.editorVisible && window.selectionMode) window.selectionMode = false; else controller.editorVisible = false }
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: controller.editorVisible ? 1 : 0
        ColumnLayout {
            spacing: 16
            RowLayout {
                Layout.fillWidth: true; Layout.margins: 16
                TouchButton { text: "\u2630"; font.pixelSize: 28; Accessible.name: qsTr("quick actions"); onClicked: showQuickMenu() }
                ColumnLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("photo library"); color: "#eceef0"; font.pixelSize: 28; font.weight: Font.DemiBold }
                    Label { text: controller.images.length === 1 ? qsTr("1 photo") : qsTr("%1 photos").arg(controller.images.length.toLocaleString(Qt.locale("en_US"), "f", 0)); color: "#9da6b3" }
                }
                TouchButton { text: qsTr("collections"); enabled: controller.ready && !controller.busy; onClicked: collectionDialog.open() }
                TouchButton { text: window.selectionMode ? qsTr("done") : qsTr("select"); checked: window.selectionMode; onClicked: window.selectionMode = !window.selectionMode }
                TouchButton { text: qsTr("import"); enabled: controller.ready && !controller.busy; onClicked: importSource.open() }
            }
            RowLayout {
                visible: window.selectionMode
                Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16
                Label { text: qsTr("%1 selected").arg(window.selectedImages.length); color: "#00dbe5" }
                TouchButton { text: qsTr("all"); enabled: !controller.busy; onClicked: controller.libraryCommand("all", {}, 0) }
                TouchButton { text: qsTr("none"); enabled: !controller.busy; onClicked: controller.libraryCommand("none", {}, 0) }
                TouchButton { text: qsTr("invert"); enabled: !controller.busy; onClicked: controller.libraryCommand("invert", {}, 0) }
                TouchButton { text: qsTr("same film roll"); enabled: !controller.busy && window.selectedImages.length > 0; onClicked: controller.libraryCommand("film", {}, 0) }
                TouchButton { text: qsTr("untouched"); enabled: !controller.busy; onClicked: controller.libraryCommand("untouched", {}, 0) }
                Item { Layout.fillWidth: true }
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
                    required property int index
                    width: library.cellWidth; height: library.cellHeight
                    TouchButton {
                        anchors.fill: parent; anchors.margins: 6; text: ""
                        enabled: controller.ready && !controller.busy
                        Accessible.name: modelData.filename
                        checked: window.selectionMode && window.selectedImages.indexOf(modelData.imgid) >= 0
                        onClicked: {
                            if(window.selectionMode) controller.libraryCommand("toggle", {}, modelData.imgid)
                            else { viewZoom = 1; controller.selectImage(modelData.imgid) }
                        }
                        onPressAndHold: {
                            window.selectionMode = true
                            controller.libraryCommand("toggle", {}, modelData.imgid)
                        }
                        onActiveFocusChanged: if(activeFocus) { library.currentIndex = index; library.positionViewAtIndex(index, GridView.Contain) }
                        Keys.onPressed: function(event) {
                            if(window.selectionMode && (event.key === Qt.Key_Space || event.key === Qt.Key_Return)
                               && (event.modifiers & Qt.ShiftModifier)) {
                                event.accepted = true
                                controller.libraryCommand("range", {}, modelData.imgid)
                                return
                            }
                            const columns = Math.max(1, Math.round(library.width / library.cellWidth))
                            const direction = event.key === Qt.Key_Down ? columns : event.key === Qt.Key_Up ? -columns
                                : event.key === Qt.Key_Tab || event.key === Qt.Key_Right ? 1
                                : event.key === Qt.Key_Backtab || event.key === Qt.Key_Left ? -1 : 0
                            const next = index + direction
                            if(direction && next >= 0 && next < library.count) {
                                event.accepted = true
                                library.currentIndex = next
                                library.positionViewAtIndex(next, GridView.Contain)
                                Qt.callLater(function() { const item = library.itemAtIndex(next); if(item) item.children[0].forceActiveFocus() })
                            }
                        }
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 10; spacing: 8
                            Image {
                                Layout.fillWidth: true; Layout.fillHeight: true
                                source: "image://photos/thumb/" + modelData.imgid + "?revision=" + window.thumbnailRevision
                                fillMode: Image.PreserveAspectFit; asynchronous: true; cache: false
                            }
                            Label { text: modelData.filename; textFormat: Text.PlainText; Layout.fillWidth: true; elide: Text.ElideMiddle; font.pixelSize: 14; color: window.selectionMode && window.selectedImages.indexOf(modelData.imgid) >= 0 ? "#07191b" : "#e2e5e9" }
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
                    text: qsTr("no photos in this collection\nimport photos or change collections and filters")
                    horizontalAlignment: Text.AlignHCenter; lineHeight: 1.8; color: "#9da6b3"; font.pixelSize: 20
                }
                DropArea { anchors.fill: parent; onDropped: function(drop) { controller.reviewImport(drop.urls, window.importRecursive) } }
                ScrollBar.vertical: ScrollBar { }
            }
        }
        RowLayout {
            spacing: 0
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
                Item {
                    id: imageStage
                    objectName: "imageStage"
                    Layout.fillWidth: true; Layout.fillHeight: true
                    Flickable {
                        id: canvas
                        anchors.fill: parent
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
                        TapHandler {
                            onTapped: showImageInformation()
                            onDoubleTapped: { window.viewZoom = window.viewZoom > 1 ? 1 : 2; clampPan() }
                        }
                        WheelHandler { onWheel: function(event) { zoomBy(event.angleDelta.y > 0 ? 1.1 : 1 / 1.1) } }
                    }
                    Rectangle {
                        id: imageInformation
                        objectName: "imageInformation"
                        anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 20
                        width: Math.min(520, parent.width - 40); height: informationText.implicitHeight + 28
                        color: "#cf101619"; radius: 12
                        opacity: window.informationVisible && controller.imageId > 0 ? 1 : 0
                        visible: opacity > 0
                        Behavior on opacity { NumberAnimation { duration: 450 } }
                        Column {
                            id: informationText
                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 14
                            spacing: 5
                            Label {
                                width: parent.width; text: controller.imageInformation.filename || ""
                                textFormat: Text.PlainText; color: "#f4f7f8"; font.pixelSize: 18; font.weight: Font.DemiBold; elide: Text.ElideMiddle
                            }
                            Label { width: parent.width; text: cameraDescription(); textFormat: Text.PlainText; visible: text.length > 0; elide: Text.ElideRight; color: "#c0cbd0"; font.pixelSize: 12 }
                            Label { width: parent.width; text: exposureDescription(); color: "#c0cbd0"; font.pixelSize: 12; wrapMode: Text.Wrap }
                        }
                    }
                    Rectangle {
                        anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 20
                        width: 92; height: 36; radius: 8; color: "#cf101619"; visible: controller.before
                        Label { anchors.centerIn: parent; text: qsTr("before"); color: "#f0f4f5" }
                    }
                    Rectangle {
                        objectName: "statusOverlay"
                        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                        anchors.leftMargin: 16; anchors.rightMargin: 16; anchors.bottomMargin: 16
                        height: statusLine.implicitHeight + 20; radius: 10
                        color: controller.hasError ? "#ed532d29" : "#d4101619"
                        visible: controller.busy || controller.hasError || window.statusVisible
                        RowLayout {
                            id: statusLine
                            anchors.fill: parent; anchors.margins: 10; spacing: 8
                            BusyIndicator { running: controller.busy; visible: running; implicitWidth: 24; implicitHeight: 24 }
                            Label { text: controller.message; textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#c0cbd0"; font.pixelSize: 12 }
                        }
                    }
                }
                Rectangle {
                    id: filmstripBar
                    objectName: "filmstripBar"
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(112, Math.min(window.maximumFilmstripHeight, window.requestedFilmstripHeight || (window.compact ? 120 : 144)))
                    color: "#0e1417"
                    Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: "#283337" }
                    PanelHandle {
                        objectName: "filmstripResizeHandle"
                        anchors.horizontalCenter: parent.horizontalCenter; anchors.top: parent.top; anchors.topMargin: -26
                        z: 2
                        Accessible.name: qsTr("filmstrip height")
                        panelSize: filmstripBar.height; minimumValue: 112; maximumValue: window.maximumFilmstripHeight
                        onAdjusted: function(delta) { window.resizeFilmstrip(delta) }
                        onResetRequested: { window.requestedFilmstripHeight = 0; Qt.callLater(clampPan) }
                    }
                    TouchButton {
                        id: menuButton
                        objectName: "editorMenuButton"
                        anchors.left: parent.left; anchors.leftMargin: 10; anchors.verticalCenter: filmstrip.verticalCenter
                        text: "\u2630"; implicitWidth: 64; font.pixelSize: 28
                        Accessible.name: qsTr("quick actions")
                        ToolTip.visible: hovered; ToolTip.text: qsTr("quick actions")
                        onClicked: showQuickMenu()
                    }
                    ListView {
                        id: filmstrip
                        objectName: "filmstrip"
                        anchors.left: menuButton.right; anchors.right: parent.right
                        anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.margins: 10; anchors.topMargin: 36
                        orientation: ListView.Horizontal; spacing: 10; clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        model: controller.images; cacheBuffer: 200
                        onWidthChanged: Qt.callLater(updateFilmstrip)
                        delegate: TouchButton {
                            required property var modelData
                            required property int index
                            width: Math.max(112, filmstrip.height * 1.6); height: filmstrip.height
                            text: ""; padding: 4; checked: controller.imageId === modelData.imgid
                            Accessible.name: modelData.filename
                            onClicked: { if(controller.imageId !== modelData.imgid) { viewZoom = 1; controller.selectImage(modelData.imgid) } else showImageInformation() }
                            onActiveFocusChanged: if(activeFocus) filmstrip.positionViewAtIndex(index, ListView.Contain)
                            ToolTip.visible: hovered; ToolTip.text: modelData.filename
                            Image {
                                objectName: "filmThumbnail-" + modelData.imgid
                                anchors.fill: parent; anchors.margins: 4
                                source: "image://photos/thumb/" + modelData.imgid + "?revision=" + window.thumbnailRevision
                                fillMode: Image.PreserveAspectFit; asynchronous: true; cache: false
                            }
                            background: Rectangle {
                                radius: 8; color: "#182126"
                                border.width: parent.checked || parent.activeFocus ? 2 : 1
                                border.color: parent.checked ? "#00dbe5" : parent.activeFocus ? "#f0f6f8" : "#2a3439"
                            }
                            Component.onCompleted: controller.requestThumbnail(modelData.imgid)
                            Connections {
                                target: controller
                                function onStateChanged() { if(controller.ready) controller.requestThumbnail(modelData.imgid) }
                            }
                        }
                    }
                }
            }
            Rectangle {
                id: toolPanel
                objectName: "toolPanel"
                Layout.preferredWidth: Math.max(340, Math.min(window.maximumModuleWidth, window.requestedModuleWidth || Math.min(420, window.width * 0.31)))
                Layout.fillHeight: true; color: "#111719"
                Rectangle { anchors.left: parent.left; width: 1; height: parent.height; color: "#283337" }
                PanelHandle {
                    objectName: "moduleResizeHandle"
                    anchors.left: parent.left; anchors.leftMargin: -24; anchors.verticalCenter: parent.verticalCenter
                    z: 2; vertical: true
                    Accessible.name: qsTr("module panel width")
                    panelSize: toolPanel.width; minimumValue: 340; maximumValue: window.maximumModuleWidth
                    onAdjusted: function(delta) { window.resizeModules(delta) }
                    onResetRequested: { window.requestedModuleWidth = 0; Qt.callLater(clampPan) }
                }
                Flickable {
                    id: toolScroll
                    anchors.fill: parent; anchors.margins: 12; anchors.leftMargin: 40
                    contentWidth: width; contentHeight: toolColumn.implicitHeight
                    onContentHeightChanged: contentY = Math.max(0, Math.min(contentY, contentHeight - height))
                    clip: true; boundsBehavior: Flickable.StopAtBounds
                    Column {
                        id: toolColumn
                        width: toolScroll.width; spacing: 10
                        Repeater {
                            model: controller.tools
                            Rectangle {
                                id: toolCard
                                objectName: "card-" + modelData.operation
                                required property var modelData
                                readonly property bool expanded: window.expandedTool === modelData.operation
                                onYChanged: if(expanded) toolScroll.contentY = Math.min(y, Math.max(0, toolScroll.contentHeight - toolScroll.height))
                                width: toolColumn.width; height: toolContents.implicitHeight
                                color: expanded ? "#1a2326" : "#161e21"; radius: 12
                                border.width: 1; border.color: expanded ? "#315258" : "#263136"
                                Column {
                                    id: toolContents
                                    width: parent.width
                                    RowLayout {
                                        width: parent.width; spacing: 0
                                        TouchButton {
                                            objectName: "tool-" + toolCard.modelData.operation
                                            Layout.fillWidth: true; text: ""
                                            enabled: toolCard.modelData.available && controller.imageId > 0
                                            Accessible.name: toolCard.modelData.label
                                            onClicked: {
                                                if(toolCard.expanded) window.expandedTool = ""
                                                else {
                                                    window.expandedTool = toolCard.modelData.operation
                                                    controller.selectTool(toolCard.modelData.operation)
                                                    Qt.callLater(function() { toolScroll.contentY = Math.min(toolCard.y, Math.max(0, toolScroll.contentHeight - toolScroll.height)) })
                                                }
                                            }
                                            background: Rectangle { radius: 12; color: parent.down || parent.hovered ? "#263438" : "transparent"; border.width: parent.activeFocus ? 2 : 0; border.color: "#00dbe5" }
                                            RowLayout {
                                                anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16; spacing: 12
                                                Label { text: toolCard.expanded ? "\u2304" : "\u203a"; color: "#9aafb6"; font.pixelSize: 22; Layout.preferredWidth: 18 }
                                                Label { text: toolCard.modelData.label; color: toolCard.expanded ? "#00dbe5" : "#e5ecef"; font.pixelSize: 17; Layout.fillWidth: true; elide: Text.ElideRight }
                                            }
                                        }
                                        TouchButton {
                                            text: "..."; implicitWidth: 64
                                            visible: toolCard.expanded
                                            Accessible.name: qsTr("tool actions")
                                            onClicked: toolActions.open()
                                            background: Rectangle { radius: 10; color: parent.down || parent.hovered ? "#263438" : "transparent"; border.width: parent.activeFocus ? 2 : 0; border.color: "#00dbe5" }
                                        }
                                        Switch {
                                            id: enabledSwitch
                                            visible: toolCard.expanded
                                            checked: controller.moduleEnabled
                                            implicitWidth: 64; implicitHeight: 64; padding: 0
                                            Accessible.name: qsTr("enable %1").arg(toolCard.modelData.label)
                                            onToggled: controller.setEnabled(checked)
                                            indicator: Rectangle {
                                                x: (enabledSwitch.width - width) / 2; y: (enabledSwitch.height - height) / 2
                                                width: 42; height: 26; radius: 13
                                                color: enabledSwitch.checked ? "#00dbe5" : "#465157"
                                                border.width: enabledSwitch.activeFocus ? 2 : 0; border.color: "#e6eef0"
                                                Rectangle { x: enabledSwitch.checked ? parent.width - width - 3 : 3; y: 3; width: 20; height: 20; radius: 10; color: "#f0f6f8" }
                                            }
                                        }
                                    }
                                    Loader {
                                        width: parent.width
                                        active: toolCard.expanded && controller.operation === toolCard.modelData.operation
                                        height: active && item ? item.implicitHeight : 0
                                        sourceComponent: adjustmentControls
                                    }
                                }
                            }
                        }
                    }
                    ScrollBar.vertical: ScrollBar { }
                }
            }
        }
    }

    Component {
        id: adjustmentControls
        ColumnLayout {
            width: parent.width; spacing: 8
            Repeater {
                model: controller.fields
                FieldControl {
                    required property var model
                    field: model.field
                    Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16
                    visible: controller.operation !== "exposure" || window.advanced
                        || ["exposure", "black", "mode"].indexOf(field.name) >= 0
                        || (Number(controller.values.mode) === 1 && field.name.startsWith("deflicker_"))
                }
            }
            Item { Layout.preferredHeight: 12 }
        }
    }

    footer: Rectangle {
        height: controller.editorVisible ? 0 : 32
        visible: !controller.editorVisible; color: "#101619"
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16
            BusyIndicator { running: controller.busy; implicitWidth: 24; implicitHeight: 24; visible: running }
            Label { text: controller.message; textFormat: Text.PlainText; Layout.fillWidth: true; elide: Text.ElideMiddle; color: controller.hasError ? "#ffa99e" : "#a8b0bc"; font.pixelSize: 12 }
            Label { text: gamepad.connected ? qsTr("A select   B back   X before   Y tools   LB/RB photos") : qsTr("touch, keyboard, and mouse"); color: "#7f8b9c"; font.pixelSize: 12 }
        }
    }

    LibraryDialog { id: collectionDialog; objectName: "collectionDialog"; photoController: controller }
    ImportReview { id: importReview; objectName: "importReview"; photoController: controller }
    Dialog {
        id: importSource
        modal: true; focus: true; anchors.centerIn: parent; title: qsTr("add to library")
        width: Math.min(480, window.width - 32); padding: 20
        background: Rectangle { color: "#121a1e"; radius: 16; border.color: "#334249" }
        contentItem: ColumnLayout {
            spacing: 12
            Label { text: qsTr("choose a source, then review which photos to add"); wrapMode: Text.WordWrap; Layout.fillWidth: true; color: "#aebbc1" }
            TouchButton { text: qsTr("choose photos"); Layout.fillWidth: true; onClicked: { importSource.close(); importDialog.open() } }
            TouchButton { text: qsTr("choose folder"); Layout.fillWidth: true; onClicked: { importSource.close(); folderDialog.open() } }
            TouchButton { text: qsTr("include subfolders"); checked: window.importRecursive; Layout.fillWidth: true; onClicked: window.importRecursive = !window.importRecursive }
            TouchButton { text: qsTr("cancel"); Layout.fillWidth: true; onClicked: importSource.close() }
        }
    }
    FileDialog {
        id: importDialog
        title: qsTr("import photos")
        fileMode: FileDialog.OpenFiles
        options: FileDialog.DontUseNativeDialog
        onAccepted: controller.reviewImport(selectedFiles, false)
    }
    Popup {
        id: toolActions
        anchors.centerIn: parent
        width: 320; padding: 20; modal: true; focus: true
        background: Rectangle { color: "#1c272c"; radius: 16; border.color: "#334249" }
        ColumnLayout {
            width: parent.width; spacing: 10
            Label { text: controller.toolLabel; Layout.fillWidth: true; color: "#e6eef0"; font.pixelSize: 20 }
            TouchButton { text: qsTr("reset tool"); Layout.fillWidth: true; onClicked: { toolActions.close(); controller.reset() } }
            TouchButton {
                text: window.advanced ? qsTr("fewer controls") : qsTr("all controls")
                Layout.fillWidth: true; visible: controller.operation === "exposure"
                checked: window.advanced; onClicked: { window.advanced = !window.advanced; toolActions.close() }
            }
            TouchButton { text: qsTr("close"); Layout.fillWidth: true; onClicked: toolActions.close() }
        }
    }
    FolderDialog {
        id: folderDialog
        title: qsTr("import a photo folder")
        options: FolderDialog.DontUseNativeDialog
        onAccepted: controller.reviewImport([selectedFolder], window.importRecursive)
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
        objectName: "quickMenu"
        anchors.centerIn: parent
        width: Math.min(520, window.width - 48); height: Math.min(620, window.height - 48); padding: 24; modal: true; focus: true
        background: Rectangle { color: "#24272d"; radius: 20; border.color: "#424951" }
        ColumnLayout {
            anchors.fill: parent; spacing: 12
            Label { text: qsTr("quick actions"); color: "#f0f1f3"; font.pixelSize: 24 }
            ScrollView {
                id: quickScroll
                Layout.fillWidth: true; Layout.fillHeight: true
                contentWidth: availableWidth
                ColumnLayout {
                    id: quickColumn
                    width: parent.width; spacing: 10
                    RowLayout {
                        Layout.fillWidth: true; spacing: 10
                        TouchButton { text: qsTr("library"); Layout.fillWidth: true; checked: !controller.editorVisible; onClicked: { quickMenu.close(); controller.editorVisible = false } }
                        TouchButton { text: qsTr("editor"); Layout.fillWidth: true; enabled: controller.imageId > 0; checked: controller.editorVisible; onClicked: { quickMenu.close(); controller.editorVisible = true } }
                    }
                    TouchButton { text: qsTr("export photo"); Layout.fillWidth: true; enabled: controller.imageId > 0; onClicked: { quickMenu.close(); exportDialog.open() } }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 10
                        TouchButton { text: qsTr("undo"); Layout.fillWidth: true; enabled: controller.canUndo; onClicked: { quickMenu.close(); controller.undo() } }
                        TouchButton { text: qsTr("redo"); Layout.fillWidth: true; enabled: controller.canRedo; onClicked: { quickMenu.close(); controller.redo() } }
                    }
                    TouchButton { text: qsTr("edit history"); Layout.fillWidth: true; enabled: controller.imageId > 0; onClicked: { quickMenu.close(); showHistory() } }
                    TouchButton {
                        text: controller.before ? qsTr("show edited image") : qsTr("show before image"); Layout.fillWidth: true; enabled: controller.imageId > 0
                        onClicked: { controller.before = !controller.before; quickMenu.close() }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 10
                        TouchButton { text: qsTr("fit image"); Layout.fillWidth: true; enabled: controller.imageId > 0; onClicked: { window.viewZoom = 1; clampPan(); quickMenu.close() } }
                        TouchButton { text: qsTr("image information"); Layout.fillWidth: true; enabled: controller.imageId > 0; onClicked: { quickMenu.close(); showImageInformation() } }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 10
                    TouchButton { text: qsTr("collections and filters"); Layout.fillWidth: true; enabled: controller.ready && !controller.busy; onClicked: { quickMenu.close(); collectionDialog.open() } }
                    TouchButton { text: qsTr("import photos"); Layout.fillWidth: true; enabled: controller.ready; onClicked: { quickMenu.close(); importDialog.open() } }
                        TouchButton { text: qsTr("import folder"); Layout.fillWidth: true; enabled: controller.ready; onClicked: { quickMenu.close(); folderDialog.open() } }
                    }
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
