import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

TouchDialog {
    id: libraryDialog
    required property var photoController
    property int page: 0
    property int browseProperty: 1
    property bool showCollectionRules: false
    property bool showSort: false
    property bool showFilters: false
    property string removePath: ""
    property string errorText: ""
    property string search: ""
    property var expanded: ({})
    property var draft: ({collection: [], filtering: [], sort: []})
    property var selectedFolders: ({})
    property alias focusColumn: ruleColumn
    readonly property var focusViewport: scroll.contentItem
    readonly property var properties: photoController.library.properties || []
    readonly property var folders: photoController.library.folders || []
    readonly property bool folderMode: browseProperty === 0 || browseProperty === 1
    readonly property var values: photoController.collectionValues.property === browseProperty ? photoController.collectionValues.values || [] : []
    readonly property var sortNames: [qsTr("filename"), qsTr("capture time"), qsTr("import time"),
        qsTr("modification time"), qsTr("export time"), qsTr("print time"), qsTr("rating"),
        qsTr("image ID"), qsTr("color label"), qsTr("group"), qsTr("path"), qsTr("custom order"),
        qsTr("title"), qsTr("description"), qsTr("aspect ratio"), qsTr("shuffle")]
    modal: true; focus: true
    title: qsTr("collections and filters")
    width: Math.min(880, parent.width - 32); height: Math.min(700, parent.height - 32)
    anchors.centerIn: parent; padding: 16
    background: Rectangle { color: "#121a1e"; radius: 16; border.color: "#334249" }

    function propertyIndex(item) {
        for(let i = 0; i < properties.length; ++i) if(properties[i].item === item) return i
        return -1
    }
    function change(group, index, key, value) {
        const next = JSON.parse(JSON.stringify(draft)); next[group][index][key] = value; draft = next
        if(group === "collection") syncFolders()
    }
    function remove(group, index) {
        const next = JSON.parse(JSON.stringify(draft)); next[group].splice(index, 1); draft = next
        if(group === "collection") syncFolders()
    }
    function add(group) {
        const next = JSON.parse(JSON.stringify(draft))
        next[group].push(group === "sort" ? {item: 0, order: 0} : {item: 2, mode: 0, off: 0, top: 0, string: ""})
        draft = next
    }
    function rule(item, text, mode) { return {item: item, string: text, mode: mode, off: 0, top: 0} }
    function normalize(path) { return path.replace(/\\/g, "/").replace(/\/$/, "") }
    function syncFolders() {
        const next = {}
        for(const folder of folders) {
            const key = normalize(folder.folder)
            let selected = false
            for(const entry of draft.collection) {
                if(entry.item !== 0 && entry.item !== 1) continue
                const value = normalize(entry.string)
                const matches = value === "%" || value === key || (value.endsWith("*") && (key === value.slice(0, -1) || key.startsWith(value.slice(0, -1) + "/")))
                if(matches) selected = entry.mode !== 2
            }
            next[key] = selected
        }
        selectedFolders = next
    }
    function tree(includeCollapsed) {
        const nodes = {}, roots = []
        for(const folder of folders) {
            const key = normalize(folder.folder), pieces = key.split("/").filter(function(s) { return s.length })
            let path = key.startsWith("//") ? "/" : "", parent = null
            for(const piece of pieces) {
                path += (path ? "/" : "") + piece
                if(!nodes[path]) {
                    nodes[path] = {key: path, label: piece, children: [], direct: 0, count: 0, source: path}
                    if(parent) parent.children.push(nodes[path]); else roots.push(nodes[path])
                }
                parent = nodes[path]
            }
            if(parent) { parent.direct += folder.images; parent.source = folder.folder }
        }
        function count(node) { node.count = node.direct; for(const child of node.children) node.count += count(child); return node.count }
        for(const root of roots) count(root)
        let visibleRoots = roots
        while(visibleRoots.length === 1 && !visibleRoots[0].direct && visibleRoots[0].children.length === 1) visibleRoots = visibleRoots[0].children
        const rows = []
        function visit(node, depth) {
            const matches = !search || node.key.toLowerCase().includes(search.toLowerCase())
            if(includeCollapsed || matches || node.children.length) rows.push({key: node.key, label: node.label, source: node.source, count: node.count, depth: depth, children: node.children.length})
            if(includeCollapsed || search || expanded[node.key] !== false) {
                node.children.sort(function(a, b) { return a.label.localeCompare(b.label) })
                for(const child of node.children) visit(child, depth + 1)
            }
        }
        for(const root of visibleRoots) visit(root, 0)
        return rows
    }
    function folderState(key) {
        let count = 0, checked = 0
        for(const folder of folders) {
            const path = normalize(folder.folder)
            if(path === key || path.startsWith(key + "/")) { count++; if(selectedFolders[path]) checked++ }
        }
        return !checked ? Qt.Unchecked : checked === count ? Qt.Checked : Qt.PartiallyChecked
    }
    function toggleFolder(key, checked) {
        const selected = Object.assign({}, selectedFolders)
        for(const folder of folders) {
            const path = normalize(folder.folder)
            if(path === key || path.startsWith(key + "/")) selected[path] = checked
        }
        const rules = [], covered = []
        for(const node of tree(true).sort(function(a, b) { return a.key.length - b.key.length })) {
            if(covered.some(function(path) { return node.key === path || node.key.startsWith(path + "/") })) continue
            const descendants = folders.filter(function(folder) { const path = normalize(folder.folder); return path === node.key || path.startsWith(node.key + "/") })
            if(descendants.length && descendants.every(function(folder) { return selected[normalize(folder.folder)] })) {
                const nativePath = folders.length && folders[0].folder.includes("\\") ? node.key.replace(/\//g, "\\") : node.key
                rules.push(rule(1, nativePath + "*", rules.length ? 1 : 0)); covered.push(node.key)
            } else {
                const direct = folders.find(function(folder) { return normalize(folder.folder) === node.key })
                if(direct && selected[node.key]) rules.push(rule(0, direct.folder, rules.length ? 1 : 0))
            }
        }
        if(rules.length > 10) { errorText = qsTr("this selection needs more than 10 collection rules; select a parent folder to combine them"); selectedFolders = Object.assign({}, selectedFolders); return }
        if(!rules.length) rules.push(rule(0, "%", 2))
        const next = JSON.parse(JSON.stringify(draft)); next.collection = rules; draft = next; selectedFolders = selected; errorText = ""
    }
    function valueChecked(value) { return draft.collection.some(function(entry) { return entry.item === browseProperty && entry.string === value && entry.mode !== 2 }) }
    function toggleValue(value, checked) {
        let rules = draft.collection.filter(function(entry) { return entry.item === browseProperty && entry.mode !== 2 && entry.string !== value })
        if(checked) rules.push(rule(browseProperty, value, rules.length ? 1 : 0))
        if(rules.length > 10) { errorText = qsTr("a collection supports up to 10 rules"); return }
        rules.forEach(function(entry, index) { entry.mode = index ? 1 : 0 })
        if(!rules.length) rules = [rule(0, "%", 2)]
        const next = JSON.parse(JSON.stringify(draft)); next.collection = rules; draft = next; errorText = ""
    }
    function loadValues() { if(!folderMode) photoController.libraryCommand("values", {property: browseProperty}, 0) }
    function resetAll() {
        draft = {collection: [rule(0, "%", 0)], filtering: [], sort: [{item: 0, order: 0}]}
        syncFolders(); errorText = ""
    }
    onPageChanged: if(page === 3) showSort = true
    onOpened: {
        const state = photoController.library
        draft = JSON.parse(JSON.stringify({collection: state.collection || [], filtering: state.filtering || [], sort: state.sort || []}))
        if(!draft.collection.length) resetAll()
        showFilters = draft.filtering.some(function(rule) { return !rule.off && !!rule.string })
        syncFolders(); errorText = ""; loadValues()
    }
    contentItem: ColumnLayout {
        spacing: 10
        ScrollView {
            id: scroll
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true; contentWidth: availableWidth
            ColumnLayout {
                id: ruleColumn
                width: scroll.availableWidth; spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("filter rules"); color: "#00dbe5"; font.pixelSize: 18; Layout.fillWidth: true }
                    TouchButton { text: qsTr("edit filters (%1)").arg(libraryDialog.draft.filtering.length); checked: libraryDialog.showFilters; onClicked: libraryDialog.showFilters = !libraryDialog.showFilters }
                }
                RuleEditor { visible: libraryDialog.showFilters; Layout.fillWidth: true; dialog: libraryDialog; group: "filtering" }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("collection"); color: "#00dbe5"; font.pixelSize: 18; Layout.fillWidth: true }
                    TouchButton { text: qsTr("edit rules"); checked: libraryDialog.showCollectionRules; onClicked: libraryDialog.showCollectionRules = !libraryDialog.showCollectionRules }
                    TouchButton { iconName: "settings"; text: qsTr("sort"); checked: libraryDialog.showSort; onClicked: libraryDialog.showSort = !libraryDialog.showSort }
                }
                RuleEditor { visible: libraryDialog.showCollectionRules; Layout.fillWidth: true; dialog: libraryDialog; group: "collection" }
                RuleEditor { visible: libraryDialog.showSort; Layout.fillWidth: true; dialog: libraryDialog; group: "sort" }
                RowLayout {
                    Layout.fillWidth: true
                    TouchComboBox {
                        Layout.preferredWidth: 260; implicitHeight: 64; model: libraryDialog.properties; textRole: "label"
                        currentIndex: libraryDialog.propertyIndex(libraryDialog.browseProperty)
                        Accessible.name: qsTr("collection property")
                        onActivated: { libraryDialog.browseProperty = libraryDialog.properties[currentIndex].item; libraryDialog.search = ""; libraryDialog.loadValues() }
                        delegate: ItemDelegate { required property var modelData; width: parent.width; height: 64; text: modelData.label }
                    }
                    TextField {
                        Layout.fillWidth: true; implicitHeight: 64; placeholderText: qsTr("search collection values")
                        text: libraryDialog.search; onTextEdited: libraryDialog.search = text
                    }
                }
                ListView {
                    id: valueList
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(contentHeight, Math.max(128, libraryDialog.height * 0.45))
                    clip: true; boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    model: libraryDialog.folderMode ? libraryDialog.tree() : libraryDialog.values.filter(function(value) { return !libraryDialog.search || value.value.toLowerCase().includes(libraryDialog.search.toLowerCase()) })
                    delegate: RowLayout {
                        required property var modelData
                        required property int index
                        width: valueList.width; height: 64; spacing: 4
                        Item { visible: libraryDialog.folderMode; Layout.preferredWidth: Math.min(6, modelData.depth || 0) * 18 }
                        TouchButton {
                            visible: libraryDialog.folderMode
                            text: qsTr("expand or collapse folder"); iconName: libraryDialog.expanded[modelData.key] === false ? "chevron-right" : "chevron-down"
                            enabled: !!modelData.children; opacity: modelData.children ? 1 : 0; implicitWidth: 64
                            Accessible.name: qsTr("expand or collapse %1").arg(modelData.label || "")
                            onClicked: { const next = Object.assign({}, libraryDialog.expanded); next[modelData.key] = next[modelData.key] === false; libraryDialog.expanded = next }
                            onActiveFocusChanged: if(activeFocus) valueList.positionViewAtIndex(index, ListView.Contain)
                        }
                        CheckBox {
                            Layout.fillWidth: true; implicitHeight: 64; focusPolicy: Qt.StrongFocus
                            text: (libraryDialog.folderMode ? modelData.label : modelData.value) + "  (" + modelData.count + ")"
                            tristate: libraryDialog.folderMode
                            checkState: libraryDialog.folderMode ? libraryDialog.folderState(modelData.key) : libraryDialog.valueChecked(modelData.value) ? Qt.Checked : Qt.Unchecked
                            nextCheckState: function() { return checkState === Qt.Checked ? Qt.Unchecked : Qt.Checked }
                            onClicked: {
                                if(libraryDialog.folderMode) libraryDialog.toggleFolder(modelData.key, checkState === Qt.Checked)
                                else libraryDialog.toggleValue(modelData.value, checkState === Qt.Checked)
                            }
                            onActiveFocusChanged: if(activeFocus) valueList.positionViewAtIndex(index, ListView.Contain)
                            ToolTip.visible: hovered; ToolTip.text: libraryDialog.folderMode ? modelData.key : modelData.value
                        }
                        TouchButton {
                            visible: libraryDialog.folderMode
                            iconName: "trash"; text: qsTr("remove folder from catalog"); implicitWidth: 64
                            onClicked: { libraryDialog.removePath = modelData.key; removeDialog.open() }
                        }
                    }
                }
                Label { visible: !libraryDialog.folderMode && !libraryDialog.values.length; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#aebbc1"; text: qsTr("no values available; use edit rules to enter a matching value") }
                Label { visible: !!libraryDialog.errorText; text: libraryDialog.errorText; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#ffa99e" }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            TouchButton { iconName: "reset"; text: qsTr("reset all"); onClicked: libraryDialog.resetAll() }
            TouchButton { text: qsTr("remove collection from catalog"); iconName: "trash"; onClicked: { libraryDialog.removePath = ""; removeDialog.open() } }
            Item { Layout.fillWidth: true }
            TouchButton { iconName: "close"; text: qsTr("cancel"); onClicked: libraryDialog.close() }
            TouchButton { iconName: "check"; text: qsTr("apply"); checked: true; enabled: libraryDialog.photoController.ready && !libraryDialog.photoController.busy; onClicked: { libraryDialog.photoController.libraryCommand("collection", libraryDialog.draft); libraryDialog.close() } }
        }
    }
    TouchDialog {
        id: removeDialog
        parent: Overlay.overlay; anchors.centerIn: parent; width: Math.min(560, parent.width - 32)
        title: qsTr("remove from catalog"); modal: true; focus: true
        contentItem: ColumnLayout {
            Label { text: libraryDialog.removePath || qsTr("current collection"); textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#e6eef0" }
            Label { text: qsTr("remove these photos from the catalog? folder removal includes imported subfolders and all their photos, including filtered or rejected images. original files and sidecars stay on disk"); Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#aebbc1" }
            RowLayout {
                TouchButton { text: qsTr("cancel"); iconName: "close"; onClicked: removeDialog.close() }
                TouchButton { text: qsTr("remove"); iconName: "trash"; enabled: !photoController.busy; onClicked: { photoController.libraryCommand(libraryDialog.removePath ? "remove-folder" : "remove-collection", {folder: libraryDialog.removePath}); removeDialog.close(); libraryDialog.close() } }
            }
        }
    }

}
