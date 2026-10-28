import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts

// 登入门禁 + 锁屏遮罩 + 无交互自动锁（5 分钟）。
//
// 挂在 Overlay.overlay 最上层，z 高于启动动画。
// 未登录时挡住整个桌面；锁屏时挡住敏感内容但不停已授权任务。
//
// 所有判断取自 sessionManager（服务端状态），不看本地变量。
Item {
    id: gate
    anchors.fill: parent
    property var inputSource: null
    // --ui-test 无人值守审计没有登录步骤，门禁会挡住被审计的桌面。
    property bool enabledGate: typeof sessionGateBypassed !== "undefined" ? !sessionGateBypassed : true

    // 未登录 → 显示登录页；已登录但锁屏 → 显示锁屏。
    readonly property bool needLogin: gate.enabledGate && !sessionManager.loggedIn
    readonly property bool needUnlock: gate.enabledGate && sessionManager.loggedIn && sessionManager.locked
    readonly property bool blocking: needLogin || needUnlock

    visible: blocking
    // 挡住下层所有输入：未登录时桌面不可点。
    MouseArea {
        anchors.fill: parent
        enabled: gate.blocking
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        onWheel: function(wheel) { wheel.accepted = true }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.background
        opacity: 0.97
    }

    // ---- 无交互自动锁（5 分钟）----
    // 任何输入都重置。只在已登录且未锁屏时计时。
    property int idleSeconds: 300
    property int idleElapsed: 0

    Timer {
        id: idleTicker
        interval: 1000
        repeat: true
        running: gate.enabledGate && sessionManager.loggedIn && !sessionManager.locked
        onTriggered: {
            gate.idleElapsed += 1
            if (gate.idleElapsed >= gate.idleSeconds) {
                gate.idleElapsed = 0
                sessionManager.lockNow()
            }
        }
    }

    Connections {
        target: gate.inputSource
        function onActiveFocusItemChanged() { gate.idleElapsed = 0 }
    }

    // 窗口级输入事件都会经过这里；用 TapHandler 覆盖全窗口不现实，
    // 因此改为监听根窗口的 signal（由 Main.qml 转发 clicked/keyed）。
    function noteInput() { gate.idleElapsed = 0 }

    // 有未完成任务时切用户会被服务端拒绝（login 返回 BUSY），
    // 这里给出可操作的出口，而不是让用户卡住。
    Loader {
        anchors.centerIn: gate
        width: Math.min(gate.width - Theme.margin * 2, 520 * Theme.scaleFactor)
        active: gate.needLogin && sessionManager.message === "BUSY"
        visible: active
        sourceComponent: TaskFinishDialog {
            tasks: gate.pendingTasks()
            message: gate.localizedMessage(sessionManager.message)
            onFinishTask: function(taskId) {
                sessionManager.finishTask(taskId)
                sessionManager.refreshUsers()
            }
            onStopAll: function() {
                gate.stopAllTasks()
                sessionManager.refreshUsers()
            }
        }
    }

    function pendingTasks() {
        // SessionManager 不把 TaskCapability* 暴露给 QML；这里只给出可读标签。
        const list = []
        for (const task of sessionManager.pendingTaskSummaries())
            list.push({taskId: task.taskId, label: I18n.t("任务") + " " + task.deviceId})
        return list
    }

    function stopAllTasks() {
        for (const task of sessionManager.pendingTaskSummaries())
            sessionManager.finishTask(task.taskId)
    }

    // 报告 §5.1：刷卡只跳到该用户的 PIN 页，**不直接登录**。
    Connections {
        target: securityService
        function onCardUserSelected(userId) {
            if (!gate.needLogin) return
            gate.preSelectedUserId = userId
            // 让登录页直接进入 PIN 输入状态（免去手动选人）。
            for (const user of sessionManager.users)
                if (user.userId === userId) gate.preSelectedName = user.displayName
        }
    }
    property string preSelectedUserId: ""
    property string preSelectedName: ""

    // ---- 登录 / 锁屏内容 ----
    Loader {
        anchors.centerIn: gate
        width: Math.min(gate.width - Theme.margin * 2, 520 * Theme.scaleFactor)
        active: gate.blocking
        visible: active
        sourceComponent: gate.needLogin ? loginComponent : lockComponent
    }

    Component {
        id: loginComponent
        LoginPage {
            users: sessionManager.users
            // 刷卡选中的用户优先：仍须输入 PIN 才登录。
            preSelectedUserId: gate.preSelectedUserId
            preSelectedDisplayName: gate.preSelectedName
            message: gate.localizedMessage(sessionManager.message)
            messageIsError: sessionManager.message.length > 0
            retryAfterMs: sessionManager.retryAfterMs
            busy: gate.verifying
            onPinSubmitted: function(userId, pin) {
                // KDF 在服务端同步执行（Pi 上数百毫秒），期间给可见反馈。
                gate.verifying = true
                const code = sessionManager.login(userId, pin)
                gate.verifying = false
                if (code === "") gate.idleElapsed = 0
            }
            onRefreshRequested: sessionManager.refreshUsers()
        }
    }

    Component {
        id: lockComponent
        LockScreen {
            message: gate.localizedMessage(sessionManager.message)
            busy: gate.verifying
            onPinSubmitted: function(pin) {
                gate.verifying = true
                const code = sessionManager.unlockWithPin(pin)
                gate.verifying = false
                if (code === "") gate.idleElapsed = 0
            }
            onLogoutRequested: sessionManager.signOut()
        }
    }

    // 错误码 → 本地化提示。不回显账号是否存在。
    function localizedMessage(code) {
        if (!code || code.length === 0) return ""
        switch (code) {
        case "AUTH_FAILED":       return I18n.t("PIN 不正确")
        case "RATE_LIMITED":      return I18n.t("请稍候再试")
        case "NOT_AUTHORIZED":    return I18n.t("没有权限")
        case "SESSION_EXPIRED":   return I18n.t("会话已失效，请重新登录")
        case "STORAGE_UNAVAILABLE": return I18n.t("数据存储不可用")
        case "BUSY":              return I18n.t("请先结束正在进行的任务")
        default:                  return I18n.t("操作失败")
        }
    }

    // 没有用户时给出明确指引，不让用户卡在空列表里。
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.margin
        anchors.horizontalCenter: parent.horizontalCenter
        width: hint.implicitWidth + Theme.gap
        height: hint.implicitHeight + Theme.gap / 2
        radius: Theme.shapeSmall
        color: Theme.surfaceContainer
        visible: gate.needLogin && !sessionManager.hasUsers
        CarText {
            id: hint
            anchors.centerIn: parent
            text: I18n.t("没有用户，请先在终端执行 --init-admin 创建管理员")
            color: Theme.warning
            fontPixelSize: Theme.fontLabel
        }
    }
}
