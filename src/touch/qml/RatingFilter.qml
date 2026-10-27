import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: control
    required property var photoController
    property var rules: photoController.library.filtering || []
    property bool deferred: false
    signal edited(var filtering)
    readonly property int ratingProperty: Number(photoController.library.rating_property)
    readonly property int rangeProperty: Number(photoController.library.rating_range_property)
    property string errorText: ""
    UiMetrics { id: metrics }
    spacing: metrics.gap

    function isRating(rule) { return rule.item === ratingProperty || rule.item === rangeProperty }
    function matches(value, expression) {
        const range = expression.match(/^\s*\[\s*(-?\d+)\s*;\s*(-?\d+)\s*\]\s*$/)
        if(range) return value >= Number(range[1]) && value <= Number(range[2])
        const match = expression.match(/^\s*(<=|>=|<>|=|<|>)?\s*(-?\d+)\s*$/)
        if(!match) return true
        const number = Number(match[2]), op = match[1] || "="
        if(op === "<>") return value !== number
        if(op === "<=") return value <= number
        if(op === ">=") return value >= number
        if(op === "<") return value < number
        if(op === ">") return value > number
        return value === number
    }
    function allowed(value) {
        let result = true, first = true
        for(const rule of rules) {
            if(!isRating(rule) || rule.off || !rule.string) continue
            const match = matches(value, rule.string)
            result = rule.mode === 2 ? result && !match : rule.mode === 1 && !first ? result || match : result && match
            first = false
        }
        return result
    }
    function setAllowed(value, checked) {
        const enabled = []
        for(let rating = -1; rating <= 5; rating++) enabled.push(rating === value ? checked : allowed(rating))
        const state = photoController.library
        const filtering = rules.filter(function(rule) { return !isRating(rule) })
        // exclusions intersect with other filters without an OR escaping their scope
        for(let rating = -1; rating <= 5; rating++)
            if(!enabled[rating + 1]) filtering.push({item: ratingProperty, string: "=" + rating, mode: 2, off: 0, top: 0})
        if(filtering.length > 10) { errorText = qsTr("remove another filter before adding more rating choices"); return }
        errorText = ""
        if(deferred) edited(filtering)
        else photoController.libraryCommand("collection", {collection: state.collection, filtering: filtering, sort: state.sort}, 0)
    }
    Label { text: qsTr("rating filter"); color: metrics.muted }
    Flow {
        Layout.fillWidth: true
        spacing: metrics.gap
        Repeater {
            model: 7
            TouchButton {
                required property int index
                objectName: "ratingChoice" + (index - 1)
                text: index === 0 ? qsTr("rejected") : index === 2 ? qsTr("1 star") : qsTr("%1 stars").arg(index - 1)
                checked: control.allowed(index - 1)
                enabled: control.photoController.ready && !control.photoController.busy
                onClicked: control.setAllowed(index - 1, !checked)
            }
        }
    }
    Label { visible: !!control.errorText; text: control.errorText; color: metrics.text; wrapMode: Text.Wrap; Layout.fillWidth: true }
}
