import QtQuick
import QtTest
import "../qml" as Native

TestCase {
    id: test
    name: "NativeWheel"
    width: 420; height: 360; visible: true; when: windowShown
    QtObject {
        id: view
        property bool loading: false
        property bool visible: true
        property real zoomFactor: 1
        property int calls: 0
        property bool respond: true
        property var callback: null
        function runJavaScript(script, done) {
            calls++
            if (!done) return
            if (respond) Qt.callLater(function(){done({x:1,y:1})})
            else callback = done
        }
    }
    Native.BrowserWheel { id: controller; x: 20; y: 20; targetView: view; requestTimeout: 100 }
    SignalSpy { id: spy; target: controller; signalName: "scrollIssued" }
    function init() {
        controller.stop()
        if (view.callback) { view.callback(); view.callback = null }
        view.respond = true; view.loading = false; view.visible = true
        view.zoomFactor = 1; controller.switchedOn = true
        controller.documentLoaded()
        tryVerify(function(){return !controller.pending && controller.trackingInstalled && !controller.faulted})
        controller.stop(); spy.clear(); view.calls = 0
    }
    function cleanup() { mouseRelease(controller.wheel,92,92); controller.stop() }
    function test_defaultOff() { controller.switchedOn = false; verify(!controller.visible); verify(!controller.enabled) }
    function test_deadZone() { mousePress(controller.wheel,92,92); wait(60); compare(view.calls,0) }
    function test_dragRelease() {
        mousePress(controller.wheel,92,92); mouseMove(controller.wheel,92,160,20)
        tryVerify(function(){return view.calls>0})
        verify(controller.wheel.offsetY>0); compare(controller.wheel.axisX,0)
        mouseRelease(controller.wheel,92,160); compare(controller.wheel.offsetY,0)
        const count=view.calls; wait(60); compare(view.calls,count)
    }
    function test_geometry() {
        controller.wheel.sample(115,92); const small=controller.wheel.offsetX
        controller.wheel.sample(145,92); verify(controller.wheel.offsetX>small)
        controller.wheel.sample(1000,-1000)
        verify(Math.hypot(controller.wheel.offsetX,controller.wheel.offsetY)<=1.00001)
        verify(controller.wheel.axisX===0 || controller.wheel.axisY===0)
    }
    function test_lifecycle() {
        controller.wheel.held=true; controller.wheel.sample(92,160); controller.switchedOn=false
        verify(!controller.wheel.held)
        controller.switchedOn=true; controller.wheel.held=true; view.loading=true
        verify(!controller.wheel.held)
        view.loading=false; controller.wheel.held=true; view.visible=false
        verify(!controller.wheel.held)
    }
    function test_singleFlightTimeout() {
        view.respond=false; controller.wheel.held=true; controller.wheel.sample(92,160)
        tryCompare(view,"calls",1); wait(150)
        compare(view.calls,1); verify(controller.faulted); verify(!controller.wheel.held)
    }
    function test_initializationBlocksScrolling() {
        view.respond=false
        controller.documentLoaded()
        compare(view.calls,1); verify(controller.pending)
        controller.wheel.held=true; controller.wheel.sample(92,160)
        controller.lastTick=Date.now()-40; controller.tick()
        compare(view.calls,1)
        view.callback(); view.callback=null
        verify(controller.trackingInstalled); verify(!controller.pending)
    }
    function test_timeoutRecoversAfterNewDocument() {
        view.respond=false; controller.wheel.held=true; controller.wheel.sample(92,160)
        controller.lastTick=Date.now()-40; controller.tick()
        tryCompare(view,"calls",1); wait(150)
        verify(controller.faulted)
        view.respond=true; view.loading=true
        view.loading=false
        tryVerify(function(){return controller.trackingInstalled && !controller.faulted && !controller.pending},1000)
    }
    function test_lateCallbackCannotClearNewRequest() {
        view.respond=false
        controller.wheel.held=true; controller.wheel.sample(92,160)
        controller.lastTick=Date.now()-40; controller.tick()
        const stale=view.callback
        wait(150); verify(controller.faulted)
        view.loading=true; view.loading=false
        verify(controller.pending)
        const current=view.callback
        stale()
        verify(controller.pending); verify(!controller.trackingInstalled)
        current(); view.callback=null
        verify(!controller.pending); verify(controller.trackingInstalled); verify(!controller.faulted)
    }
    function test_zoom() {
        controller.wheel.held=true; controller.wheel.sample(160,92)
        controller.lastTick=Date.now()-40; controller.tick()
        const normal=spy.signalArguments[0][0]
        tryVerify(function(){return !controller.pending})
        spy.clear(); view.zoomFactor=2
        controller.lastTick=Date.now()-40; controller.tick()
        verify(spy.signalArguments[0][0]<normal*0.7)
    }
    function test_targetSwitchStops() {
        controller.wheel.held=true; controller.wheel.sample(92,160)
        controller.targetView=null
        verify(!controller.wheel.held); compare(controller.wheel.offsetY,0)
        controller.targetView=view
    }
    function test_keyboardRelease() {
        controller.wheel.forceActiveFocus()
        keyPress(Qt.Key_Down)
        verify(controller.wheel.held); compare(controller.wheel.axisY,1)
        keyRelease(Qt.Key_Down)
        verify(!controller.wheel.held); compare(controller.wheel.offsetY,0)
    }
}
