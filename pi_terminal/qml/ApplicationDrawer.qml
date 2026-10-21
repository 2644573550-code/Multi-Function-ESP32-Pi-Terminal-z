import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Drawer {
    id: root
    objectName: "applicationDrawer"
    parent: Overlay.overlay
    edge: Qt.BottomEdge
    property var model
    property real uiScale: 1
    property real hostWidth: 1024
    property real hostHeight: 600
    signal entryActivated(var entry, real originX, real originY)

    x: 0
    width: parent ? parent.width : hostWidth
    height: Math.min((parent ? parent.height : hostHeight) - Theme.gap, 560 * uiScale)
    padding: Theme.shapeExtraLarge
    modal: true
    dragMargin: 0
    background: Item {
        GlassSurface { anchors.fill:parent;color:Theme.surface;radius:Theme.shapeExtraLarge }
    }
    Overlay.modal: Rectangle { color: "#99040b14" }

    function auditSnapshot() {
        return JSON.stringify({visible: opened, width: width, height: height,
            gridWidth: appDrawerGrid.width, gridHeight: appDrawerGrid.height,
            cellWidth: appDrawerGrid.cellWidth, cellHeight: appDrawerGrid.cellHeight,
            count: model ? model.count : 0, contentHeight: appDrawerGrid.contentHeight})
    }

    contentItem: ColumnLayout {
        spacing: Theme.gap / 2
        RowLayout {
            Layout.fillWidth: true
            CarText { text: I18n.t("应用"); heading: true; Layout.fillWidth: true }
            CarText { text: (root.model ? root.model.count : 0) + I18n.t(" 个应用"); subdued: true }
            IconButton { iconName: "close"; label: I18n.t("关闭应用抽屉"); onClicked: root.close() }
        }
        GridView {
            id: appDrawerGrid
            objectName: "applicationDrawerGrid"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            cellWidth: Math.max(150 * root.uiScale,
                width / Math.max(1, Math.floor(width / (150 * root.uiScale))))
            cellHeight: 148 * root.uiScale
            model: root.model
            boundsBehavior: Flickable.StopAtBounds
            delegate: Item {
                id: drawerTile
                required property string entryId
                required property string title
                required property string description
                required property string iconName
                 required property string accentColor
                required property int route
                required property string instance
                required property bool ready
                width: appDrawerGrid.cellWidth
                height: appDrawerGrid.cellHeight
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: Theme.gap / 4
                            radius: Theme.shapeExtraLarge
                            color: Theme.surfaceContainer
                            border.color: Theme.outlineVariant
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.gap / 2
                        spacing: Theme.gap / 4
                        AppIcon {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredWidth: 56 * root.uiScale
                            Layout.preferredHeight: 56 * root.uiScale
                            iconName: drawerTile.iconName
                            accentColor: drawerTile.accentColor
                        }
                        CarText {
                            text: drawerTile.title
                            heading: true
                            font.pointSize: Theme.body * 1.05
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            maximumLineCount: 1
                            elide: Text.ElideRight
                        }
                        CarText {
                            text: drawerTile.instance
                                  ? (drawerTile.ready ? I18n.t("已连接") : I18n.t("连接中"))
                                  : drawerTile.description
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            maximumLineCount: 1
                            elide: Text.ElideRight
                            color: drawerTile.instance && !drawerTile.ready
                                   ? Theme.warning : Theme.secondary
                        }
                    }
                }
                TapHandler {
                    gesturePolicy: TapHandler.DragThreshold
                    onTapped: {
                        const entry = {entryId: drawerTile.entryId, title: drawerTile.title,
                            description: drawerTile.description, iconName: drawerTile.iconName,
                            accentColor: drawerTile.accentColor, route: drawerTile.route,
                            instance: drawerTile.instance, ready: drawerTile.ready}
                        const point = drawerTile.mapToItem(null, drawerTile.width / 2,
                                                           drawerTile.height / 2)
                        root.close()
                        Qt.callLater(function() {
                            root.entryActivated(entry, point.x, point.y)
                        })
                    }
                }
            }
        }
    }
}
