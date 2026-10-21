import QtQuick
import QtQuick.Controls
import Terminal.Native 1.0

Item {
    id: root
    objectName: "desktopPetOverlay"
    property var hostWindow: null
    property var hostStatusDock: null
    property var hostPetManager: null
    property var hostPetMenu: null
    property var hostTrustDialog: null
    property var hostPowerConfirm: null
    property var hostSystemActionsDialog: null
    property bool active: hostWindow && hostStatusDock && hostPetManager && hostPetMenu
                          && hostTrustDialog && hostPowerConfirm && hostSystemActionsDialog
                          && hostWindow.petEasterEggEnabled && hostWindow.startupComplete
                          && hostStatusDock.visible && hostWindow.visible
                          && !hostTrustDialog.visible && !hostPowerConfirm.visible
                          && !hostSystemActionsDialog.visible
                          && !hostPetManager.opened && !hostPetMenu.visible
    property bool roaming: true
    property bool reducedMotion: false
    property real floorY: hostWindow ? hostWindow.contentItem.height : height
    property real petScale: hostWindow && hostPetManager
                            ? Math.min(1.0, Math.max(0.5, hostWindow.width / 1365))*hostPetManager.library.sizeMultiplier
                            : 0.75
    property real speed: hostWindow && hostWindow.efficientRendering ? 26 : 38
    property real petX: 0
    property int direction: 1
    property string action: "idle"
    property real idleRemaining: 2
    property url modelFolder: hostPetManager ? hostPetManager.library.currentFolder : "qrc:/arkpets/amiya"
    property bool dragging: false
    property real dragY: 0
    property real petY: groundY
    property real velocityX: 0
    property real velocityY: 0
    property real gravity: 1200
    property bool falling: false
    property real behaviorRemaining: 6
    property bool automaticBehaviors: true
    readonly property real poseFootY: sprite.ready ? sprite.frameFootY : 184
    // Stored size is a preference. Cap only the effective scale when a small
    // host cannot fit it; retain the user's setting for larger windows.
    readonly property real renderScale: Math.max(0.01,Math.min(
        petScale*((sprite.info.targetHeight || 176)/(sprite.info.referenceHeight || sprite.info.frameHeight || 192)),
        width>0?width/(sprite.info.frameWidth||192):1,
        floorY>0?floorY/(sprite.info.canvasHeight||sprite.info.frameHeight||192):1))
    readonly property real groundY: Math.max(0, floorY-poseFootY*renderScale)
    property int frameIndex: 0
    property real frameTime: 0
    readonly property real maximumX: Math.max(0, width - character.width)
    readonly property bool interacting: action === "interact"
    readonly property bool assetsReady: sprite.ready
    readonly property var animationInfo: sprite.info
    signal interacted()
    signal dragFinished(real position)
    function t(text) { return typeof systemTools === "undefined" ? text : I18n.t(text) }
    visible: active && width > 0 && floorY > 0
    // Keep other poses behind the taskbar crop. The seated pose is intentionally
    // allowed to draw its complete silhouette, including dangling legs below
    // the seat/contact line.
    clip: action !== "sit"

    function clampPosition() { petX = Math.max(0, Math.min(maximumX, petX)) }
    function interact() {
        if (!active || interacting || dragging || falling) return
        action = "interact"; interacted()
    }
    function finishInteraction() { action = "idle"; idleRemaining = 2 + Math.random()*3 }
    function supports(name) {
        return sprite.ready && animationInfo.actions[name] !== undefined && animationInfo.actions[name].supported !== false
    }
    function setBehavior(name) {
        if (dragging || falling || !supports(name)) return false
        action=name; behaviorRemaining= name==="sleep" ? 12+Math.random()*12 : 5+Math.random()*6
        return true
    }
    function chooseBehavior(randomValue) {
        const value=randomValue===undefined?Math.random():randomValue
        const next=value<0.48?"walk":value<0.72?"idle":value<0.89?"sit":"sleep"
        if (!setBehavior(next)) setBehavior("idle")
        idleRemaining=2+Math.random()*3
    }
    function beginDrag() {
        petY=character.y;dragY=petY;falling=false;velocityX=0;velocityY=0
        dragging = true; action = "idle"
    }
    function moveDrag(x, y) {
        if (!dragging) return
        petX = Math.max(0, Math.min(maximumX, x))
        dragY = Math.max(0, Math.min(Math.max(0, height-character.height), y))
        petY=dragY
    }
    function endDrag() {
        if (!dragging) return
        dragging = false; clampPosition(); finishInteraction()
        falling=petY<groundY
        if (!falling) {petY=groundY;velocityX=0;velocityY=0}
        dragFinished(petX)
    }
    function advancePhysics(seconds) {
        if (!active || !visible || dragging || !falling) return
        // Small fixed-size substeps avoid floor/wall tunneling after a slow
        // frame. Physics remains active even when roaming/reduced motion is off.
        let remaining=Math.max(0,Math.min(0.25,seconds))
        while (remaining>0.00001 && falling) {
            const dt=Math.min(1/120,remaining);remaining-=dt
            petY+=velocityY*dt+0.5*gravity*dt*dt
            velocityY+=gravity*dt;petX+=velocityX*dt
            if(petX<0||petX>maximumX){clampPosition();velocityX=-velocityX*0.25}
            if(petY<0){petY=0;velocityY=Math.max(0,velocityY)}
            if(petY>=groundY){petY=groundY;falling=false;velocityX=0;velocityY=0;finishInteraction()}
        }
    }
    function advance(seconds) {
        advancePhysics(seconds)
        if (!active || !visible || !roaming || interacting || reducedMotion || dragging || falling) return
        const dt = Math.max(0, Math.min(0.1, seconds))
        if(automaticBehaviors) {
            behaviorRemaining-=dt
            if(behaviorRemaining<=0){chooseBehavior();return}
        }
        if (action === "idle") {
            idleRemaining -= dt
            if (idleRemaining <= 0) {
                if(supports("walk"))action="walk"
                else {action="idle";idleRemaining=3}
            }
        } else if (action === "walk") {
            petX += direction*speed*dt
            if (petX <= 0 || petX >= maximumX) {
                clampPosition(); direction = petX <= 0 ? 1 : -1
                action = "idle"; idleRemaining = 1 + Math.random()*2
            }
        }
    }
    function animate(seconds) {
        if (!sprite.ready || (reducedMotion && !interacting)) return
        const a = animationInfo.actions[action]
        frameTime += Math.max(0, Math.min(0.1, seconds))
        const frames = Math.floor(frameTime*a.fps)
        if (interacting && frames >= a.frames) { finishInteraction(); return }
        frameIndex = frames % a.frames
    }
    onActionChanged: { frameIndex = 0; frameTime = 0 }
    onModelFolderChanged: { action = "idle"; frameIndex = 0; frameTime = 0; idleRemaining = 2;falling=false;velocityY=0;velocityX=0;petY=groundY }
    onGroundYChanged: {
        if(!dragging && (!falling||petY>=groundY)){petY=groundY;falling=false;velocityY=0;velocityX=0}
    }
    onWidthChanged: clampPosition()
    onMaximumXChanged: clampPosition()
    onPetScaleChanged: clampPosition()
    onRenderScaleChanged: {
        clampPosition()
        if(dragging){dragY=Math.max(0,Math.min(dragY,height-character.height));petY=dragY}
    }
    onActiveChanged: {
        clock.lastTime = 0
        if (!active) { dragging = false; falling=false;velocityY=0;velocityX=0;petY=groundY;action = "idle"; idleRemaining = 2 }
    }
    onReducedMotionChanged: if (reducedMotion && !interacting) action = "idle"
    Item {
        id: character
        objectName: "desktopPetCharacter"
        x: root.petX
        y: root.dragging ? root.dragY : root.falling ? root.petY : root.groundY
        width: (sprite.info.frameWidth || 144)*root.renderScale
        height: (sprite.info.canvasHeight || sprite.info.frameHeight || 192)*root.renderScale
        containmentMask: sprite
        PetSpriteItem {
            id: sprite
            objectName: "desktopPetSprite"
            anchors.fill: parent
            folder: root.modelFolder
            action: root.action
            currentFrame: root.frameIndex
            // direction is the signed travel direction; the source sprite's
            // native orientation faces right, so only leftward travel mirrors.
            mirrored: root.direction < 0
        }
        MouseArea {
            anchors.fill: parent
            // Use the same alpha mask as the scene-graph renderer.
            containmentMask: sprite
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            preventStealing: true
            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
            property point pressPoint
            property point startPosition
            property point lastPoint
            property double lastTime: 0
            property bool heldMenu: false
            onPressed: function(mouse) {
                heldMenu=false
                if(mouse.button===Qt.RightButton) return
                pressPoint = mapToItem(root, mouse.x, mouse.y)
                startPosition = Qt.point(character.x, character.y)
                lastPoint=pressPoint;lastTime=Date.now()
            }
            onPositionChanged: function(mouse) {
                if (!pressed) return
                if(!(pressedButtons & Qt.LeftButton)) return
                const p = mapToItem(root, mouse.x, mouse.y)
                const dx = p.x-pressPoint.x, dy = p.y-pressPoint.y
                if (!root.dragging && Math.hypot(dx,dy) > Qt.styleHints.startDragDistance) root.beginDrag()
                if (root.dragging) root.moveDrag(startPosition.x+dx, startPosition.y+dy)
                const now=Date.now(),elapsed=(now-lastTime)/1000
                if(elapsed>=0.008) {
                    root.velocityX=Math.max(-500,Math.min(500,(p.x-lastPoint.x)/elapsed))
                    root.velocityY=Math.max(-600,Math.min(600,(p.y-lastPoint.y)/elapsed))
                    lastPoint=p;lastTime=now
                }
            }
            onReleased: function(mouse) {
                if(mouse.button===Qt.RightButton){behaviorMenu.popup();return}
                if(heldMenu)return
                if (root.dragging) {
                    if(Date.now()-lastTime>120){root.velocityX=0;root.velocityY=0}
                    root.endDrag()
                }
                else root.interact()
            }
            onCanceled: root.endDrag()
            onPressAndHold:function(mouse){if(!root.dragging){heldMenu=true;behaviorMenu.popup();mouse.accepted=true}}
        }
        Menu {
            id:behaviorMenu
            MenuItem {text:root.t("自动活动");checkable:true;checked:root.automaticBehaviors;onTriggered:root.automaticBehaviors=!root.automaticBehaviors}
            MenuItem {text:root.t("行走");enabled:root.supports("walk")&&!root.falling;onTriggered:{root.setBehavior("walk");root.roaming=true}}
            MenuItem {text:root.t("待机");enabled:!root.falling;onTriggered:root.setBehavior("idle")}
            MenuItem {text:root.t("坐下");enabled:root.supports("sit")&&!root.falling;onTriggered:root.setBehavior("sit")}
            MenuItem {text:root.t("躺下");enabled:root.supports("sleep")&&!root.falling;onTriggered:root.setBehavior("sleep")}
        }
    }
    Timer {
        id: clock
        objectName:"desktopPetClock"
        property double lastTime: 0
        interval: 33; repeat: true
        running: root.visible
        onRunningChanged: lastTime = 0
        onTriggered: {
            const now = Date.now()
            if (lastTime > 0) { root.advance((now-lastTime)/1000); root.animate((now-lastTime)/1000) }
            lastTime = now
        }
    }
}
