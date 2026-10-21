import QtQuick
import QtQuick.Controls

Button {
    id:key
    implicitWidth:64;implicitHeight:48
    font.pixelSize:18
    padding:4;leftPadding:4;rightPadding:4;topPadding:4;bottomPadding:4;topInset:0;bottomInset:0;leftInset:0;rightInset:0
    background:Rectangle {radius:6;color:key.down?Theme.pressed:Theme.raised;border.color:key.activeFocus?Theme.accent:Theme.outlineVariant;border.width:1}
    contentItem:Text {text:key.text;font:key.font;color:Theme.foreground;horizontalAlignment:Text.AlignHCenter;verticalAlignment:Text.AlignVCenter;elide:Text.ElideRight;maximumLineCount:1}
}
