import QtQuick
import QtQuick.Window
import QtQuick.Controls

Label {
    id:label
    property bool heading: false
    property bool hero: false
    property bool subdued: false
    property bool auditText: true
    property real fontPixelSize: -1
    // Keep one font unit active. Explicit pixel-sized captions retain their
    // visual size without inheriting a conflicting pointSize from this type.
    font.pointSize: fontPixelSize > 0 ? fontPixelSize * 72 / (Screen.logicalPixelDensity * 25.4)
        : hero ? Theme.hero : heading ? Theme.title : Theme.body
    font.weight: heading || hero ? Font.DemiBold : Font.Normal
    color: {
        const window=ApplicationWindow.window
        return window && typeof window.textBackground==="function" ? Theme.textOn(window.textBackground(label)) : Theme.textOn(Theme.background)
    }
    wrapMode: maximumLineCount === 1 ? Text.NoWrap : Text.Wrap
    Accessible.role: Accessible.StaticText
    Accessible.name: text
}
