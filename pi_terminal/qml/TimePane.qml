import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 时间页的双栏卡片。
//
// 视觉与 SettingsGroup 一致（圆角、图标圈、标题/副标题），但正文放在 Flickable 里：
// 宽屏双栏时卡片撑满页面高度、内容在卡片内部滚动；窄屏单栏时卡片按内容高度。
// 刻意不改 SettingsGroup，避免影响设置页等既有页面。
Rectangle {
    id: card

    property string iconName: "settings"
    property string title: ""
    property string subtitle: ""
    property color accentColor: Theme.accent
    // true：撑满容器高度，正文内部滚动；false：按内容高度。
    property bool scrollBody: false
    default property alias content: body.data

    radius: Theme.shapeLarge
    color: Theme.surface
    border.color: Theme.outlineVariant

    readonly property real headerHeight: Math.max(48 * Theme.scaleFactor, headingColumn.implicitHeight)
    // 正文占用的纵向预算：上边距 + 标题 + 半格间距在上，一格间距在下。隐式高度与
    // 内部 Flickable 用同一套账，否则单栏“按内容高”的卡片自己就会溢出半格。
    readonly property real bodyTop: headerHeight + Theme.gap * 1.5
    readonly property real bodyBottomInset: Theme.gap
    implicitHeight: bodyTop + body.implicitHeight + bodyBottomInset
    // 单栏：卡片按内容高度，布局不得把它压小（压小会裁掉正文）。
    // 双栏：卡片必须能被压到栅格行高，正文改由内部 Flickable 滚动；
    // 这里若继续按内容高给下限，Layout.fillHeight 就会被下限顶回去，
    // 卡片长过视口、页面又被 clip，末行内容直接消失（曾卡住的 bug）。
    Layout.minimumHeight: scrollBody ? 0 : implicitHeight

    Column {
        id: header
        x: Theme.gap
        y: Theme.gap
        width: Math.max(1, card.width - Theme.gap * 2)
        spacing: 0
        Item {
            width: parent.width
            height: card.headerHeight
            Rectangle {
                width: 48 * Theme.scaleFactor
                height: width
                anchors.verticalCenter: parent.verticalCenter
                radius: width / 2
                color: Qt.darker(card.accentColor, 2.1)
                border.color: Qt.lighter(card.accentColor, 1.15)
                MaterialIcon {
                    anchors.centerIn: parent
                    width: 25 * Theme.scaleFactor
                    height: width
                    name: card.iconName
                    light: true
                }
            }
            Column {
                id: headingColumn
                x: 48 * Theme.scaleFactor + Theme.gap
                width: Math.max(1, parent.width - 48 * Theme.scaleFactor - Theme.gap)
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2 * Theme.scaleFactor
                CarText {
                    text: card.title
                    heading: true
                    width: parent.width
                    maximumLineCount: 1
                    elide: Text.ElideRight
                }
                CarText {
                    visible: card.subtitle !== ""
                    text: card.subtitle
                    subdued: true
                    width: parent.width
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    Flickable {
        id: scroll
        objectName: card.objectName + "Body"
        x: Theme.gap
        y: card.bodyTop
        width: Math.max(1, card.width - Theme.gap * 2)
        height: card.scrollBody
                ? Math.max(0, card.height - card.bodyTop - card.bodyBottomInset)
                : body.implicitHeight
        contentWidth: width
        contentHeight: body.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        ColumnLayout {
            id: body
            width: scroll.width
            spacing: Theme.gap / 3
        }
    }

    // 双栏下正文在卡片内部滚动，但控件样式的 ScrollBar 静止时会淡出，屏上没有任何
    // “还有内容”的线索（世界时钟末行半截最像 bug）。这里自绘一条常驻细指示条 +
    // 底部渐隐，不依赖样式的淡出策略；单栏时两者都不出现。
    // 只对“真溢出”提示：缩放后 gap/字号取整会留下几像素的假溢出，那点余量被底部
    // 内边距吃掉，却会画出一条几乎没有行程的滚动条，看着像渲染错误。
    readonly property real bodyOverflow: Math.max(0, scroll.contentHeight - scroll.height)
    readonly property bool bodyScrollable: card.scrollBody && bodyOverflow > 4 * Theme.scaleFactor
    readonly property real bodyMaxScroll: bodyOverflow

    Rectangle {
        id: bodyFade
        objectName: card.objectName + "ScrollFade"
        visible: card.bodyScrollable && scroll.contentY < card.bodyMaxScroll - 1
        x: scroll.x
        width: scroll.width
        height: Theme.gap * 2
        y: scroll.y + Math.max(0, scroll.height - height)
        gradient: Gradient {
            GradientStop { position: 0; color: "transparent" }
            GradientStop { position: 1; color: card.color }
        }
    }

    Rectangle {
        id: bodyScrollRail
        objectName: card.objectName + "ScrollRail"
        visible: card.bodyScrollable
        width: 4 * Theme.scaleFactor
        height: Math.max(28 * Theme.scaleFactor,
                         scroll.height * Math.min(1, scroll.height / Math.max(1, scroll.contentHeight)))
        x: card.width - width - Theme.gap / 2
        y: scroll.y + Math.max(0, (scroll.height - height) *
                               (scroll.contentY / Math.max(1, card.bodyMaxScroll)))
        radius: width / 2
        color: Theme.outline
        opacity: scroll.moving || scroll.flicking ? 0.95 : 0.6
        Behavior on opacity { NumberAnimation { duration: 120 } }
    }
}
