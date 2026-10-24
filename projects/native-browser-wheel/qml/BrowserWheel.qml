import QtQuick
import QtQuick.Window
import QtWebEngine
import "ContextScroll.js" as PageScroll
import "WheelStrings.js" as Strings

Item {
    id: root
    property var targetView: null
    property bool switchedOn: false
    property real maximumSpeed: 720
    property string language: "zh"
    property var translate: function(text) { return Strings.t(text, root.language) }
    property int requestTimeout: 1000
    property bool pending: false
    property bool faulted: false
    property int requestToken: 0
    property int documentToken: 0
    property int pendingDocument: 0
    property bool trackingInstalled: false
    property double lastTick: 0
    property alias wheel: wheel
    signal scrollIssued(real dx, real dy)
    implicitWidth: 184; implicitHeight: 184
    visible: switchedOn
    enabled: switchedOn && !!targetView && !targetView.loading && targetView.visible && !faulted

    // Do not clear pending on reset: an old request must finish or time out
    // before another can be issued, even across target switches.
    function stop() { wheel.reset() }
    function invalidateDocument() {
        documentToken++
        trackingInstalled = false
    }
    function finishRequest(token, requestDocument, timedOut) {
        if (token !== requestToken) return
        pending = false
        watchdog.stop()
        if (timedOut) requestToken++ // A late callback must not resurrect this request.
        if (requestDocument !== documentToken) { install(); return }
        if (timedOut) { faulted = true; stop() }
    }
    function install() {
        if (!targetView || targetView.loading || trackingInstalled || pending) return
        pending = true
        const request = ++requestToken
        const document = documentToken
        pendingDocument = document
        watchdog.restart()
        try {
            targetView.runJavaScript(PageScroll.installScript(), function() {
                if (request !== requestToken) return
                root.pending = false
                watchdog.stop()
                if (document === root.documentToken) {
                    root.trackingInstalled = true
                    root.faulted = false
                }
                else root.install()
            })
        } catch (error) {
            finishRequest(request, document, true)
        }
    }
    function documentLoaded() {
        if (!targetView || targetView.loading) return
        stop()
        invalidateDocument()
        faulted = false
        install()
    }
    function tick() {
        const now = Date.now()
        const dt = Math.min(50, Math.max(0, now-lastTick))/1000
        lastTick = now
        if (pending || !trackingInstalled || !enabled || !visible || !wheel.moving) return
        const zoom = Math.max(0.1, targetView.zoomFactor || 1)
        const dx = wheel.axisX * wheel.strength * maximumSpeed * dt / zoom
        const dy = wheel.axisY * wheel.strength * maximumSpeed * dt / zoom
        pending = true
        const request = ++requestToken
        const document = documentToken
        pendingDocument = document
        watchdog.restart()
        try {
            targetView.runJavaScript(PageScroll.moveScript(dx, dy), function() {
                root.finishRequest(request, document, false)
            })
            scrollIssued(dx, dy)
        } catch (error) {
            finishRequest(request, document, true)
        }
    }
    onTargetViewChanged: { stop(); invalidateDocument(); install() }
    onSwitchedOnChanged: stop()
    onVisibleChanged: if (!visible) stop()
    onEnabledChanged: if (!enabled) stop()
    Component.onCompleted: install()
    Connections {
        target: root.targetView
        ignoreUnknownSignals: true
        function onLoadingChanged(request) {
            root.stop()
            if (!root.targetView.loading && (!request || request.status === WebEngineView.LoadSucceededStatus))
                root.documentLoaded()
        }
        function onUrlChanged() { root.stop(); root.invalidateDocument(); root.install() }
        function onVisibleChanged() { if (!root.targetView.visible) root.stop() }
    }
    Connections {
        target: root.Window.window
        function onActiveChanged() { if (!root.Window.window.active) root.stop() }
    }
    Timer {
        interval: 16; repeat: true
        running: root.enabled && root.visible && wheel.moving
        onRunningChanged: if (running) root.lastTick = Date.now()
        onTriggered: root.tick()
    }
    Timer {
        id: watchdog; interval: root.requestTimeout
        onTriggered: root.finishRequest(root.requestToken, root.pendingDocument, true)
    }
    DirectionWheel { id: wheel; anchors.fill: parent; language: root.language; translate: root.translate }
}
