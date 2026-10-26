import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: window
    visible: true
    width: 1280
    height: 720
    minimumWidth: 800
    minimumHeight: 480
    title: qsTr("darktabletouch")
    color: "#090d0f"
    font.family: "Segoe UI"
    font.pixelSize: 15
    palette.window: "#090d0f"
    palette.windowText: "#e6e8eb"
    palette.base: "#202329"
    palette.text: "#e6e8eb"
    palette.placeholderText: "#91a1aa"
    palette.button: "#292d34"
    palette.buttonText: "#e6e8eb"
    palette.highlight: "#00dbe5"
    palette.highlightedText: "#07191b"
    UiMetrics { id: ui; pixelDensity: window.Screen.pixelDensity; pixelRatio: window.Screen.devicePixelRatio }
    onActiveChanged: if(!active) controller.before = false
    property bool toolsOpen: false
    property bool distractionFree: false
    property string toolCategory: "light"
    property string filmstripState: "compact"
    property bool autoHideFilmstrip: true
    readonly property var toolGroups: [
        {key: "light", label: qsTr("light"), icon: "light", operations: ["exposure"]},
        {key: "color", label: qsTr("color"), icon: "color", operations: ["agx"]},
        {key: "detail", label: qsTr("detail"), icon: "detail", operations: ["denoiseprofile", "lens"]},
        {key: "crop", label: qsTr("crop"), icon: "crop", operations: ["crop", "flip"]}
    ]
    function categoryFor(operation) {
        for(const group of toolGroups) if(group.operations.indexOf(operation) >= 0) return group.key
        return "all"
    }
    function showToolCategory(group) {
        if(toolsOpen && toolCategory === group.key) { toolsOpen = false; return }
        distractionFree = false; toolsOpen = true; toolCategory = group.key
        if(group.operations.indexOf(controller.operation) < 0) controller.selectTool(group.operations[0])
        Qt.callLater(clampPan)
    }
    function cycleFilmstrip() {
        filmstripState = filmstripState === "hidden" ? "compact" : filmstripState === "compact" ? "expanded" : "hidden"
        requestedFilmstripHeight = 0
        filmstripTimer.restart()
        Qt.callLater(clampPan)
    }
    function toggleChrome() {
        if(controller.cropEditing || modalOpen) return
        distractionFree = !distractionFree
        informationVisible = false
        statusVisible = false
        Qt.callLater(clampPan)
    }
    onFilmstripStateChanged: { filmstripTimer.restart(); Qt.callLater(clampPan) }
    Timer {
        id: filmstripTimer; interval: 6000; running: controller.editorVisible && window.autoHideFilmstrip
        onTriggered: {
            if(!window.autoHideFilmstrip || !controller.editorVisible || window.filmstripState === "hidden") return
            if(!window.modalOpen && !filmstrip.moving && !filmstripHover.hovered)
                window.filmstripState = "hidden"
            else restart()
        }
    }
    property real viewZoom: 1
    property int cropRatioN: 0
    property int cropRatioD: 0
    property bool advanced: false
    property bool selectionMode: false
    property var actionImage: ({})
    property string pendingImageAction: ""
    property bool importRecursive: true
    readonly property var selectedImages: controller.library.selected || []
    property string expandedTool: "exposure"
    property bool informationVisible: false
    property bool statusVisible: true
    readonly property bool compact: height < 650
    property real requestedFilmstripHeight: 0
    property real requestedModuleWidth: 0
    readonly property real maximumFilmstripHeight: Math.min(300, height * 0.45)
    readonly property real maximumModuleWidth: Math.min(560, width - ui.touchHeight - 320)
    readonly property bool textInputFocused: activeFocusItem instanceof TextInput || activeFocusItem instanceof TextEdit
    readonly property bool modalOpen: quickMenu.visible || historyDialog.visible || toolSearch.visible || toolActions.visible
        || historyPasteDialog.visible || resetHistoryDialog.visible || imageActions.visible || confirmImageAction.visible
        || collectionDialog.visible || quickSettings.visible || batchExportDialog.visible || importReview.visible || importSource.visible
        || importDialog.visible || folderDialog.visible || exportDialog.visible || parameterPopupFocused()
    readonly property bool imageShortcutsEnabled: controller.ready && controller.imageId > 0 && !controller.busy
        && !controller.cropEditing && !modalOpen && !textInputFocused

    function resizeFilmstrip(delta) {
        filmstripState = "expanded"
        requestedFilmstripHeight = Math.max(ui.compactFilmstrip, Math.min(maximumFilmstripHeight, filmstripBar.height + delta))
        Qt.callLater(clampPan)
    }
    function resizeModules(delta) {
        requestedModuleWidth = Math.max(ui.panelWidth, Math.min(maximumModuleWidth, toolPanel.width + delta))
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
        if(historyPasteDialog.opened) revealItem(activeFocusItem, historyPasteDialog.focusColumn, historyPasteDialog.focusViewport)
        if(batchExportDialog.opened) revealItem(activeFocusItem, batchExportDialog.focusColumn, batchExportDialog.focusViewport)
        if(imageActions.opened) revealItem(activeFocusItem, imageActionColumn, imageActionScroll.contentItem)
    }

    Timer { id: informationTimer; interval: 4000; onTriggered: window.informationVisible = false }
    Timer { id: statusTimer; interval: 3500; onTriggered: window.statusVisible = false }

    function showHistory() { if(controller.cropEditing) controller.cropCommand("cancel"); historyDialog.open() }
    function showImageActions(image) {
        if(controller.busy) return
        if(controller.cropEditing) controller.cropCommand("cancel")
        actionImage = JSON.parse(JSON.stringify(image))
        imageActions.open()
    }
    function chooseImageAction(action) {
        imageActions.close()
        if(action === "copy") { controller.historyCommand("copy", {}, actionImage.imgid); return }
        if(action === "paste") { historyPasteDialog.imageId = actionImage.imgid; historyPasteDialog.scope = "current"; historyPasteDialog.open(); return }
        if(action === "reset") { showResetHistory(actionImage.imgid); return }
        if(action === "remove" || action === "trash") { pendingImageAction = action; confirmImageAction.open() }
        else controller.libraryCommand(action, {}, actionImage.imgid)
    }
    function showResetHistory(image) { if(controller.cropEditing) controller.cropCommand("cancel"); resetHistoryDialog.imageId = image; resetHistoryDialog.scope = "current"; resetHistoryDialog.open() }
    function showQuickMenu() { if(controller.cropEditing) controller.cropCommand("cancel"); historyPasteDialog.close(); resetHistoryDialog.close(); batchExportDialog.close(); quickSettings.close(); imageActions.close(); confirmImageAction.close(); historyDialog.close(); toolActions.close(); toolSearch.close(); collectionDialog.close(); importReview.close(); importSource.close(); quickMenu.open() }

    onClosing: function(close) {
        if(!controller.canClose) { close.accepted = false; saveView(); controller.shutdown() }
    }
    Connections {
        target: controller
        function onClosingReady() { window.close() }
        function onImageInformationChanged() { showImageInformation(); updateFilmstrip(); if(window.autoHideFilmstrip) filmstripTimer.restart() }
        function onImagesChanged() { Qt.callLater(updateFilmstrip) }
        function onImportReviewChanged() { importReview.open() }
        function onModuleChanged() { window.expandedTool = controller.operation; window.toolCategory = window.categoryFor(controller.operation) }
        function onCropChanged() { if(controller.cropEditing) { window.toolsOpen = true; window.distractionFree = false; window.viewZoom = 1; canvas.contentX = 0; canvas.contentY = 0; cropOverlay.setBounds(controller.cropBounds) } }
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
    function placeZoom(zoom, x, y, imageX, imageY) {
        viewZoom = Math.max(1, Math.min(8, zoom))
        canvas.contentX = imageX * canvas.width * viewZoom - x
        canvas.contentY = imageY * canvas.height * viewZoom - y
        clampPan()
    }
    function zoomAt(zoom, x, y) {
        placeZoom(zoom, x, y, (canvas.contentX + x) / canvas.contentWidth,
            (canvas.contentY + y) / canvas.contentHeight)
    }
    function zoomBy(amount, point) {
        const anchor = point || Qt.point(canvas.width / 2, canvas.height / 2)
        zoomAt(viewZoom * amount, anchor.x, anchor.y)
    }

    function parameterPopupFocused() { let item = activeFocusItem; while(item) { if(item.objectName === "choicePopupList" || item.objectName === "fieldActionsContent") return true; item = item.parent } return false }
    Binding { target: gamepad; property: "editing"; value: !controller.exportRunning && !window.modalOpen && !window.textInputFocused && controller.editorVisible && !controller.cropEditing }
    Connections {
        target: gamepad
        function onAction(name, pressed) {
            if(name === "compare" && !pressed) { controller.before = false; return }
            if(!pressed || window.modalOpen || window.textInputFocused || controller.cropEditing || controller.exportRunning) return
            if(name === "compare") { if(controller.editorVisible) controller.before = true; return }
            if(name === "previous") { window.contentItem.forceActiveFocus(); controller.navigateImage(-1) }
            else if(name === "next") { window.contentItem.forceActiveFocus(); controller.navigateImage(1) }
            else if(name === "increase") { window.contentItem.forceActiveFocus(); controller.adjustLastParameter(1) }
            else if(name === "decrease") { window.contentItem.forceActiveFocus(); controller.adjustLastParameter(-1) }
            else if(name === "library") controller.editorVisible = !controller.editorVisible
            else if(name === "search") toolSearch.open()
            else if(name === "menu") showQuickMenu()
        }
        function onPan(delta) {
            if(gamepad.editing) { canvas.contentX += delta.x * 12; canvas.contentY += delta.y * 12; clampPan() }
        }
        function onZoom(delta) { if(gamepad.editing) zoomBy(1 + delta * 0.025) }
    }

    Shortcut { sequences: [StandardKey.Undo]; enabled: window.imageShortcutsEnabled; onActivated: controller.undo() }
    Shortcut { sequences: [StandardKey.Redo]; enabled: window.imageShortcutsEnabled; onActivated: controller.redo() }
    Shortcut {
        sequence: "Escape"
        enabled: !window.modalOpen && !window.textInputFocused
        onActivated: { if(controller.cropEditing) { controller.cropCommand("cancel"); return } if(controller.before) controller.before = false; else if(!controller.editorVisible && window.selectionMode) window.selectionMode = false; else if(window.distractionFree) window.distractionFree = false; else if(window.toolsOpen) window.toolsOpen = false; else controller.editorVisible = false }
    }

    header: Rectangle {
        objectName: "editorActionBar"
        visible: controller.editorVisible && !window.distractionFree
        height: visible ? ui.touchHeight + 8 : 0
        color: "#101619"
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 8
            TouchButton {
                objectName: "editorLibraryButton"
                text: qsTr("library"); iconName: "grid"; showLabel: true; stacked: window.width < 1050; padding: 6
                enabled: !controller.exportRunning
                onClicked: controller.editorVisible = false
            }
            TouchButton { objectName: "editorUndoButton"; showLabel: false; flat: true; padding: 8; text: qsTr("undo"); iconName: "undo"; enabled: controller.canUndo && !controller.busy && !controller.cropEditing; onClicked: controller.undo() }
            TouchButton { objectName: "editorRedoButton"; showLabel: false; flat: true; padding: 8; text: qsTr("redo"); iconName: "redo"; enabled: controller.canRedo && !controller.busy && !controller.cropEditing; onClicked: controller.redo() }
            TouchButton {
                objectName: "editorCompareButton"
                text: qsTr("hold original")
                checked: controller.before; enabled: controller.imageId > 0 && !controller.busy && !controller.cropEditing
                onPressed: controller.before = true
                onReleased: controller.before = false
                onCanceled: controller.before = false
                onActiveFocusChanged: if(!activeFocus) controller.before = false
            }
            TouchButton {
                objectName: "editorMetadataButton"
                text: controller.imageInformation.filename || ""; flat: true
                Layout.fillWidth: true; Layout.minimumWidth: 0; clip: true
                font.pixelSize: ui.captionSize
                onClicked: showImageInformation()
                contentItem: Label { text: parent.text; textFormat: Text.PlainText; elide: Text.ElideMiddle; color: ui.muted; verticalAlignment: Text.AlignVCenter }
            }
            TouchButton { objectName: "filmstripStateButton"; stacked: window.width < 1050; padding: 6; text: qsTr("photos"); iconName: "grid"; flat: true; onClicked: cycleFilmstrip() }
            TouchButton {
                objectName: "editorExportButton"
                text: qsTr("export"); iconName: "export"; showLabel: true; stacked: window.width < 1050; padding: 6
                enabled: controller.imageId > 0 && !controller.busy && !controller.cropEditing
                onClicked: { batchExportDialog.scope = "current"; batchExportDialog.open() }
            }
            TouchButton { text: qsTr("more"); iconName: "menu"; stacked: window.width < 1050; padding: 6; onClicked: showQuickMenu() }
        }
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: controller.editorVisible ? 1 : 0
        ColumnLayout {
            spacing: 16
            RowLayout {
                objectName: "libraryActionBar"
                Layout.fillWidth: true; Layout.preferredWidth: parent.width - 2 * ui.inset
                Layout.leftMargin: ui.inset; Layout.rightMargin: ui.inset; Layout.topMargin: ui.inset; spacing: ui.gap
                ColumnLayout {
                    Layout.minimumWidth: 120
                    Label { text: qsTr("photo library"); color: ui.text; font.pixelSize: ui.titleSize; font.weight: Font.DemiBold }
                    Label { text: controller.images.length === 1 ? qsTr("1 photo") : qsTr("%1 photos").arg(controller.images.length); color: ui.muted; font.pixelSize: ui.captionSize }
                }
                Item { Layout.fillWidth: true }
                TouchButton { text: window.selectionMode ? qsTr("done") : qsTr("select"); visible: controller.images.length > 0; checked: window.selectionMode; onClicked: window.selectionMode = !window.selectionMode }
                TouchButton { text: qsTr("import"); iconName: "import"; visible: controller.images.length > 0; enabled: controller.ready && !controller.busy; onClicked: importSource.open() }
                TouchButton { text: qsTr("export"); iconName: "export"; visible: controller.images.length > 0; enabled: !controller.busy && (!window.selectionMode || window.selectedImages.length > 0); onClicked: { batchExportDialog.scope = window.selectionMode ? "selected" : "collection"; batchExportDialog.open() } }
                TouchButton { objectName: "libraryFilterButton"; text: qsTr("filters"); iconName: "filter"; flat: true; enabled: controller.ready && !controller.busy; onClicked: collectionDialog.open() }
                TouchButton { text: qsTr("more"); iconName: "menu"; flat: true; onClicked: showQuickMenu() }
            }
            Flickable {
                Layout.fillWidth: true; Layout.preferredHeight: 72
                Layout.leftMargin: 16; Layout.rightMargin: 16
                visible: window.selectionMode
                contentWidth: selectionActions.implicitWidth; contentHeight: height
                flickableDirection: Flickable.HorizontalFlick; clip: true
                boundsBehavior: Flickable.StopAtBounds
                RowLayout {
                    id: selectionActions
                    height: 64; spacing: 8
                    Label { text: qsTr("%1 selected").arg(window.selectedImages.length); color: "#00dbe5" }
                    TouchButton { text: qsTr("all"); enabled: !controller.busy; onClicked: controller.libraryCommand("all", {}, 0) }
                    TouchButton { text: qsTr("none"); enabled: !controller.busy; onClicked: controller.libraryCommand("none", {}, 0) }
                    TouchButton { text: qsTr("invert"); enabled: !controller.busy; onClicked: controller.libraryCommand("invert", {}, 0) }
                    TouchButton { text: qsTr("same film roll"); enabled: !controller.busy && window.selectedImages.length > 0; onClicked: controller.libraryCommand("film", {}, 0) }
                    TouchButton { text: qsTr("untouched"); enabled: !controller.busy; onClicked: controller.libraryCommand("untouched", {}, 0) }
                    TouchButton {
                        objectName: "pasteSelectedButton"
                        text: qsTr("paste history"); iconName: "paste"
                        enabled: !controller.busy && window.selectedImages.length > 0 && !!controller.copiedHistory.source
                        onClicked: { historyPasteDialog.imageId = controller.imageId; historyPasteDialog.scope = "selected"; historyPasteDialog.open() }
                    }
                    TouchButton {
                        objectName: "resetSelectedButton"
                        text: qsTr("reset selected"); iconName: "reset"; enabled: !controller.busy && window.selectedImages.length > 0
                        onClicked: { showResetHistory(controller.imageId); resetHistoryDialog.scope = "selected" }
                    }
                }
                ScrollBar.horizontal: ScrollBar {}
            }
            GridView {
                id: library
                Layout.fillWidth: true; Layout.fillHeight: true; Layout.leftMargin: 16; Layout.rightMargin: 16
                clip: true
                cellWidth: Math.max(200, width / Math.max(1, Math.floor(width / 240)))
                cellHeight: Math.min(cellWidth * 0.80, Math.max(64, height))
                model: controller.images
                cacheBuffer: cellHeight
                delegate: Item {
                    required property var modelData
                    required property int index
                    width: library.cellWidth; height: library.cellHeight
                    ThumbnailButton {
                        objectName: "thumbnail-" + modelData.imgid
                        anchors.fill: parent; anchors.margins: 6; text: ""
                        enabled: controller.ready && !controller.busy
                        Accessible.name: modelData.filename
                        checked: window.selectionMode && window.selectedImages.indexOf(modelData.imgid) >= 0
                        onClicked: {
                            if(window.selectionMode) controller.libraryCommand("toggle", {}, modelData.imgid)
                            else { viewZoom = 1; controller.selectImage(modelData.imgid) }
                        }
                        onPressAndHold: {
                            window.showImageActions(modelData)
                        }
                        TapHandler { acceptedButtons: Qt.RightButton; onTapped: window.showImageActions(modelData) }
                        onActiveFocusChanged: if(activeFocus) { library.currentIndex = index; library.positionViewAtIndex(index, GridView.Contain) }
                        Keys.onPressed: function(event) {
                            if(event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                                event.accepted = true; window.showImageActions(modelData); return
                            }
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
                            ThumbnailImage { Layout.fillWidth: true; Layout.fillHeight: true; imageId: modelData.imgid }
                            Label { text: modelData.filename; textFormat: Text.PlainText; Layout.fillWidth: true; elide: Text.ElideMiddle; font.pixelSize: 14; color: window.selectionMode && window.selectedImages.indexOf(modelData.imgid) >= 0 ? "#07191b" : "#e2e5e9" }
                        }
                    }

                }
                ColumnLayout {
                    anchors.centerIn: parent; width: Math.min(440, parent.width - 32)
                    visible: controller.ready && controller.images.length === 0; spacing: ui.gap
                    Label { text: qsTr("your photo library is empty"); Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; color: ui.text; font.pixelSize: ui.titleSize }
                    Label { text: qsTr("import photos to start editing, or change filters to find your photos"); Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; color: ui.muted }
                    TouchButton { objectName: "emptyImportButton"; text: qsTr("import photos"); iconName: "import"; Layout.alignment: Qt.AlignHCenter; enabled: !controller.busy; onClicked: importSource.open() }
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
                        objectName: "imageCanvas"
                        anchors.fill: parent
                        interactive: window.viewZoom > 1 && !pinch.active && !controller.cropEditing
                        clip: true; boundsBehavior: Flickable.StopAtBounds
                        onWidthChanged: Qt.callLater(clampPan)
                        onHeightChanged: Qt.callLater(clampPan)
                        contentWidth: width * window.viewZoom
                        contentHeight: height * window.viewZoom
                        Image {
                            id: photo
                            width: canvas.contentWidth; height: canvas.contentHeight
                            source: controller.preview
                            fillMode: Image.PreserveAspectFit; asynchronous: true; cache: false
                            TapHandler {
                                enabled: !controller.cropEditing
                                objectName: "canvasTapHandler"
                                exclusiveSignals: TapHandler.SingleTap | TapHandler.DoubleTap
                                onSingleTapped: window.toggleChrome()
                                onDoubleTapped: function(point) {
                                    const anchor = imageStage.mapFromItem(null, point.scenePosition.x, point.scenePosition.y)
                                    window.zoomAt(window.viewZoom > 1 ? 1 : 2, anchor.x, anchor.y)
                                }
                            }
                            DragHandler {
                                target: null; xAxis.enabled: false
                                enabled: window.filmstripState === "hidden" && !window.distractionFree && !controller.cropEditing && window.viewZoom <= 1
                                function revealPhotos() {
                                    if(active && centroid.scenePosition.y - centroid.scenePressPosition.y < -24) {
                                        window.filmstripState = "compact"; filmstripTimer.restart()
                                    }
                                }
                                onActiveChanged: revealPhotos()
                                onCentroidChanged: revealPhotos()
                            }
                            CropOverlay {
                                id: cropOverlay
                                anchors.centerIn: parent; width: photo.paintedWidth; height: photo.paintedHeight
                                visible: controller.cropEditing; enabled: visible && !controller.busy
                                aspect: window.cropRatioN > 0 && window.cropRatioD > 0 ? window.cropRatioN / window.cropRatioD : 0
                            }
                        }
                        PinchHandler {
                            id: pinch
                            enabled: !controller.cropEditing
                            parent: imageStage
                            target: null
                            property real startZoom: 1
                            property point imageAnchor: Qt.point(0.5, 0.5)
                            function updateZoom() {
                                if(!active) return
                                const point = imageStage.mapFromItem(null, centroid.scenePosition.x, centroid.scenePosition.y)
                                window.placeZoom(startZoom * activeScale, point.x, point.y, imageAnchor.x, imageAnchor.y)
                            }
                            onActiveChanged: if(active) {
                                canvas.cancelFlick()
                                startZoom = window.viewZoom
                                const point = imageStage.mapFromItem(null, centroid.scenePosition.x, centroid.scenePosition.y)
                                imageAnchor = Qt.point((canvas.contentX + point.x) / canvas.contentWidth,
                                    (canvas.contentY + point.y) / canvas.contentHeight)
                            }
                            onActiveScaleChanged: updateZoom()
                            onCentroidChanged: updateZoom()
                        }
                        WheelHandler {
                            parent: imageStage; target: null; enabled: !controller.cropEditing
                            onWheel: function(event) {
                                zoomBy(Math.pow(1.1, event.angleDelta.y / 120), Qt.point(event.x, event.y))
                                event.accepted = true
                            }
                        }
                    }
                    TouchButton {
                        objectName: "filmstripRevealButton"
                        visible: window.filmstripState === "hidden" && !window.distractionFree
                        text: qsTr("photos"); iconName: "grid"; flat: true
                        anchors.right: parent.right; anchors.bottom: parent.bottom
                        onClicked: { window.filmstripState = "compact"; filmstripTimer.restart() }
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
                    visible: window.filmstripState !== "hidden" && !window.distractionFree
                    Layout.preferredHeight: Math.min(window.maximumFilmstripHeight, window.requestedFilmstripHeight || (window.filmstripState === "expanded" ? ui.expandedFilmstrip : ui.compactFilmstrip))
                    color: "#0e1417"
                    Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: "#283337" }
                    PanelHandle {
                        objectName: "filmstripResizeHandle"
                        anchors.horizontalCenter: parent.horizontalCenter; anchors.top: parent.top; anchors.topMargin: -ui.touchHeight / 2
                        z: 2
                        Accessible.name: qsTr("filmstrip height")
                        panelSize: filmstripBar.height; minimumValue: ui.compactFilmstrip; maximumValue: window.maximumFilmstripHeight
                        onAdjusted: function(delta) { window.resizeFilmstrip(delta) }
                        onResetRequested: { window.requestedFilmstripHeight = 0; window.filmstripState = "compact"; Qt.callLater(clampPan) }
                    }
                    TouchButton {
                        id: menuButton
                        objectName: "editorMenuButton"
                        anchors.left: parent.left; anchors.leftMargin: 10; anchors.verticalCenter: filmstrip.verticalCenter
                        iconName: "grid"; text: qsTr("photos"); stacked: true; implicitWidth: ui.touchHeight; padding: 6
                        Accessible.name: qsTr("filmstrip size")
                        onClicked: window.cycleFilmstrip()
                    }
                    ListView {
                        id: filmstrip
                        objectName: "filmstrip"
                        anchors.left: menuButton.right; anchors.right: parent.right
                        anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.margins: 8; anchors.topMargin: ui.touchHeight / 2 + 4
                        orientation: ListView.Horizontal; spacing: 10; clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        model: controller.images; cacheBuffer: 200
                        HoverHandler { id: filmstripHover }
                        onMovementEnded: filmstripTimer.restart()
                        onWidthChanged: Qt.callLater(updateFilmstrip)
                        delegate: ThumbnailButton {
                            required property var modelData
                            required property int index
                            width: Math.max(112, filmstrip.height * 1.6); height: filmstrip.height
                            text: ""; padding: 4; checked: controller.imageId === modelData.imgid
                            Accessible.name: modelData.filename
                            onClicked: { if(controller.imageId !== modelData.imgid) { viewZoom = 1; controller.selectImage(modelData.imgid) } else showImageInformation() }
                            onPressAndHold: window.showImageActions(modelData)
                            TapHandler { acceptedButtons: Qt.RightButton; onTapped: window.showImageActions(modelData) }
                            onActiveFocusChanged: if(activeFocus) filmstrip.positionViewAtIndex(index, ListView.Contain)
                            ToolTip.visible: hovered; ToolTip.text: modelData.filename
                            ThumbnailImage {
                                objectName: "filmThumbnail-" + modelData.imgid
                                anchors.fill: parent; anchors.margins: 4; imageId: modelData.imgid
                            }

                            background: Rectangle {
                                radius: 8; color: "#182126"
                                border.width: parent.checked || parent.activeFocus ? 2 : 1
                                border.color: parent.checked ? "#00dbe5" : parent.activeFocus ? "#f0f6f8" : "#2a3439"
                            }

                        }
                    }
                }
            }
            Rectangle {
                id: toolPanel
                objectName: "toolPanel"
                visible: window.toolsOpen && !window.distractionFree
                Layout.preferredWidth: Math.max(ui.panelWidth, Math.min(window.maximumModuleWidth, window.requestedModuleWidth || ui.panelWidth))
                Layout.fillHeight: true; color: ui.panel
                Rectangle { anchors.left: parent.left; width: 1; height: parent.height; color: "#283337" }
                PanelHandle {
                    objectName: "moduleResizeHandle"
                    anchors.left: parent.left; anchors.leftMargin: -ui.touchHeight / 2; anchors.verticalCenter: parent.verticalCenter
                    z: 2; vertical: true
                    Accessible.name: qsTr("module panel width")
                    panelSize: toolPanel.width; minimumValue: ui.panelWidth; maximumValue: window.maximumModuleWidth
                    onAdjusted: function(delta) { window.resizeModules(delta) }
                    onResetRequested: { window.requestedModuleWidth = 0; Qt.callLater(clampPan) }
                }
                Flickable {
                    id: toolScroll
                    anchors.fill: parent; anchors.margins: ui.gap; anchors.leftMargin: ui.touchHeight / 2 + ui.gap
                    contentWidth: width; contentHeight: toolColumn.implicitHeight
                    onContentHeightChanged: contentY = Math.max(0, Math.min(contentY, contentHeight - height))
                    clip: true; boundsBehavior: Flickable.StopAtBounds
                    Column {
                        id: toolColumn
                        width: toolScroll.width; spacing: ui.gap
                        RowLayout {
                            width: parent.width
                            TouchButton { objectName: "moduleResetButton"; text: qsTr("reset %1").arg(controller.toolLabel); iconName: "reset"; Layout.fillWidth: true; flat: true; enabled: !controller.busy && !controller.cropEditing; onClicked: controller.reset() }
                            TouchButton { text: qsTr("hide"); iconName: "chevron-right"; flat: true; onClicked: window.toolsOpen = false }
                        }
                        Repeater {
                            model: controller.tools
                            Rectangle {
                                id: toolCard
                                objectName: "card-" + modelData.operation
                                required property var modelData
                                readonly property bool expanded: window.expandedTool === modelData.operation
                                onYChanged: if(expanded) toolScroll.contentY = Math.min(y, Math.max(0, toolScroll.contentHeight - toolScroll.height))
                                visible: window.categoryFor(modelData.operation) === window.toolCategory || window.toolCategory === "all"
                                width: toolColumn.width; height: visible ? toolContents.implicitHeight : 0; clip: true
                                Behavior on height { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
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
                                            enabled: toolCard.modelData.available && controller.imageId > 0 && !controller.busy
                                            Accessible.name: toolCard.modelData.label
                                            onClicked: {
                                                if(toolCard.expanded) { window.expandedTool = ""; if(controller.cropEditing) controller.cropCommand("cancel") }
                                                else {
                                                    window.toolsOpen = true; window.distractionFree = false
                                                    window.toolCategory = window.categoryFor(toolCard.modelData.operation)
                                                    window.expandedTool = toolCard.modelData.operation
                                                    controller.selectTool(toolCard.modelData.operation)
                                                    Qt.callLater(function() { toolScroll.contentY = Math.min(toolCard.y, Math.max(0, toolScroll.contentHeight - toolScroll.height)) })
                                                }
                                            }
                                            background: Rectangle { radius: 12; color: parent.down || parent.hovered ? "#263438" : "transparent"; border.width: parent.activeFocus ? 2 : 0; border.color: "#00dbe5" }
                                            RowLayout {
                                                anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 8
                                                Icon { visible: toolCard.modelData.operation !== "lens"; name: toolCard.expanded ? "chevron-down" : "chevron-right"; color: "#9aafb6"; Layout.preferredWidth: 18; Layout.preferredHeight: 18 }
                                                Label { text: toolCard.modelData.label; color: toolCard.expanded ? "#00dbe5" : "#e5ecef"; font.pixelSize: 17; Layout.fillWidth: true; elide: Text.ElideRight }
                                            }
                                        }
                                        TouchButton {
                                            iconName: "more"; text: qsTr("actions"); stacked: true; implicitWidth: 76; padding: 8; font.pixelSize: 13
                                            visible: toolCard.expanded && toolCard.modelData.operation !== "lens" && !controller.cropEditing
                                            Accessible.name: qsTr("tool actions")
                                            onClicked: toolActions.open()
                                            background: Rectangle { radius: 10; color: parent.down || parent.hovered ? "#263438" : "transparent"; border.width: parent.activeFocus ? 2 : 0; border.color: "#00dbe5" }
                                        }
                                        Switch {
                                            id: enabledSwitch
                                            objectName: "enable-" + toolCard.modelData.operation
                                            enabled: controller.imageId > 0 && !controller.cropEditing && !controller.exportRunning
                                            visible: true
                                            checked: controller.cropEditing && toolCard.modelData.operation === "crop" ? true : controller.moduleStates[toolCard.modelData.operation] || false
                                            implicitWidth: ui.touchHeight; implicitHeight: ui.touchHeight; padding: 0; leftPadding: 0; rightPadding: 0
                                            text: checked ? qsTr("on") : qsTr("off")
                                            contentItem: Text { text: enabledSwitch.text; color: "#c0cbd0"; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignBottom; bottomPadding: 3 }
                                            Accessible.name: qsTr("enable %1").arg(toolCard.modelData.label)
                                            onToggled: controller.setEnabledFor(toolCard.modelData.operation, checked)
                                            indicator: Rectangle {
                                                x: (enabledSwitch.width - width) / 2; y: 8
                                                width: 42; height: 26; radius: 13
                                                color: enabledSwitch.checked ? "#00dbe5" : "#465157"
                                                border.width: enabledSwitch.activeFocus ? 2 : 0; border.color: "#e6eef0"
                                                Rectangle { x: enabledSwitch.checked ? parent.width - width - 3 : 3; y: 3; width: 20; height: 20; radius: 10; color: "#f0f6f8" }
                                            }
                                        }
                                    }
                                    Loader {
                                        width: parent.width
                                        active: toolCard.expanded && controller.operation === toolCard.modelData.operation && toolCard.modelData.operation !== "lens"
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
            Rectangle {
                objectName: "toolRail"
                visible: !window.distractionFree
                Layout.preferredWidth: ui.touchHeight + 8; Layout.fillHeight: true; color: ui.panel
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 4; spacing: ui.gap
                    Repeater {
                        model: window.toolGroups
                        TouchButton {
                            required property var modelData
                            objectName: "category-" + modelData.key
                            text: modelData.label; iconName: modelData.icon; stacked: true; flat: true; padding: 6
                            Layout.fillWidth: true; font.pixelSize: ui.captionSize
                            checked: window.toolsOpen && window.toolCategory === modelData.key
                            enabled: controller.imageId > 0 && !controller.busy
                            onClicked: window.showToolCategory(modelData)
                        }
                    }
                    Item { Layout.fillHeight: true }
                    TouchButton { objectName: "allToolsButton"; text: qsTr("all tools"); iconName: "more"; stacked: true; flat: true; padding: 6; Layout.fillWidth: true; font.pixelSize: ui.captionSize; onClicked: { window.toolsOpen = true; window.distractionFree = false; window.toolCategory = "all"; toolSearch.open() } }
                }
            }
        }
    }

    Component {
        id: adjustmentControls
        ColumnLayout {
            width: parent.width; spacing: 8
            ColumnLayout {
                visible: controller.operation === "crop"
                Layout.fillWidth: true; Layout.margins: 16
                TouchComboBox {
                    id: cropAspectControl
                    visible: controller.cropEditing
                    Layout.fillWidth: true; implicitHeight: 64
                    model: [qsTr("free"), qsTr("original"), "1:1", "4:3", "3:2", "16:9", qsTr("custom")]
                    onActivated: {
                        const ratios = [[0,0],[controller.cropOriginalSize.width,controller.cropOriginalSize.height],[1,1],[4,3],[3,2],[16,9],[window.cropRatioN || 1,window.cropRatioD || 1]]
                        window.cropRatioN = ratios[currentIndex][0]; window.cropRatioD = ratios[currentIndex][1]
                        Qt.callLater(cropOverlay.fitRatio)
                    }
                }
                RowLayout {
                    visible: controller.cropEditing && cropAspectControl.currentIndex === 6
                    TextField { Layout.fillWidth: true; implicitHeight: 64; placeholderText: qsTr("ratio width"); text: window.cropRatioN; validator: IntValidator { bottom: 0; top: 100000 } onEditingFinished: if(acceptableInput) window.cropRatioN = Number(text) }
                    TextField { Layout.fillWidth: true; implicitHeight: 64; placeholderText: qsTr("ratio height"); text: window.cropRatioD; validator: IntValidator { bottom: 0; top: 100000 } onEditingFinished: if(acceptableInput) window.cropRatioD = Number(text) }

                }
                TouchButton { visible: controller.cropEditing && window.cropRatioN > 0; text: qsTr("swap aspect orientation"); iconName: "redo"; enabled: window.cropRatioN > 0; onClicked: { const n = window.cropRatioN; window.cropRatioN = window.cropRatioD; window.cropRatioD = n; Qt.callLater(cropOverlay.fitRatio) } }
                TouchButton { text: qsTr("edit crop"); iconName: "crop"; visible: !controller.cropEditing; onClicked: controller.cropCommand("begin") }
                TouchButton { visible: controller.cropEditing; text: qsTr("auto crop borders"); Layout.fillWidth: true; enabled: controller.cropEditing && !controller.busy; onClicked: controller.cropCommand("auto", {aspect: cropOverlay.aspect || photo.paintedWidth/photo.paintedHeight}) }
                RowLayout {
                    visible: controller.cropEditing
                    TouchButton { Layout.fillWidth: true; stacked: true; padding: 8; text: qsTr("reset frame"); iconName: "reset"; enabled: controller.cropEditing; onClicked: { cropOverlay.setBounds([0,0,1,1]); cropOverlay.fitRatio() } }
                    TouchButton { Layout.fillWidth: true; stacked: true; padding: 8; text: qsTr("cancel"); iconName: "close"; enabled: controller.cropEditing; onClicked: controller.cropCommand("cancel") }
                    TouchButton { Layout.fillWidth: true; stacked: true; padding: 8; text: qsTr("apply"); iconName: "check"; enabled: controller.cropEditing && !controller.busy && ((window.cropRatioN === 0 && window.cropRatioD === 0) || (window.cropRatioN > 0 && window.cropRatioD > 0)); onClicked: controller.cropCommand("apply", {bounds: cropOverlay.bounds, ratio_n: window.cropRatioN, ratio_d: window.cropRatioD}) }
                }
            }
            Repeater {
                model: controller.operation === "crop" ? null : controller.fields
                FieldControl {
                    required property var model
                    enabled: !controller.exportRunning
                    field: model.field
                    Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16
                    visible: controller.operation === "exposure" && !window.advanced
                        ? (Number(controller.values.mode) === 1 ? ["mode", "deflicker_target_level", "deflicker_percentile"] : ["mode", "exposure", "black"]).indexOf(field.name) >= 0
                        : field.visible !== false
                }
            }
            Item { Layout.preferredHeight: 12 }
        }
    }

    footer: Rectangle {
        height: controller.exportRunning ? 72 : controller.editorVisible ? 0 : 32
        visible: !controller.editorVisible || controller.exportRunning; color: "#101619"
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16
            BusyIndicator { running: controller.busy; implicitWidth: 24; implicitHeight: 24; visible: running }
            Label { text: controller.message; textFormat: Text.PlainText; Layout.fillWidth: true; elide: Text.ElideMiddle; color: controller.hasError ? "#ffa99e" : "#a8b0bc"; font.pixelSize: 12 }
            TouchButton { text: qsTr("cancel export"); iconName: "close"; showLabel: true; visible: controller.exportRunning; onClicked: controller.cancelExport() }
            Label { text: gamepad.connected ? qsTr("A select   B back   X before   Y tools   LB/RB photos") : qsTr("touch, keyboard, and mouse"); color: "#7f8b9c"; font.pixelSize: 12 }
        }
    }

    TouchPopup {
        id: imageActions
        objectName: "imageActions"
        anchors.centerIn: parent; width: Math.min(500, window.width - 32)
        height: Math.min(600, window.height - 32)
        padding: 20; modal: true; focus: true
        background: Rectangle { color: "#121a1e"; radius: 16; border.color: "#334249" }
        ScrollView {
            id: imageActionScroll
            anchors.fill: parent; contentWidth: availableWidth; clip: true
            ColumnLayout {
            id: imageActionColumn
            width: parent.width; spacing: 10
            Label { text: window.actionImage.filename || ""; textFormat: Text.PlainText; color: "#e6eef0"; font.pixelSize: 20; Layout.fillWidth: true; elide: Text.ElideMiddle }
            TouchButton { showLabel: true; iconName: "copy"; text: qsTr("copy history"); Layout.fillWidth: true; enabled: !controller.busy; onClicked: window.chooseImageAction("copy") }
            TouchButton { showLabel: true; iconName: "paste"; text: qsTr("paste selected history"); Layout.fillWidth: true; enabled: !controller.busy && !!controller.copiedHistory.source; onClicked: window.chooseImageAction("paste") }
            TouchButton { showLabel: true; iconName: "reset"; text: qsTr("reset adjustments"); Layout.fillWidth: true; enabled: !controller.busy; onClicked: window.chooseImageAction("reset") }
            TouchButton { showLabel: true; iconName: "reject"; text: qsTr("reject"); Layout.fillWidth: true; enabled: !controller.busy; onClicked: window.chooseImageAction("reject") }
            TouchButton { showLabel: true; iconName: "unrate"; text: qsTr("unreject / unrate"); Layout.fillWidth: true; enabled: !controller.busy; onClicked: window.chooseImageAction("unrate") }
            TouchButton { showLabel: true; iconName: "trash"; text: qsTr("remove from collection"); Layout.fillWidth: true; enabled: !controller.busy; onClicked: window.chooseImageAction("remove") }
            TouchButton { showLabel: true; iconName: "trash"; text: qsTr("delete from disk"); Layout.fillWidth: true; enabled: !controller.busy; onClicked: window.chooseImageAction("trash") }
            }
        }
    }
    TouchDialog {
        id: confirmImageAction
        objectName: "confirmImageAction"
        title: window.pendingImageAction === "trash" ? qsTr("delete from disk") : qsTr("remove from collection")
        anchors.centerIn: parent; width: Math.min(560, window.width - 32)
        padding: 20; modal: true; focus: true
        background: Rectangle { color: "#121a1e"; radius: 16; border.color: "#334249" }
        contentItem: ColumnLayout {
            spacing: 16
            Label { text: window.actionImage.filename || ""; textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#e6eef0"; font.pixelSize: 20 }
            Label {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#aebbc1"
                text: window.pendingImageAction === "trash"
                    ? qsTr("move this photo and its sidecars to the Recycle Bin? if other catalog versions use the source, only this version and its sidecars are removed")
                    : qsTr("remove this photo from the library and all collections? the original file and sidecars stay on disk")
            }
            RowLayout {
                Layout.fillWidth: true
                TouchButton {
                    text: window.pendingImageAction === "trash" ? qsTr("move to Recycle Bin") : qsTr("remove")
                    Layout.fillWidth: true; enabled: !controller.busy
                    onClicked: { confirmImageAction.close(); controller.libraryCommand(window.pendingImageAction, {}, window.actionImage.imgid) }
                }
            }
        }
    }
    TouchDialog {
        id: quickSettings
        title: qsTr("quick settings"); anchors.centerIn: parent; width: Math.min(540, window.width - 32); modal: true; focus: true
        contentItem: ColumnLayout {
            Label { text: qsTr("photo strip"); color: ui.muted }
            TouchComboBox { Layout.fillWidth: true; implicitHeight: ui.touchHeight; model: [qsTr("hidden"), qsTr("compact"), qsTr("expanded")]; currentIndex: ["hidden", "compact", "expanded"].indexOf(window.filmstripState); onActivated: { window.filmstripState = ["hidden", "compact", "expanded"][currentIndex]; window.requestedFilmstripHeight = 0 } }
            Switch { text: qsTr("hide photo strip automatically"); checked: window.autoHideFilmstrip; implicitHeight: ui.touchHeight; onToggled: { window.autoHideFilmstrip = checked; if(checked) filmstripTimer.restart() } }
            TouchButton { text: qsTr("switch to classic"); iconName: "grid"; Layout.fillWidth: true; visible: managedSession; enabled: controller.ready && !controller.busy; onClicked: { quickSettings.close(); saveView(); controller.switchInterface("classic") } }
            Switch { text: qsTr("show rejected images"); checked: controller.showRejected; implicitHeight: 64; onToggled: controller.showRejected = checked }
            Switch { text: qsTr("next image after rejecting"); checked: controller.advanceAfterReject; implicitHeight: 64; onToggled: controller.advanceAfterReject = checked }
        }
    }
    HistoryPasteDialog { id: historyPasteDialog; photoController: controller }
    TouchDialog {
        id: resetHistoryDialog
        objectName: "resetHistoryDialog"
        property int imageId: 0
        property string scope: "current"
        readonly property int targetCount: controller.images.filter(function(image) {
            if(scope === "current") return image.imgid === (imageId || controller.imageId)
            return scope === "collection" || window.selectedImages.indexOf(image.imgid) >= 0
        }).length
        title: qsTr("reset adjustments")
        modal: true; focus: true; anchors.centerIn: parent; width: Math.min(560, window.width - 32)
        contentItem: ColumnLayout {
            spacing: 16
            TouchComboBox {
                Layout.fillWidth: true; implicitHeight: 64
                model: [qsTr("current photo"), qsTr("selected photos"), qsTr("visible collection")]
                currentIndex: ["current", "selected", "collection"].indexOf(resetHistoryDialog.scope)
                onActivated: resetHistoryDialog.scope = ["current", "selected", "collection"][currentIndex]
            }
            Label { text: qsTr("reset adjustments on %1 photos to the engine defaults? original files, ratings, and collection membership are kept. undo restores the previous history in this session").arg(resetHistoryDialog.targetCount); wrapMode: Text.Wrap; Layout.fillWidth: true; color: "#aebbc1" }
            TouchButton { objectName: "resetHistoryButton"; text: qsTr("reset adjustments"); iconName: "reset"; showLabel: true; Layout.fillWidth: true; enabled: !controller.busy && resetHistoryDialog.targetCount > 0; onClicked: { controller.historyCommand("reset", {scope: resetHistoryDialog.scope}, resetHistoryDialog.imageId); resetHistoryDialog.close() } }
        }
    }
    ExportDialog { id: batchExportDialog; photoController: controller }
    LibraryDialog { id: collectionDialog; objectName: "collectionDialog"; photoController: controller }
    ImportReview { id: importReview; objectName: "importReview"; photoController: controller }
    TouchDialog {
        id: importSource
        modal: true; focus: true; anchors.centerIn: parent; title: qsTr("add to library")
        width: Math.min(480, window.width - 32); padding: 20
        background: Rectangle { color: "#121a1e"; radius: 16; border.color: "#334249" }
        contentItem: ColumnLayout {
            spacing: 12
            Label { text: qsTr("choose a source, then review which photos to add"); wrapMode: Text.WordWrap; Layout.fillWidth: true; color: "#aebbc1" }
            TouchButton { text: qsTr("choose photos"); Layout.fillWidth: true; onClicked: { importSource.close(); importDialog.open() } }
            TouchButton { iconName: "folder"; showLabel: true; text: qsTr("choose folder"); Layout.fillWidth: true; onClicked: { importSource.close(); folderDialog.open() } }
            TouchButton { text: qsTr("include subfolders"); checked: window.importRecursive; Layout.fillWidth: true; onClicked: window.importRecursive = !window.importRecursive }
        }
    }
    FileDialog {
        id: importDialog
        title: qsTr("import photos")
        fileMode: FileDialog.OpenFiles
        onAccepted: controller.reviewImport(selectedFiles, false)
    }
    TouchPopup {
        id: toolActions
        anchors.centerIn: parent
        width: 320; padding: 20; modal: true; focus: true
        background: Rectangle { color: "#1c272c"; radius: 16; border.color: "#334249" }
        ColumnLayout {
            width: parent.width; spacing: 10
            Label { text: controller.toolLabel; Layout.fillWidth: true; color: "#e6eef0"; font.pixelSize: 20 }
            TouchButton { iconName: "reset"; showLabel: true; text: qsTr("reset tool"); Layout.fillWidth: true; onClicked: { toolActions.close(); controller.reset() } }
            TouchButton {
                text: window.advanced ? qsTr("fewer controls") : qsTr("all controls")
                Layout.fillWidth: true; visible: controller.operation === "exposure"
                checked: window.advanced; onClicked: { window.advanced = !window.advanced; toolActions.close() }
            }
        }
    }
    FolderDialog {
        id: folderDialog
        title: qsTr("import a photo folder")
        onAccepted: controller.reviewImport([selectedFolder], window.importRecursive)
    }
    FileDialog {
        id: exportDialog
        title: qsTr("export photo")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("JPEG image (*.jpg)"), qsTr("PNG image (*.png)"), qsTr("TIFF image (*.tif)")]
        defaultSuffix: "jpg"
        onAccepted: {
            const path = selectedFile.toString().toLowerCase()
            controller.exportTo(selectedFile, path.endsWith(".png") ? "png" : path.endsWith(".tif") || path.endsWith(".tiff") ? "tiff" : "jpeg")
        }
    }
    TouchPopup {
        id: quickMenu
        objectName: "quickMenu"
        x: Math.max(8, window.width - width - 12); y: controller.editorVisible ? 8 : ui.touchHeight + 16
        width: Math.min(420, window.width - 24); height: Math.min(quickColumn.implicitHeight + topPadding + bottomPadding, window.height - 88)
        padding: ui.inset; modal: true; focus: true
        background: Rectangle { color: ui.floating; radius: ui.radius }
        ScrollView {
            id: quickScroll
            anchors.fill: parent; contentWidth: availableWidth; clip: true
            ColumnLayout {
                id: quickColumn
                width: parent.width; spacing: ui.gap
                Label { text: qsTr("quick actions"); color: ui.text; font.pixelSize: ui.titleSize }
                GridLayout {
                    Layout.fillWidth: true; columns: 2; columnSpacing: ui.gap; rowSpacing: ui.gap
                    TouchButton { text: controller.before ? qsTr("after") : qsTr("before"); iconName: "compare"; Layout.fillWidth: true; checked: controller.before; enabled: controller.imageId > 0 && !controller.cropEditing; onClicked: { controller.before = !controller.before; quickMenu.close() } }
                    TouchButton { text: qsTr("fit image"); iconName: "fit"; Layout.fillWidth: true; enabled: controller.imageId > 0; onClicked: { window.viewZoom = 1; clampPan(); quickMenu.close() } }
                    TouchButton { text: qsTr("edit history"); iconName: "history"; Layout.fillWidth: true; enabled: controller.imageId > 0; onClicked: { quickMenu.close(); showHistory() } }
                    TouchButton { text: qsTr("copy edits"); iconName: "copy"; Layout.fillWidth: true; enabled: controller.imageId > 0 && !controller.busy; onClicked: { quickMenu.close(); controller.historyCommand("copy") } }
                    TouchButton { text: qsTr("paste edits"); iconName: "paste"; Layout.fillWidth: true; enabled: !!controller.copiedHistory.source && !controller.busy; onClicked: { quickMenu.close(); historyPasteDialog.imageId = controller.imageId; historyPasteDialog.scope = window.selectionMode ? "selected" : "current"; historyPasteDialog.open() } }
                    TouchButton { text: qsTr("settings"); iconName: "gear"; Layout.fillWidth: true; onClicked: { quickMenu.close(); quickSettings.open() } }
                }
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#435259" }
                TouchButton { text: qsTr("reset adjustments"); iconName: "reset"; Layout.fillWidth: true; flat: true; enabled: controller.imageId > 0 && !controller.busy; onClicked: { quickMenu.close(); showResetHistory(controller.imageId) } }
            }
        }
    }
    TouchPopup {
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
    TouchPopup {
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
                    onClicked: { window.toolsOpen = true; window.distractionFree = false; controller.selectTool(modelData.operation); toolSearch.close() }
                }
            }
        }
    }
}
