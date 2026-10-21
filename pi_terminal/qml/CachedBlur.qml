import QtQuick
import QtQuick.Window
import Terminal.Native 1.0

// A cached surface is a scene-graph quad, not another framebuffer. BlurItem
// remains the sole blur algorithm and is used until the shared atlas is ready.
Item {
    id: root
    property Item source
    property bool preblurred: false
    property rect sourceRect: Qt.rect(0,0,1,1)
    property int levels: 4
    property real resolutionScale: 1
    property real strength: 1
    property real mixAmount: 1
    property real cornerRadius: 0
    Loader {
        anchors.fill: parent
        active: !root.preblurred
        sourceComponent: Component {
            BlurItem {
                source: root.source
                levels: root.levels; resolutionScale: root.resolutionScale
                strength: root.strength; mixAmount: root.mixAmount; cornerRadius: root.cornerRadius
            }
        }
    }
    ShaderEffect {
        anchors.fill: parent
        visible: root.preblurred
        property var source: root.source
        property vector4d sampleRect: Qt.vector4d(root.sourceRect.x,root.sourceRect.y,root.sourceRect.width,root.sourceRect.height)
        property vector2d outputSize: Qt.vector2d(width*root.resolutionScale*Screen.devicePixelRatio,height*root.resolutionScale*Screen.devicePixelRatio)
        property real cornerRadius: root.cornerRadius*root.resolutionScale*Screen.devicePixelRatio
        fragmentShader: "qrc:/shaders/cached-blur.frag.qsb"
    }
}
