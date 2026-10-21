import QtQuick
import QtQuick.Controls
import Terminal.Native 1.0

Item {
    id:glass
    property color color:Theme.surface
    property real radius:Theme.shapeLarge
    property color borderColor:Theme.outline
    property real borderWidth:0
    property bool blurEnabled:true
    property real blurStrength:2
    property real tintOpacity:systemTools.moduleOpacity
    property bool staticBlur:false
    property Item blurCacheTexture:null
    property rect trackedRect:Qt.rect(0,0,1,1)
    readonly property rect sampleRect:sample.sourceRect
    readonly property bool sampleLive:sample.live
    readonly property Item cachedTexture:staticBlur ? (blurCacheTexture || (host && host.panelBlurTexture ? host.panelBlurTexture : null)) : null
    function captureStaticBlur(offset) {
        if(!staticBlur || !visible || !blurEnabled)return
        if(!backdrop)return
        const p=mapToItem(backdrop,0,0)
        trackedRect=Qt.rect(p.x+(offset?offset.x:0),p.y+(offset?offset.y:0),Math.max(1,width),Math.max(1,height))
        if(!cachedTexture)sample.scheduleUpdate()
    }
    readonly property var host:ApplicationWindow.window
    readonly property Item backdrop:host && host.blurBackdropItem !== undefined ? host.blurBackdropItem : null
    ShaderEffectSource {
        id:sample;parent:glass.backdrop?glass.backdrop.parent:glass;z:-99;width:1;height:1;sourceItem:glass.cachedTexture?null:glass.backdrop;live:!glass.staticBlur
        sourceRect:{if(glass.staticBlur){if(glass.host && typeof glass.host.panelBlurRect==="function")return glass.host.panelBlurRect(glass);if(glass.cachedTexture && typeof glass.cachedTexture.rectMapper==="function")return glass.cachedTexture.rectMapper(glass);return glass.trackedRect}if(!glass.backdrop)return Qt.rect(0,0,1,1);const p=glass.mapToItem(glass.backdrop,0,0);const w=Math.min(glass.width,glass.backdrop.width);const h=Math.min(glass.height,glass.backdrop.height);return Qt.rect(Math.max(0,Math.min(p.x,glass.backdrop.width-w)),Math.max(0,Math.min(p.y,glass.backdrop.height-h)),w,h)}
        onSourceRectChanged:if(glass.staticBlur && !glass.cachedTexture)sample.scheduleUpdate()
    }
    CachedBlur {objectName:"globalSurfaceBlur";anchors.fill:parent;source:glass.cachedTexture||sample;preblurred:glass.cachedTexture!==null
        sourceRect:glass.cachedTexture?Qt.rect(glass.sampleRect.x/glass.cachedTexture.width,glass.sampleRect.y/glass.cachedTexture.height,glass.sampleRect.width/glass.cachedTexture.width,glass.sampleRect.height/glass.cachedTexture.height):Qt.rect(0,0,1,1)
        visible:glass.blurEnabled;levels:systemTools.energySaving?2:4;resolutionScale:glass.cachedTexture?glass.cachedTexture.resolutionScale:systemTools.energySaving?0.25:0.5;strength:glass.blurStrength;cornerRadius:glass.radius}
    Rectangle {id:tint;anchors.fill:parent;color:glass.color;radius:glass.radius;opacity:glass.tintOpacity;border.color:glass.borderColor;border.width:glass.borderWidth}
}
