// Kept independent of the UI so the exact page script is integration-tested.
function installContextTracking() {
    function install(doc) {
        if (!doc || doc.__terminalJoystickTracking) return;
        doc.__terminalJoystickTracking = true;
        function remember(event) {
            var path = event.composedPath ? event.composedPath() : [];
            doc.__terminalJoystickTarget = path.length ? path[0] : event.target;
            doc.__terminalJoystickFocusAtPointer = doc.activeElement;
        }
        doc.addEventListener("pointermove", remember, {passive: true});
        doc.addEventListener("pointerdown", remember, {passive: true});
        doc.addEventListener("focusin", remember, {passive: true});
        function frames() {
            Array.prototype.forEach.call(doc.querySelectorAll("iframe,frame"), function(frame) {
                try { install(frame.contentDocument); } catch (_) { /* Cross-origin boundary. */ }
            });
        }
        doc.addEventListener("load", frames, true);
        frames();
    }
    install(document);
}

function scrollContext(dx, dy) {
    function target(doc) {
        var element = doc.__terminalJoystickTarget;
        var active = doc.activeElement;
        // Focus can change without focusin delivery while the WebEngine view
        // is inactive (for example after dragging the QML joystick).
        if (!element || !element.isConnected ||
            (active && active !== doc.body && active !== doc.documentElement &&
             active !== doc.__terminalJoystickFocusAtPointer)) element = active;
        if (!element || element === doc.body || element === doc.documentElement)
            element = doc.elementFromPoint(doc.defaultView.innerWidth / 2, doc.defaultView.innerHeight / 2);
        return element || doc.scrollingElement;
    }
    var doc = document;
    var element = target(doc);
    // Same-origin frames retain their own last pointer/focus context. A sandbox
    // or cross-origin frame cannot be inspected; never bypass that boundary.
    for (var depth = 0; element && depth < 8; ++depth) {
        if (element.tagName !== "IFRAME" && element.tagName !== "FRAME") break;
        try {
            if (!element.contentDocument) break;
            doc = element.contentDocument;
            element = target(doc);
        } catch (_) { break; }
    }
    var remainingX = dx, remainingY = dy;
    var movedX = 0, movedY = 0;
    for (var parents = 0; element && parents < 64; ++parents) {
        if (element.nodeType === 1) {
            var style = doc.defaultView.getComputedStyle(element);
            var page = element === doc.scrollingElement;
            var allowX = page || /^(auto|scroll|overlay)$/.test(style.overflowX);
            var allowY = page || /^(auto|scroll|overlay)$/.test(style.overflowY);
            var oldX = element.scrollLeft, oldY = element.scrollTop;
            element.scrollTo({left: oldX + (allowX ? remainingX : 0),
                              top: oldY + (allowY ? remainingY : 0), behavior: "instant"});
            var usedX = element.scrollLeft - oldX, usedY = element.scrollTop - oldY;
            remainingX -= usedX; remainingY -= usedY;
            movedX += usedX; movedY += usedY;
            if (allowX && /^(contain|none)$/.test(style.overscrollBehaviorX)) remainingX = 0;
            if (allowY && /^(contain|none)$/.test(style.overscrollBehaviorY)) remainingY = 0;
        }
        if (Math.abs(remainingX) < 0.01 && Math.abs(remainingY) < 0.01) break;
        if (element.parentElement) element = element.parentElement;
        else if (element.getRootNode && element.getRootNode().host) element = element.getRootNode().host;
        else if (element !== doc.scrollingElement) element = doc.scrollingElement;
        else break;
    }
    return {x: movedX, y: movedY};
}

// QML-compiled JavaScript functions stringify as `[code]`; never use
// Function.prototype.toString() to create a script for the WebEngine page.
var installationSource = [
    "(function installContextTracking() {",
    "  function install(doc) {",
    "    if (!doc || doc.__terminalJoystickTracking) return;",
    "    doc.__terminalJoystickTracking = true;",
    "    function remember(event) {",
    "      var path = event.composedPath ? event.composedPath() : [];",
    "      doc.__terminalJoystickTarget = path.length ? path[0] : event.target;",
    "      doc.__terminalJoystickFocusAtPointer = doc.activeElement;",
    "    }",
    "    doc.addEventListener('pointermove', remember, {passive:true});",
    "    doc.addEventListener('pointerdown', remember, {passive:true});",
    "    doc.addEventListener('focusin', remember, {passive:true});",
    "    function frames() {",
    "      Array.prototype.forEach.call(doc.querySelectorAll('iframe,frame'), function(frame) {",
    "        try { install(frame.contentDocument); } catch (_) {}",
    "      });",
    "    }",
    "    doc.addEventListener('load', frames, true);",
    "    frames();",
    "  }",
    "  install(document);",
    "})()"
].join("\n");

function installationScript() { return installationSource; }

var scrollSource = [
    "(function scrollContext(dx,dy) {",
    "  function target(doc) {",
    "    var element = doc.__terminalJoystickTarget;",
    "    var active = doc.activeElement;",
    "    if (!element || !element.isConnected ||",
    "        (active && active !== doc.body && active !== doc.documentElement &&",
    "         active !== doc.__terminalJoystickFocusAtPointer)) element = active;",
    "    if (!element || element === doc.body || element === doc.documentElement)",
    "      element = doc.elementFromPoint(doc.defaultView.innerWidth/2,doc.defaultView.innerHeight/2);",
    "    return element || doc.scrollingElement;",
    "  }",
    "  var doc=document, element=target(doc);",
    "  for (var depth=0; element && depth<8; ++depth) {",
    "    if (element.tagName !== 'IFRAME' && element.tagName !== 'FRAME') break;",
    "    try { if (!element.contentDocument) break; doc=element.contentDocument; element=target(doc); }",
    "    catch (_) { break; }",
    "  }",
    "  var remainingX=dx, remainingY=dy, movedX=0, movedY=0;",
    "  for (var parents=0; element && parents<64; ++parents) {",
    "    if (element.nodeType === 1) {",
    "      var style=doc.defaultView.getComputedStyle(element);",
    "      var page=element === doc.scrollingElement;",
    "      var allowX=page || /^(auto|scroll|overlay)$/.test(style.overflowX);",
    "      var allowY=page || /^(auto|scroll|overlay)$/.test(style.overflowY);",
    "      var oldX=element.scrollLeft, oldY=element.scrollTop;",
    "      element.scrollTo({left:oldX+(allowX?remainingX:0),top:oldY+(allowY?remainingY:0),behavior:'instant'});",
    "      var usedX=element.scrollLeft-oldX, usedY=element.scrollTop-oldY;",
    "      remainingX-=usedX; remainingY-=usedY; movedX+=usedX; movedY+=usedY;",
    "      if (allowX && /^(contain|none)$/.test(style.overscrollBehaviorX)) remainingX=0;",
    "      if (allowY && /^(contain|none)$/.test(style.overscrollBehaviorY)) remainingY=0;",
    "    }",
    "    if (Math.abs(remainingX)<0.01 && Math.abs(remainingY)<0.01) break;",
    "    if (element.parentElement) element=element.parentElement;",
    "    else if (element.getRootNode && element.getRootNode().host) element=element.getRootNode().host;",
    "    else if (element !== doc.scrollingElement) element=doc.scrollingElement;",
    "    else break;",
    "  }",
    "  return {x:movedX,y:movedY};",
    "})"
].join("\n");

function scrollScript(dx, dy) {
    if (!isFinite(dx) || !isFinite(dy)) return "void 0";
    return scrollSource + "(" + JSON.stringify(dx) + "," + JSON.stringify(dy) + ")";
}
