import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Terminal.Native 1.0

Item {
    id:root
    property var model
    property int revision:0
    property Item backgroundSource
    property Item backgroundBlurTexture
    readonly property bool efficientRendering:systemTools.energySaving
    // Larger app cards leave room for a visible 3D turn while paging.
    readonly property int columns:Math.max(2,Math.min(3,Math.floor(width/(260*Theme.scaleFactor))))
    readonly property int rows:Math.max(1,Math.min(2,Math.floor((height-24*Theme.scaleFactor)/(170*Theme.scaleFactor))))
    readonly property int capacity:columns*rows
    readonly property int pageCount:Math.max(1,Math.ceil((model?model.count:0)/capacity))
    property alias currentIndex:view.currentIndex
    property alias contentX:view.contentX
    signal activated(var entry,real originX,real originY)
    FontMetrics {id:titleMetrics;font.pointSize:Theme.title}
    FontMetrics {id:bodyMetrics;font.pointSize:Theme.body}
    function entryAt(index) {
        if(!model || index<0 || index>=model.count) return {entryId:"",title:"",description:"",category:"",accentColor:"#85deff",iconName:"extension",route:0,instance:"",ready:false}
        let e=model.get(index)
        return {entryId:e.entryId,title:e.title,description:e.description,category:e.category,accentColor:e.accentColor,iconName:e.iconName,route:e.route,instance:e.instance,ready:e.ready}
    }
    function reveal(instance) {
        for(let i=0;i<(model?model.count:0);++i) if(model.get(i).instance===instance) {view.currentIndex=Math.floor(i/capacity);return}
    }
    onPageCountChanged:if(view.currentIndex>=pageCount)view.currentIndex=pageCount-1
    Flickable {
        id:vertical;objectName:"homePage";anchors.fill:parent;clip:true
        contentWidth:width;contentHeight:Math.max(height,root.rows*170*Theme.scaleFactor+48*Theme.scaleFactor)
        boundsBehavior:Flickable.StopAtBounds;flickableDirection:Flickable.VerticalFlick
        ListView {
            id:view;objectName:"homeCarousel";width:parent.width;height:vertical.contentHeight-48*Theme.scaleFactor
            orientation:ListView.Horizontal;clip:true;model:root.pageCount
            snapMode:ListView.SnapOneItem;boundsBehavior:Flickable.StopAtBounds
            highlightRangeMode:ListView.StrictlyEnforceRange;preferredHighlightBegin:0;preferredHighlightEnd:width
            highlightMoveDuration:240;maximumFlickVelocity:2000*Theme.scaleFactor;flickDeceleration:2800*Theme.scaleFactor
            focus:true
            Keys.onLeftPressed:decrementCurrentIndex()
            Keys.onRightPressed:incrementCurrentIndex()
            delegate:Item {
                id:page;required property int index;width:view.width;height:view.height
                Repeater {
                    model:root.capacity
                    delegate:Item {
                        id:tile;objectName:"appTile";required property int index
                        property var entry:{let changed=root.revision;return root.entryAt(page.index*root.capacity+index)}
                        property string entryId:entry.entryId
                        property bool auditTile:true
                         width:page.width/root.columns
                         height:page.height/root.rows
                         x:(index%root.columns)*width
                         y:Math.floor(index/root.columns)*height
                        visible:entryId!==""
                         property real pageOffset:(page.x-view.contentX)/Math.max(1,view.width)
                         property real rotationDegrees:Math.max(-54,Math.min(54,-pageOffset*54))
                         property real depthScale:1-Math.min(0.1,Math.abs(pageOffset)*0.1)
                         property real iconSize:Math.min(100*Theme.scaleFactor,Math.max(68*Theme.scaleFactor,height-titleMetrics.height-bodyMetrics.height-Theme.gap*3))
                         Behavior on rotationDegrees { NumberAnimation { duration:180; easing.type:Easing.OutCubic } }
                         Behavior on depthScale { NumberAnimation { duration:180; easing.type:Easing.OutCubic } }
                        function launch() {
                            let point=iconSurface.mapToItem(null,iconSurface.width/2,iconSurface.height/2)
                            root.activated(entry,point.x,point.y)
                        }
                        activeFocusOnTab:true
                        Keys.onReturnPressed:launch()
                        Accessible.role:Accessible.Button;Accessible.name:entry.title+" "+entry.description
                        Accessible.onPressAction:launch()
                         transform: [
                             Rotation { id:cardRotation;objectName:"cardYRotation"; origin.x:tile.width/2; origin.y:tile.height/2; axis.x:0; axis.y:1; axis.z:0; angle:tile.rotationDegrees },
                             Scale { id:cardScale;origin.x:tile.width/2; origin.y:tile.height/2; xScale:tile.depthScale; yScale:tile.depthScale }
                         ]
                          ShaderEffectSource {
                              id:cardBackdrop;objectName:"launcherCardBackdrop";parent:root.backgroundSource?root.backgroundSource.parent:tile;z:-99;sourceItem:root.backgroundBlurTexture?null:root.backgroundSource;width:1;height:1;live:true
                             sourceRect:{
                                if(!root.backgroundSource)return Qt.rect(0,0,1,1)
                                const geometry=tile.x+tile.y+page.x+page.y+view.contentX+vertical.contentY+cardRotation.angle+cardScale.xScale+cardScale.yScale
                                if(!isFinite(geometry))return Qt.rect(0,0,1,1)
                                 const center=tile.mapToItem(root.backgroundSource,tile.width/2,tile.height/2)
                                 return Qt.rect(center.x-tile.width/2,center.y-tile.height/2,tile.width,tile.height)
                             }
                            textureSize:Qt.size(Math.max(1,tile.width*(efficientRendering?0.5:1)),Math.max(1,tile.height*(efficientRendering?0.5:1)))
                        }
                           CachedBlur {objectName:"launcherBackdropBlur";visible:!view.moving;anchors.fill:parent;source:root.backgroundBlurTexture||cardBackdrop
                               preblurred:root.backgroundBlurTexture!==null
                               sourceRect:root.backgroundBlurTexture?Qt.rect(cardBackdrop.sourceRect.x/root.backgroundBlurTexture.width,cardBackdrop.sourceRect.y/root.backgroundBlurTexture.height,cardBackdrop.sourceRect.width/root.backgroundBlurTexture.width,cardBackdrop.sourceRect.height/root.backgroundBlurTexture.height):Qt.rect(0,0,1,1)
                               levels:systemTools.energySaving?2:4;strength:2;mixAmount:1;resolutionScale:root.backgroundBlurTexture?root.backgroundBlurTexture.resolutionScale:systemTools.energySaving?0.25:0.5;cornerRadius:Theme.shapeExtraLarge}
                        Item {
                            objectName:"launcherCardChrome"
                            anchors.fill:parent
                              visible:true
                             anchors.margins:6*Theme.scaleFactor
                              opacity:systemTools.launcherOpacity
                            layer.enabled:!efficientRendering
                            Rectangle {
                                id:cardSurface
                                objectName:"launcherCardSurface"
                                anchors.fill:parent
                                radius:Theme.shapeExtraLarge
                                property int outlineWidth:page.index===view.currentIndex?2:1
                              color:efficientRendering?Theme.surfaceContainer:Qt.rgba(Theme.surfaceContainer.r,Theme.surfaceContainer.g,Theme.surfaceContainer.b,0.72)
                                border.width:outlineWidth
                                border.color:page.index===view.currentIndex?Theme.accent:Theme.outlineVariant
                            }
                            BlurItem {
                                objectName:"launcherCardBlur"
                                visible:!efficientRendering&&!view.moving
                                anchors.fill:cardSurface
                                source:cardSurface
                                levels:1;strength:0.35;mixAmount:0.12;opacity:0.9
                            }
                        }
                        Column {
                            anchors.centerIn:parent;width:parent.width;spacing:Theme.gap*1.25
                            Item {
                                objectName:"launcherLogo"
                                width:tile.iconSize;height:width;anchors.horizontalCenter:parent.horizontalCenter
                                scale:tap.pressed?0.94:1
                                Behavior on scale {NumberAnimation{duration:90}}
                                 AppIcon {id:iconSurface;anchors.fill:parent;iconName:tile.entry.iconName;accentColor:tile.entry.accentColor;layer.enabled:!efficientRendering}
                                // Blend a shallow GPU blur with 90% of the original glyph. Labels stay sharp.
                                 BlurItem {objectName:"launcherIconBlur";visible:false;anchors.fill:parent;source:iconSurface;levels:1;strength:0.35;mixAmount:0.10}
                                Rectangle {anchors.fill:parent;anchors.margins:-5*Theme.scaleFactor;radius:Theme.shapeFull;color:"transparent"
                                    border.width:tile.activeFocus?2:0;border.color:Theme.accent}
                                Rectangle {
                                    opacity:systemTools.launcherOpacity;visible:tile.entry.instance!=="";width:32*Theme.scaleFactor;height:width;radius:Theme.shapeFull
                                    anchors.right:parent.right;anchors.bottom:parent.bottom;color:Theme.surface
                                    MaterialIcon {anchors.centerIn:parent;width:24*Theme.scaleFactor;height:width;name:tile.entry.ready?"check":"refresh"}
                                }
                            }
                             CarText {id:nameText;objectName:"launcherTitle";width:parent.width;height:titleMetrics.height;text:tile.entry.title;heading:true;horizontalAlignment:Text.AlignHCenter
                                maximumLineCount:1;elide:Text.ElideRight}
                              CarText {id:descriptionText;objectName:"launcherDescription";visible:tile.entry.instance!=="";width:parent.width;height:bodyMetrics.height;text:tile.entry.description;subdued:true
                                 Accessible.ignored:tile.entry.instance==="";horizontalAlignment:Text.AlignHCenter;maximumLineCount:1;elide:Text.ElideRight}
                        }
                        TapHandler {id:tap;gesturePolicy:TapHandler.DragThreshold;onTapped:tile.launch()}
                    }
                }
            }
        }
        RowLayout {
            visible:root.pageCount>1
            anchors.top:view.bottom;anchors.horizontalCenter:parent.horizontalCenter;height:64*Theme.scaleFactor;spacing:Theme.gap
            IconButton {visible:root.width>420*Theme.scaleFactor;iconName:"chevron_left";label:I18n.t("上一页应用");enabled:view.currentIndex>0;onClicked:view.decrementCurrentIndex()}
            CarText {text:(view.currentIndex+1)+" / "+root.pageCount;subdued:true;horizontalAlignment:Text.AlignHCenter}
            IconButton {visible:root.width>420*Theme.scaleFactor;iconName:"chevron_right";label:I18n.t("下一页应用");enabled:view.currentIndex<root.pageCount-1;onClicked:view.incrementCurrentIndex()}
        }
    }
}
