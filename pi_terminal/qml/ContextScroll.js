.pragma library

var installSource = `(function() {
    function attach(doc) {
        if (!doc || doc.__nativeWheel) return;
        doc.__nativeWheel = { element: null, focus: null };
        function remember(event) {
            var path = event.composedPath ? event.composedPath() : [];
            doc.__nativeWheel.element = path[0] || event.target;
            doc.__nativeWheel.focus = doc.activeElement;
        }
        ["pointerdown", "pointermove", "focusin"].forEach(function(name) {
            doc.addEventListener(name, remember, {passive:true, capture:true});
        });
        function roots() {
            Array.prototype.forEach.call(doc.querySelectorAll("*"), function(node) {
                if (node.shadowRoot) attach(node.shadowRoot);
            });
        }
        roots();
        var view = doc.defaultView || (doc.ownerDocument && doc.ownerDocument.defaultView);
        if (view && view.MutationObserver) {
            var observer = new view.MutationObserver(roots);
            observer.observe(doc.documentElement || doc, {childList:true, subtree:true});
        }
        function frames() {
            Array.prototype.forEach.call(doc.querySelectorAll("iframe,frame"), function(frame) {
                try { attach(frame.contentDocument); } catch (_) {}
            });
        }
        doc.addEventListener("load", frames, true);
        frames();
    }
    attach(document);
    return true;
})()`;

var moveSource = `(function(dx, dy) {
    function choose(doc) {
        var state = doc.__nativeWheel || {};
        var node = state.element;
        var focus = doc.activeElement;
        if (!node || !node.isConnected || (focus && focus !== doc.body && focus !== doc.documentElement && focus !== state.focus)) node = focus;
        if (!node || node === doc.body || node === doc.documentElement)
            node = doc.elementFromPoint(doc.defaultView.innerWidth/2, doc.defaultView.innerHeight/2);
        return node || doc.scrollingElement;
    }
    var doc = document, node = choose(doc), frames = [];
    for (var depth=0; node && /^(IFRAME|FRAME)$/.test(node.tagName) && depth<8; depth++) {
        try {
            var child = node.contentDocument;
            if (!child) break;
            frames.push({doc:doc, node:node}); doc = child; node = choose(doc);
        } catch (_) { break; }
    }
    var rx=dx, ry=dy, mx=0, my=0;
    for (var count=0; node && count<128; count++) {
        if (node.nodeType === 1) {
            var style=doc.defaultView.getComputedStyle(node), page=node===doc.scrollingElement;
            var ax=page || /^(auto|scroll|overlay)$/.test(style.overflowX);
            var ay=page || /^(auto|scroll|overlay)$/.test(style.overflowY);
            var x=node.scrollLeft, y=node.scrollTop;
            node.scrollTo({left:x+(ax?rx:0),top:y+(ay?ry:0),behavior:"instant"});
            var ux=node.scrollLeft-x, uy=node.scrollTop-y;
            rx-=ux; ry-=uy; mx+=ux; my+=uy;
            if(/^(contain|none)$/.test(style.overscrollBehaviorX)) rx=0;
            if(/^(contain|none)$/.test(style.overscrollBehaviorY)) ry=0;
        }
        if(Math.abs(rx)<0.01 && Math.abs(ry)<0.01) break;
        if(node.parentElement) node=node.parentElement;
        else if(node.getRootNode && node.getRootNode().host) node=node.getRootNode().host;
        else if(node!==doc.scrollingElement) node=doc.scrollingElement;
        else if(frames.length) { var parent=frames.pop(); doc=parent.doc; node=parent.node; }
        else break;
    }
    return {x:mx,y:my};
})`;

function installScript() { return installSource; }
function moveScript(dx,dy) {
    if (!isFinite(dx) || !isFinite(dy)) return "void 0";
    return moveSource+"("+JSON.stringify(dx)+","+JSON.stringify(dy)+")";
}
