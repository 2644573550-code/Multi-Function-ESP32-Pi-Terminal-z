import QtQuick
import QtTest
import QtWebEngine
import "../qml/ContextScroll.js" as Scroll
import "../qml" as Native

TestCase {
    id: test
    name: "RealWebEngineContext"
    width: 800; height: 600; visible: true; when: windowShown
    property bool ready: false
    property bool done: false
    property var result
    WebEngineView {
        id: web; anchors.fill: parent
        url: Qt.resolvedUrl("page.html")
        onLoadingChanged: function(request) {
            if (request.status === WebEngineView.LoadSucceededStatus) test.ready = true
        }
    }
    Native.BrowserWheel { id: wheel; x: 600; y: 380; targetView: web; switchedOn: true }
    function evaluate(script) {
        done = false
        web.runJavaScript(script, function(value) { test.result = value; test.done = true })
        tryVerify(function() { return test.done }, 5000)
        return result
    }
    function initTestCase() { tryVerify(function(){return test.ready},15000); evaluate(Scroll.installScript()) }
    function test_contextAndBoundaries() {
        evaluate("document.getElementById('content').dispatchEvent(new PointerEvent('pointerdown',{bubbles:true,composed:true}))")
        evaluate(Scroll.moveScript(60,100))
        compare(evaluate("document.getElementById('inner').scrollTop"),100)
        compare(evaluate("document.getElementById('inner').scrollLeft"),60)
        compare(evaluate("window.scrollY"),0)
        evaluate("document.getElementById('inner').scrollTop=99999")
        evaluate(Scroll.moveScript(0,80))
        verify(evaluate("window.scrollY")>0)
        evaluate("window.scrollTo(0,0);document.getElementById('inner').style.overscrollBehavior='contain'")
        evaluate(Scroll.moveScript(0,80)); compare(evaluate("window.scrollY"),0)
        evaluate("document.getElementById('text').focus()")
        evaluate(Scroll.moveScript(0,30))
        verify(evaluate("document.getElementById('text').scrollTop")>0)
        evaluate("document.querySelector('#shadow-host').shadowRoot.querySelector('#shadow-content').dispatchEvent(new PointerEvent('pointerdown',{bubbles:true,composed:true}))")
        evaluate(Scroll.moveScript(0,30))
        verify(evaluate("document.querySelector('#shadow-host').shadowRoot.querySelector('#shadow-scroll').scrollTop")>0)
    }
    function test_nativeDragScrollsRealPage() {
        tryVerify(function(){return wheel.trackingInstalled && !wheel.pending && !wheel.faulted},3000)
        evaluate("document.activeElement.blur();window.scrollTo(0,0);var box=document.getElementById('inner');box.style.overscrollBehavior='auto';box.scrollTop=0;document.getElementById('content').dispatchEvent(new PointerEvent('pointerdown',{bubbles:true,composed:true}))")
        mousePress(wheel.wheel,92,92)
        mouseMove(wheel.wheel,92,160,30)
        wait(160)
        mouseRelease(wheel.wheel,92,160)
        tryVerify(function(){return !wheel.pending},3000)
        verify(evaluate("document.getElementById('inner').scrollTop")>0)
        const position=evaluate("document.getElementById('inner').scrollTop")
        wait(80)
        compare(evaluate("document.getElementById('inner').scrollTop"),position)
        compare(wheel.wheel.offsetY,0)
    }
}
