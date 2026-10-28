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
    // 卡片头部右上角的操作位（时间页的“报时”）。为空时不占位，其它页面头高不变。
    property Component headerAction: null
    // 卡片底部固定操作条（计时器的开始/重置）。放在正文 Flickable 之外，
    // 正文再长也不会被裁掉，也不会被滚动的手势吞掉点击。
    property Component footerAction: null
    readonly property real footerHeight: footerLoader.item ? footerLoader.item.implicitHeight : 0
    readonly property real headerActionWidth: headerActionLoader.active && headerActionLoader.item
        ? headerActionLoader.item.implicitWidth + Theme.gap / 2 : 0
    default property alias content: body.data

    radius: Theme.shapeLarge
    color: Theme.surface
    border.color: Theme.outlineVariant

    // 用标题字体推一个确定的高度，而不是取 RowLayout.implicitHeight：后者经
    // 「副标题显隐 → 行内容 → 行高」会回到 headerHeight 上，日志里能直接看到
    // 「Binding loop detected for property "headerHeight"」。
    readonly property real headerHeight: Math.max(48 * Theme.scaleFactor,
                                                  Math.round(1.5 * Theme.title),
                                                  headerActionLoader.item
                                                      ? headerActionLoader.item.implicitHeight : 0)
    // 副标题的测量探针：与副标题同字体、不参与布局。
    CarText {
        id: subtitleProbe
        visible: false
        text: card.subtitle
        maximumLineCount: 1
    }

    // 正文占用的纵向预算：上边距 + 标题 + 半格间距在上，一格间距在下。隐式高度与
    // 内部 Flickable 用同一套账，否则单栏“按内容高”的卡片自己就会溢出半格。
    readonly property real bodyTop: headerHeight + Theme.gap * 1.5
    readonly property real bodyBottomInset: Theme.gap
    implicitHeight: bodyTop + body.implicitHeight + footerHeight + bodyBottomInset
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
            // 标题一行：主标题在左、小字（副标题）在右，不再各占一行——省下来的高度
            // 全给正文。副标题放不下就整条不显示（宁可删掉也不截断成没意义的半句）。
            RowLayout {
                id: headingRow
                x: 48 * Theme.scaleFactor + Theme.gap
                width: Math.max(1, parent.width - 48 * Theme.scaleFactor - Theme.gap)
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.gap / 2
                CarText {
                    id: titleText
                    text: card.title
                    heading: true
                    Layout.fillWidth: true
                    maximumLineCount: 1
                    elide: Text.ElideRight
                }
                CarText {
                    id: subtitleText
                    text: card.subtitle
                    subdued: true
                    maximumLineCount: 1
                    elide: Text.ElideRight
                    // 标题优先：主标题 + 副标题 + 间距放不下时，副标题整条不显示，
                    // 而不是把主标题挤成「中国…」。宽度用隐藏探针量，避免在布局过程中
                    // 读正在被布局的副标题自身宽度；右侧操作位占的宽度要先扣掉。
                    Layout.maximumWidth: Math.max(0, card.width * 0.55 - card.headerActionWidth)
                    visible: card.subtitle !== ""
                             && titleText.implicitWidth + subtitleProbe.implicitWidth
                                + headingRow.spacing + card.headerActionWidth <= headingRow.width
                }
                Loader {
                    id: headerActionLoader
                    Layout.alignment: Qt.AlignVCenter
                    active: card.headerAction !== null
                    sourceComponent: card.headerAction
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
                ? Math.max(0, card.height - card.bodyTop - card.footerHeight - card.bodyBottomInset)
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

    Loader {
        id: footerLoader
        objectName: card.objectName + "Footer"
        active: card.footerAction !== null
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.gap
        anchors.rightMargin: Theme.gap
        anchors.bottomMargin: Theme.gap
        height: active && item ? item.implicitHeight : 0
        sourceComponent: card.footerAction
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
