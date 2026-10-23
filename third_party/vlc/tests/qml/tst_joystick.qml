import QtQuick
import QtTest
import "../../../../pi_terminal/qml" as Terminal

TestCase {
    id: test
    name: "BrowserJoystick"
    when: windowShown
    visible: true
    width: 400; height: 300
    QtObject {
        id: fakeView
        property real zoomFactor: 1
        property int calls: 0
        property bool respond: true
        function runJavaScript(script, callback) { calls++; if (respond) callback({x: 1, y: 1}) }
    }
    Terminal.BrowserJoystick { id: stick; x: 20; y: 20; targetView: fakeView }
    SignalSpy { id: issued; target: stick; signalName: "scrollIssued" }
    function init() {
        stick.visible = true; stick.enabled = true; stick.targetView = fakeView
        stick.reset(); fakeView.calls = 0; fakeView.respond = true; issued.clear()
    }
    function cleanup() { mouseRelease(stick, stick.width/2, stick.height/2); stick.reset() }
    function test_deadzone() {
        mousePress(stick, stick.width/2, stick.height/2)
        wait(80); compare(fakeView.calls, 0)
    }
    function test_drag_release() {
        mousePress(stick, stick.width/2, stick.height/2)
        mouseMove(stick, stick.width/2, stick.height - 5, 20)
        tryVerify(function() { return fakeView.calls > 0 })
        verify(issued.signalArguments[0][1] > 0)
        mouseRelease(stick, stick.width/2, stick.height - 5)
        compare(stick.axisX, 0); compare(stick.axisY, 0); verify(!stick.held)
        const count = fakeView.calls; wait(80); compare(fakeView.calls, count)
    }
    function test_hidden_and_disabled_stop() {
        stick.held = true; stick.updateAxes(100, 100); stick.visible = false
        verify(!stick.held); compare(stick.axisY, 0)
        stick.visible = true; stick.held = true; stick.updateAxes(100, 100); stick.enabled = false
        verify(!stick.held)
    }
    function test_target_change_stops() {
        stick.held = true; stick.updateAxes(100, 100); stick.targetView = null
        verify(!stick.held); compare(stick.axisX, 0)
    }
    function test_bounded_pending() {
        fakeView.respond = false; stick.held = true; stick.updateAxes(100, 100)
        tryCompare(fakeView, "calls", 1); wait(100); compare(fakeView.calls, 1)
        stick.reset(); wait(40); compare(fakeView.calls, 1)
    }
    function test_axes_and_zoom() {
        stick.updateAxes(1000, -1000)
        verify(Math.abs(Math.hypot(stick.axisX, stick.axisY) - 1) < 0.001)
        verify(stick.axisX > 0 && stick.axisY < 0)
        stick.held = true; stick.lastTick = Date.now() - 40; stick.tick()
        const normal = issued.signalArguments[0][0]
        issued.clear(); fakeView.zoomFactor = 2
        stick.lastTick = Date.now() - 40; stick.tick()
        verify(issued.signalArguments[0][0] < normal * 0.7)
        fakeView.zoomFactor = 1
    }
}
