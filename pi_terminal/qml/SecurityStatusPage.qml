import QtQuick
import QtQuick.Layouts

// 安全状态页（D 侧只读接点）。
//
// 定位：**不是 T-09 的登录/绑卡页面**。T-09 主责是 Mr.Z，文件范围含
// Main.qml / PageProvider / 登录锁屏绑卡组件；本文件只负责把 D 侧的
// 四块只读数据渲染出来，供 Z 参考或直接复用其中一段。
//
// 刻意不 import Theme / GlassSurface：本页要保持"零依赖可单独预览"，
// 这样 preview_security 不必链接 terminal_ui（也就不必依赖 VLC SDK）。
// 视觉上会比正式页面朴素，这是有意的取舍。
Item {
    id: page
    property color background: "#101216"
    property color cardColor: "#191c22"
    property color textColor: "#e8eaed"
    property color mutedColor: "#9aa0a6"
    property color warnColor: "#f9ab00"
    property color badColor: "#ea4335"
    property color okColor: "#34a853"

    Rectangle { anchors.fill: parent; color: page.background }

    Flickable {
        anchors.fill: parent
        anchors.margins: 20
        contentHeight: content.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: content
            width: parent.width
            spacing: 16

            Text {
                text: qsTr("安全状态")
                color: page.textColor
                font.pixelSize: 26
                font.bold: true
            }

            // ── 1. 桥板信任记录 ─────────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: trustColumn.implicitHeight + 28
                radius: 12
                color: page.cardColor

                ColumnLayout {
                    id: trustColumn
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: qsTr("已配对桥板")
                            color: page.textColor
                            font.pixelSize: 17
                            font.bold: true
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: securityStatus.trustStoreHealthy
                                  ? qsTr("%1 条").arg(securityStatus.trustRecords.length)
                                  : qsTr("信任库损坏")
                            color: securityStatus.trustStoreHealthy ? page.mutedColor : page.badColor
                            font.pixelSize: 14
                        }
                    }

                    Text {
                        visible: !securityStatus.trustStoreHealthy
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTr("信任库无法解析，所有桥板都会被拒绝。这不是「还没有配对」——请检查文件或重新配对。")
                        color: page.badColor
                        font.pixelSize: 13
                    }

                    Text {
                        visible: securityStatus.trustStoreHealthy
                                 && securityStatus.trustRecords.length === 0
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTr("还没有配对过的桥板。")
                        color: page.mutedColor
                        font.pixelSize: 13
                    }

                    Repeater {
                        model: securityStatus.trustRecords
                        delegate: ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: modelData.bridgeId + (modelData.active ? "" : qsTr("（已撤销）"))
                                color: modelData.active ? page.textColor : page.mutedColor
                                font.pixelSize: 15
                            }
                            Text {
                                Layout.fillWidth: true
                                text: qsTr("指纹 %1…  key_id %2")
                                      .arg(modelData.fingerprintHex.substring(0, 16))
                                      .arg(modelData.keyIdText)
                                color: page.mutedColor
                                font.pixelSize: 12
                                font.family: "monospace"
                                elide: Text.ElideRight
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: securityStatus.trustStorePath
                        color: page.mutedColor
                        font.pixelSize: 11
                        font.family: "monospace"
                        elide: Text.ElideMiddle
                    }
                }
            }

            // ── 2. 本机身份 ─────────────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: identityColumn.implicitHeight + 28
                radius: 12
                color: page.cardColor

                ColumnLayout {
                    id: identityColumn
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 6

                    Text {
                        text: qsTr("本机身份（ECDSA P-256）")
                        color: page.textColor
                        font.pixelSize: 17
                        font.bold: true
                    }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: {
                            switch (securityStatus.identityState) {
                            case "ready":
                                return qsTr("指纹 %1…  key_id %2")
                                        .arg(securityStatus.identityFingerprint.substring(0, 16))
                                        .arg(securityStatus.identityKeyId)
                            case "missing":
                                return qsTr("尚未生成。首次配对前需要生成，生成后所有桥板都要重新配对。")
                            case "corrupt":
                                return qsTr("身份文件损坏，无法签名。这不是「未生成」——请检查文件。")
                            default:
                                return qsTr("本次构建未启用 mbedtls，身份功能不可用。")
                            }
                        }
                        color: {
                            switch (securityStatus.identityState) {
                            case "ready": return page.textColor
                            case "missing": return page.mutedColor
                            case "corrupt": return page.badColor
                            default: return page.warnColor
                            }
                        }
                        font.pixelSize: 14
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: securityStatus.identityPath.length > 0
                        text: securityStatus.identityPath
                        color: page.mutedColor
                        font.pixelSize: 11
                        font.family: "monospace"
                        elide: Text.ElideMiddle
                    }
                }
            }

            // ── 3. 物理槽位（快照）──────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: slotColumn.implicitHeight + 28
                radius: 12
                color: page.cardColor

                ColumnLayout {
                    id: slotColumn
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 6

                    Text {
                        text: qsTr("物理槽位")
                        color: page.textColor
                        font.pixelSize: 17
                        font.bold: true
                    }

                    Repeater {
                        model: securityStatus.slotSnapshot()
                        delegate: Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: modelData.bound
                                  ? qsTr("槽 %1：%2 · 设备 %3 · 代次 %4…")
                                        .arg(modelData.slot)
                                        .arg(modelData.bridgeId)
                                        .arg(modelData.deviceId)
                                        .arg(modelData.generation.substring(0, 8))
                                  : qsTr("槽 %1：未绑定").arg(modelData.slot)
                            color: modelData.bound ? page.textColor : page.mutedColor
                            font.pixelSize: 14
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTr("槽位只表示位置，不表示身份；拔出后业务会话可继续。")
                        color: page.mutedColor
                        font.pixelSize: 11
                    }

                    // 判定计数：刷卡没反应时，先看是不是被当成迟到/串槽丢了。
                    Text {
                        id: statsText
                        property var stats: securityStatus.slotStats()
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: stats.rejected > 0
                              ? qsTr("槽位判定：接受 %1 次，拒绝 %2 次（最近原因 %3）")
                                    .arg(stats.accepted).arg(stats.rejected).arg(stats.lastRejection)
                              : qsTr("槽位判定：接受 %1 次，无拒绝").arg(stats.accepted)
                        color: stats.rejected > 0 ? page.warnColor : page.mutedColor
                        font.pixelSize: 12
                    }
                }
            }

            // ── 4. UID 索引密钥 ─────────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: uidColumn.implicitHeight + 28
                radius: 12
                color: page.cardColor

                ColumnLayout {
                    id: uidColumn
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 6

                    Text {
                        text: qsTr("UID 索引密钥")
                        color: page.textColor
                        font.pixelSize: 17
                        font.bold: true
                    }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: securityStatus.uidKeyReady
                              ? qsTr("已就绪：刷卡可派生索引并匹配用户。")
                              : qsTr("未就绪：所有刷卡都不会命中用户。这不是「没有这个用户」——说明密钥还没下发。")
                        color: securityStatus.uidKeyReady ? page.okColor : page.warnColor
                        font.pixelSize: 14
                    }
                }
            }
        }
    }
}
