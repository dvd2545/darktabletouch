import QtQuick

Image {
    id: thumbnail
    required property int imageId
    property int revision: controller.thumbnailVersion(imageId)
    onImageIdChanged: { revision = controller.thumbnailVersion(imageId); controller.requestThumbnail(imageId) }
    source: "image://photos/thumb/" + imageId + "?revision=" + revision
    fillMode: Image.PreserveAspectFit
    asynchronous: false; cache: true; retainWhileLoading: true
    Component.onCompleted: controller.requestThumbnail(imageId)
    Connections {
        target: controller
        function onThumbnailChanged(id) { if(id === thumbnail.imageId) thumbnail.revision = controller.thumbnailVersion(id) }
        function onStateChanged() { if(controller.ready) controller.requestThumbnail(thumbnail.imageId) }
    }
}
