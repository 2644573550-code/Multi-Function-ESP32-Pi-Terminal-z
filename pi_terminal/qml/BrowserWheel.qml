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
    readonly property bool preparing: switchedOn && !!targetView && !faulted &&
        (targetView.loading || pending || !trackingInstalled)
    property alias wheel: wheel
    signal scrollIssued(real dx, real dy)
    implicitWidth: 184; implicitHeight: 184
    visible: switchedOn
    enabled: switchedOn && !!targetView && !targetView.loading && targetView.visible && !faulted

    // Keep one request in flight across tab/document changes; late callbacks
    // must not resume scrolling in a different page.
    function stop() { wheel.reset() }
    function invalidateDocument() {
        documentToken++
        trackingInstalled = false
    }
    function finishRequest(token, requestDocument, timedOut) {
        if (token !== requestToken) return
        pending = false
        watchdog.stop()
        if (timedOut) requestToken++
        if (requestDocument !== documentToken) { install(); return }
        if (timedOut) { faulted = true; stop() }
    }
    function install() {
        if (!switchedOn || !targetView || targetView.loading || trackingInstalled || pending) return
        pending = true
        const request = ++requestToken
        const document = documentToken
        pendingDocument = document
        watchdog.restart()
        try {
            targetView.runJavaScript(PageScroll.installScript(), function() {
                if (request !== root.requestToken) return
                root.pending = false
                watchdog.stop()
                if (document === root.documentToken) {
                    root.trackingInstalled = true
                    root.faulted = false
                } else root.install()
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
    function installAfterCurrentDocument() {
        const target = targetView
        Qt.callLater(function() {
            if (target === root.targetView && root.switchedOn && target && !target.loading)
                root.documentLoaded()
        })
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
    onTargetViewChanged: { stop(); invalidateDocument(); faulted = false; installAfterCurrentDocument() }
    onSwitchedOnChanged: {
        stop()
        if (switchedOn) install()
    }
    onVisibleChanged: if (!visible) stop()
    onEnabledChanged: if (!enabled) stop()
    Component.onCompleted: install()
    Connections {
        target: root.targetView
        ignoreUnknownSignals: true
        function onLoadingChanged(request) {
            root.stop()
            if (!request) return
            if (request.status === WebEngineView.LoadStartedStatus) {
                root.invalidateDocument()
                root.faulted = false
            } else if (request.status === WebEngineView.LoadSucceededStatus) {
                root.installAfterCurrentDocument()
            } else if (request.status === WebEngineView.LoadFailedStatus) {
                root.invalidateDocument()
                root.faulted = false
            }
        }
        function onUrlChanged() {
            // URL changes precede WebEngine's load cycle. Invalidate now but
            // only inject into the replacement document after load succeeds.
            root.stop()
            root.invalidateDocument()
        }
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
