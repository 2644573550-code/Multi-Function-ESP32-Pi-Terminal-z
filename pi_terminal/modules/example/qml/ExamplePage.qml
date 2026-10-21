import QtQuick
import QtQuick.Controls

Rectangle {
    id: page
    property var sessionModel: null
    property var samples: []
    property var latest: sessionModel ? sessionModel.data : ({})
    property bool diagnostics: false
    // The host supplies this explicitly; ApplicationWindow.window can be null
    // during dynamic creation and must not leave a plugin at the wrong scale.
    property real hostUiScale: 1
    readonly property real uiScale: hostUiScale
    readonly property real gap: 16 * uiScale
    readonly property int columns: width >= 600 * uiScale ? 2 : 1
    readonly property real metricHeight: bodyMetrics.height * 2 + valueMetrics.height + 32 * uiScale
    readonly property real headerHeight: Math.max(heading.y + heading.height, toggle.y + toggle.height) + gap
    readonly property real bodyTop: sourceText.y + sourceText.height + gap
    implicitHeight: bodyTop + Math.max(dashboard.height, diagnostics ? report.height : 0) + gap
    color: "#071522"
    radius: 14 * uiScale
    TextMetrics { id:bodyMetrics; text:"模块发布的计数"; font.pointSize:18*page.uiScale }
    TextMetrics { id:valueMetrics; text:"12345"; font.pointSize:24*page.uiScale; font.bold:true }

    function appendSample() {
        if (!sessionModel || sessionModel.data.heartbeat === undefined) return
        const value = Number(sessionModel.data.heartbeat)
        if (!isFinite(value)) return
        samples = samples.concat([{value: value, at: Date.now()}]).slice(-36)
        chart.requestPaint()
    }
    function sensorValue(name, decimals, unit) {
        const value = latest[name]
        return value === undefined || value === null || !isFinite(Number(value))
                ? "—" : Number(value).toFixed(decimals) + " " + unit
    }
    Connections {
        target: page.sessionModel
        function onChanged() { page.appendSample() }
    }
    Component.onCompleted: appendSample()

    Text {
        id: heading
        x: page.gap; y: page.gap
        width: Math.max(0, page.width - 2 * page.gap - (page.columns === 2 ? toggle.width + page.gap : 0))
        text: "实时设备遥测"
        color: "#f2f8ff"; font.pointSize: 24 * page.uiScale; font.bold: true
        wrapMode: Text.Wrap
    }
    Text {
        id: sourceText
        x: page.gap; y: page.headerHeight
        width: Math.max(0, page.width - 2 * page.gap)
        text: String(page.latest.source || "等待模块业务数据")
        color: "#bacddd"; font.pointSize: 18 * page.uiScale
        elide: Text.ElideRight
    }
    Button {
        id: toggle
        anchors.right: parent.right; anchors.rightMargin: page.gap
        y: page.columns === 2 ? page.gap : heading.y + heading.height + page.gap
        width: Math.min(180 * page.uiScale, Math.max(0, page.width - 2 * page.gap))
        height: Math.max(56 * page.uiScale, implicitHeight)
        text: page.diagnostics ? "仪表盘" : "报文诊断"
        font.pointSize: 18 * page.uiScale
        onClicked: page.diagnostics = !page.diagnostics
    }

    Item {
        id: dashboard
        visible: !page.diagnostics
        x: page.gap; y: page.bodyTop
        width: Math.max(0, page.width - 2 * page.gap)
        height: trend.y + trend.height
        Repeater {
            id: metrics
            model: [
                {title:"温度",value:page.sensorValue("temperature_C",2,"°C"),detail:"BME280 · GPIO38/39",tint:"#82e3ff"},
                {title:"湿度",value:page.sensorValue("humidity_RH",2,"%RH"),detail:"BME280 · 相对湿度",tint:"#98e7c1"},
                {title:"气压",value:page.sensorValue("pressure_hPa",2,"hPa"),detail:"BME280 · 实测气压",tint:"#ffd68c"},
                {title:"光照",value:page.sensorValue("light_lux",1,"lx"),detail:"BH1750 · GPIO4/5",tint:"#ccb7ff"}
            ]
            delegate: Rectangle {
                id: card
                required property int index
                required property var modelData
                objectName: "telemetryMetric" + index
                x: (index % page.columns) * (dashboard.width + page.gap) / page.columns
                y: Math.floor(index / page.columns) * (card.height + page.gap)
                width: Math.max(0, (dashboard.width - page.gap * (page.columns - 1)) / page.columns)
                height: page.metricHeight
                color: "#102738"; radius: 10 * page.uiScale; border.color: "#46657c"
                Text { id:titleText; x:12*page.uiScale; y:8*page.uiScale; width:Math.max(0,parent.width-24*page.uiScale); text:card.modelData.title; color:"#d5e4f0"; font.pointSize:18*page.uiScale; elide:Text.ElideRight }
                Text { id:valueText; x:12*page.uiScale; y:titleText.y+titleText.height+8*page.uiScale; width:Math.max(0,parent.width-24*page.uiScale); text:card.modelData.value; color:card.modelData.tint; font.pointSize:24*page.uiScale; font.bold:true; elide:Text.ElideRight }
                Text { id:detailText; x:12*page.uiScale; y:valueText.y+valueText.height+8*page.uiScale; width:Math.max(0,parent.width-24*page.uiScale); text:card.modelData.detail; color:"#b5c9d9"; font.pointSize:18*page.uiScale; elide:Text.ElideRight }
            }
        }
        Rectangle {
            id: trend
            x:0; y:Math.ceil(4 / page.columns) * (page.metricHeight + page.gap)
            width:dashboard.width; height:trendTitle.height + 144 * page.uiScale
            color: "#102738"; radius: 10 * page.uiScale; border.color: "#46657c"
            Text { id:trendTitle; x:12*page.uiScale; y:8*page.uiScale; width:Math.max(0,parent.width-24*page.uiScale); text:"心跳趋势 · 实际收到的数据"; color:"#e4eff8"; font.pointSize:18*page.uiScale; wrapMode:Text.Wrap }
            Canvas {
                id: chart
                x:12*page.uiScale; y:trendTitle.y+trendTitle.height+8*page.uiScale
                width:Math.max(0,parent.width-24*page.uiScale); height:110*page.uiScale
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    const ctx=getContext("2d")
                    ctx.clearRect(0,0,width,height)
                    if (page.samples.length < 2 || width < 1) return
                    const values=page.samples.map(s=>s.value)
                    const lo=Math.min.apply(null,values)-1, hi=Math.max.apply(null,values)+1
                    ctx.strokeStyle="#82e3ff";ctx.lineWidth=2*page.uiScale
                    ctx.beginPath()
                    for(let n=0;n<values.length;n++) {
                        const px=width*n/(values.length-1),py=height-8-(height-16)*(values[n]-lo)/Math.max(2,hi-lo)
                        if(n===0)ctx.moveTo(px,py);else ctx.lineTo(px,py)
                    }
                    ctx.stroke()
                }
            }
        }
    }
    TextArea {
        id: report
        visible: page.diagnostics
        x:page.gap; y:page.bodyTop
        width:Math.max(0,page.width-2*page.gap)
        height:Math.max(240*page.uiScale, contentHeight + topPadding + bottomPadding)
        readOnly:true; wrapMode:TextEdit.Wrap
        text:JSON.stringify(page.latest,null,2)
        color:"#e4eff8";font.pointSize:18*page.uiScale
        background:Rectangle { color:"#102738";radius:10*page.uiScale }
    }
}
