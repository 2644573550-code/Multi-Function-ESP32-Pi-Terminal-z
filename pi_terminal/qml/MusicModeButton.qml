import QtQuick

IconButton {
    id: control
    property string mode: "order"
    contentItem: Canvas {
        id: drawing
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.strokeStyle = control.filled ? "#07120b" : "#ffffff"
            ctx.fillStyle = ctx.strokeStyle
            ctx.lineWidth = 2; ctx.lineCap = "round"; ctx.lineJoin = "round"
            ctx.save(); ctx.translate(width / 2 - 12, height / 2 - 12)
            function path(points) {
                ctx.beginPath(); ctx.moveTo(points[0][0], points[0][1])
                for (let i = 1; i < points.length; ++i) ctx.lineTo(points[i][0], points[i][1])
                ctx.stroke()
            }
            if (control.mode === "shuffle") {
                path([[3,6],[7,6],[17,18],[21,18]])
                path([[3,18],[7,18],[17,6],[21,6]])
                path([[18,3],[21,6],[18,9]]); path([[18,15],[21,18],[18,21]])
            } else if (control.mode === "order") {
                path([[3,5],[20,5]]); path([[3,12],[20,12]]); path([[3,19],[15,19]])
                path([[18,16],[21,19],[18,22]])
            } else {
                path([[4,10],[4,6],[20,6],[17,3]]); path([[20,6],[17,9]])
                path([[20,14],[20,18],[4,18],[7,21]]); path([[4,18],[7,15]])
                if (control.mode === "one") { ctx.font = "bold 10px sans-serif"; ctx.fillText("1",9,15) }
            }
            ctx.restore()
        }
        Connections { target: control; function onModeChanged() { drawing.requestPaint() } function onFilledChanged() { drawing.requestPaint() } }
    }
}
