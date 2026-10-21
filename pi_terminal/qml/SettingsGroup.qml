import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property string iconName: "settings"
    property string title: ""
    property string subtitle: ""
    property color accentColor: Theme.accent
    property int titleTapCount: 0
    property double titleTapStarted: 0
    property bool titleTapEnabled: false
    onVisibleChanged: { if (!visible) titleTapCount = 0 }
    onTitleTapEnabledChanged: { titleTapCount = 0 }
    signal titleTapped()
    function registerTitleTap() {
        const now = Date.now()
        if (now - titleTapStarted > 1800) titleTapCount = 0
        titleTapStarted = now
        if (++titleTapCount >= 5) {
            titleTapCount = 0
            titleTapped()
        }
    }
    default property alias content: body.data
    radius: Theme.shapeLarge
    color: Theme.surface
    border.color: Theme.outlineVariant
    implicitHeight: column.implicitHeight + Theme.gap * 2
    Column {
        id: column
        x: Theme.gap
        y: Theme.gap
        width: Math.max(1, root.width - Theme.gap * 2)
        spacing: Theme.gap
        Item {
            width: column.width
            implicitHeight: Math.max(48 * Theme.scaleFactor, headingColumn.implicitHeight)
            Rectangle {
                width: 48 * Theme.scaleFactor; height: width
                anchors.verticalCenter: parent.verticalCenter
                radius: width / 2
                color: Qt.darker(root.accentColor, 2.1)
                border.color: Qt.lighter(root.accentColor, 1.15)
                MaterialIcon { anchors.centerIn: parent; width: 25 * Theme.scaleFactor; height: width; name: root.iconName; light: true }
            }
            Column {
                id: headingColumn
                x: 48 * Theme.scaleFactor + Theme.gap
                width: Math.max(1, column.width - 48 * Theme.scaleFactor - Theme.gap)
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2 * Theme.scaleFactor
                CarText {
                    objectName: "settingsGroupTitle"
                    text: root.title; heading: true; width: parent.width
                    height: Math.max(implicitHeight, root.titleTapEnabled ? 48 : 0)
                    MouseArea {
                        anchors.fill: parent
                        enabled: root.titleTapEnabled
                        acceptedButtons: Qt.LeftButton
                        onClicked: root.registerTitleTap()
                        // Qt sends doubleClicked instead of clicked for the
                        // second press of a native mouse double-click.
                        onDoubleClicked: root.registerTitleTap()
                    }
                }
                CarText { visible: root.subtitle !== ""; text: root.subtitle; subdued: true; width: parent.width; wrapMode: Text.Wrap }
            }
        }
        ColumnLayout { id: body; width: column.width; spacing: Theme.gap / 2 }
    }
}
