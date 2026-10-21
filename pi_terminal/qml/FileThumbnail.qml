import QtQuick

Item {
    id: root
    property var entry: ({})
    property string relative: ""
    property string fallback: "description"
    property string thumbnail: ""
    function refresh() {
        thumbnail = ""
        if (entry.directory || !relative) return
        if (/\.(png|jpe?g|bmp|gif|webp)$/i.test(entry.name)) thumbnail = systemTools.fileUrl(relative).toString()
        else if (/\.(mp3|flac|m4a|ogg|opus|aac)$/i.test(entry.name)) thumbnail = systemTools.localMusicMetadata(relative).coverUrl || ""
    }
    onRelativeChanged: refresh()
    Component.onCompleted: refresh()
    Image { id: image; anchors.fill: parent; source: root.thumbnail; asynchronous: true; autoTransform: true; sourceSize: Qt.size(128, 128); fillMode: Image.PreserveAspectFit }
    MaterialIcon { anchors.fill: parent; name: root.fallback; visible: image.status !== Image.Ready }
}
