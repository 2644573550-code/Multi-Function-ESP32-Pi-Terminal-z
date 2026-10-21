import QtQuick
import Terminal.Native 1.0

// Two persistent GPU slots: publish a replacement only after its first valid
// frame. Neither wallpaper changes nor quality changes expose an empty FBO.
Item {
    id: cache
    required property Item backdrop
    property real strength: 2
    property int levels: 2
    property real resolutionScale: 0.25
    property var rectMapper: null
    property bool inputReady: true
    property Item activeTexture: null
    property int generation: 0
    property bool queued: false
    property bool dirty: false
    property int pendingSlot: -1
    readonly property bool ready: activeTexture !== null
    readonly property bool busy: queued || pendingSlot >= 0
    z: -100
    function invalidate() {activeTexture=null;pendingSlot=-1;generation++;dirty=true}
    function requestUpdate() {
        dirty=true
        if(queued || pendingSlot>=0)return
        queued=true
        Qt.callLater(function() {
            cache.queued=false
            if(!cache.inputReady)return
            if(!cache.backdrop || cache.backdrop.width<=0 || cache.backdrop.height<=0)return
            cache.dirty=false
            const slot=cache.activeTexture===first?second:first
            cache.pendingSlot=slot===first?0:1
            slot.captureWidth=cache.backdrop.width;slot.captureHeight=cache.backdrop.height
            slot.captureLevels=cache.levels;slot.captureResolution=cache.resolutionScale
            slot.captureStrength=cache.strength
            slot.captureGeneration=++cache.generation
            slot.captureSource.scheduleUpdate()
        })
    }
    function publish(slot,revision) {
        if(revision!==generation || slot.renderRevision!==revision || slot.width<=1 || slot.height<=1)return
        if((pendingSlot===0 && slot!==first) || (pendingSlot===1 && slot!==second) || pendingSlot<0)return
        if(dirty){pendingSlot=-1;requestUpdate();return}
        activeTexture=slot;pendingSlot=-1
    }
    onLevelsChanged:requestUpdate()
    onResolutionScaleChanged:requestUpdate()
    onStrengthChanged:requestUpdate()
    onInputReadyChanged:if(inputReady)requestUpdate()
    Connections {target:cache.backdrop;function onWidthChanged(){cache.requestUpdate()}function onHeightChanged(){cache.requestUpdate()}}
    Component.onCompleted:requestUpdate()
    component Slot: BlurItem {
        id:slot
        renderOnly:true
        readonly property var rectMapper:cache.rectMapper
        property real captureWidth: 1
        property real captureHeight: 1
        property int captureLevels: 2
        property real captureResolution: 0.25
        property real captureStrength: 2
        property int captureGeneration: 0
        property alias captureSource: snapshot
        width:captureWidth;height:captureHeight
        source:snapshot;levels:captureLevels;resolutionScale:captureResolution;strength:captureStrength
        onFrameReady:function(revision){cache.publish(slot,revision)}
        // Native consumers do not refFromEffectItem like ShaderEffect does.
        // Keep a tiny source node behind the opaque desktop so Qt configures
        // its layer even when it has no ShaderEffect consumer of its own.
        ShaderEffectSource {
            id:snapshot;width:1;height:1;sourceItem:cache.backdrop;live:false
            sourceRect:Qt.rect(0,0,slot.captureWidth,slot.captureHeight)
            onScheduledUpdateCompleted: {
                if(cache.pendingSlot===(slot===first?0:1) && slot.captureGeneration===cache.generation)
                    slot.renderRevision=slot.captureGeneration
            }
        }
    }
    Slot {id:first;visible:cache.activeTexture===first || cache.pendingSlot===0}
    Slot {id:second;visible:cache.activeTexture===second || cache.pendingSlot===1}
}
