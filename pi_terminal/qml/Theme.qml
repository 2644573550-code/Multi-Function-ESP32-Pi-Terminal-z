pragma Singleton
import QtQuick

QtObject {
    function textOn(background) {
        function linear(v){return v<=0.04045?v/12.92:Math.pow((v+0.055)/1.055,2.4)}
        const luminance=0.2126*linear(background.r)+0.7152*linear(background.g)+0.0722*linear(background.b)
        return luminance>0.179?"#000000":"#ffffff"
    }
    function blend(front,back,alpha){return Qt.rgba(front.r*alpha+back.r*(1-alpha),front.g*alpha+back.g*(1-alpha),front.b*alpha+back.b*(1-alpha),1)}
    // Logical pixels are dp. Never shrink fonts or hit targets below the floors.
    property real scaleFactor: 1
    readonly property real gap: 16 * scaleFactor
    readonly property real margin: 24 * scaleFactor
    readonly property real hit: Math.max(48, 64 * scaleFactor)
    readonly property real body: 18 * scaleFactor
    readonly property real title: 24 * scaleFactor
    readonly property real hero: 32 * scaleFactor
    // 像素语义的字号 token：CarText/CarButton 的 fontPixelSize 通道按逻辑像素渲染
    // （内部换算成 pt），与上面 pt 语义的 body/title/hero 是两条通道，不要混着写。
    // 时间页的说明文字、城市时间与读数都走这条通道，数值与原字号一一对应。
    readonly property real fontCaption: 12 * scaleFactor
    readonly property real fontLabel: 13 * scaleFactor
    readonly property real fontStrong: 22 * scaleFactor
    readonly property real fontDisplay: 24 * scaleFactor
    readonly property real icon: 24 * scaleFactor
    readonly property real shapeSmall: 8 * scaleFactor
    readonly property real shapeMedium: 12 * scaleFactor
    readonly property real shapeLarge: 16 * scaleFactor
    readonly property real shapeExtraLarge: 28 * scaleFactor
    readonly property real shapeFull: 999 * scaleFactor
    readonly property color background: "#08121f"
    readonly property color surface: "#102238"
    readonly property color surfaceContainer: "#162b40"
    readonly property color surfaceContainerHigh: "#1d344a"
    readonly property color primaryContainer: "#16465a"
    readonly property color raised: "#203c56"
    readonly property color pressed: "#294965"
    readonly property color foreground: "#ffffff"
    readonly property color secondary: "#ffffff"
    readonly property color accent: "#85deff"
    readonly property color accentInk: "#081b29"
    readonly property color disabledSurface: "#182a3c"
    readonly property color disabledText: "#b6c8db"
    readonly property color warning: "#ffdaa0"
    readonly property color outline: "#6e9abb"
    readonly property color outlineVariant: "#3b536a"
}
