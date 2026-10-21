import QtQuick

CarText {
    readonly property bool musicText:true
    wrapMode:Text.NoWrap
    // Music surfaces are opaque and dark, independent of the desktop wallpaper.
    color:subdued?"#b3b3b3":"#ffffff"
}
